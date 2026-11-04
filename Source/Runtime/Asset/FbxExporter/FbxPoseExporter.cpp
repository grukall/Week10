#include "EnginePCH.h"
#include "FbxPoseExporter.h"

#include "Core/EngineLog.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/FbxImporter/FbxImporter.h"

#include <fbxsdk.h>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <filesystem>
#include <limits>

namespace
{
    // 원본 Node를 경로로 찾음
    FbxNode* FindNodeBySourcePath(FbxNode* Node, const FString& SourceNodePath)
    {
        if (!Node)
        {
            return nullptr;
        }
        if (FFbxImporter::BuildSourceNodePath(Node) == SourceNodePath)
        {
            return Node;
        }

        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
        {
            if (FbxNode* Found = FindNodeBySourcePath(Node->GetChild(ChildIndex), SourceNodePath))
            {
                return Found;
            }
        }
        return nullptr;
    }

    // 엔진 행렬을 FBX 행렬로 변환
    FbxAMatrix ConvertEngineMatrix(const FMatrix& Source)
    {
        FbxAMatrix Result;
        double* Data = Result;
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
            {
                Data[Row * 4 + Column] = static_cast<double>(Source.M[Row][Column]);
            }
        }
        return Result;
    }

    FMatrix ConvertFbxMatrix(const FbxAMatrix& Source)
    {
        FMatrix Result;
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
            {
                Result.M[Row][Column] = static_cast<float>(Source.Get(Row, Column));
            }
        }
        return Result;
    }
}

bool FbxPoseExporter::ExportReboundFbx(const FString& SourcePath, const FString& DestinationPath, FSkeletalMeshData& MeshData, TArray<FMatrix>& LocalPoseMatrices)
{
    if (SourcePath.empty() || DestinationPath.empty() || MeshData.RefBones.IsEmpty() || LocalPoseMatrices.Num() != MeshData.RefBones.Num())
    {
        HTR_LOG(Error, "[FBX Export] Invalid arguments.");
        return false;
    }

    std::error_code PathError;
    if (!std::filesystem::is_regular_file(SourcePath, PathError))
    {
        HTR_LOG(Error, "[FBX Export] Source FBX does not exist: {}", SourcePath);
        return false;
    }

    const std::filesystem::path AbsoluteSourcePath = std::filesystem::absolute(SourcePath).lexically_normal();
    const std::filesystem::path AbsoluteDestinationPath = std::filesystem::absolute(DestinationPath).lexically_normal();

    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        return false;
    }
    const auto Fail = [&](const FString& Message)
        {
            HTR_LOG(Error, "[FBX Export] {}", Message);
            Manager->Destroy();
            return false;
        };

    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    if (!IOSettings)
    {
        return Fail("Failed to create FbxIOSettings.");
    }
    Manager->SetIOSettings(IOSettings);

    FbxImporter* Importer = FbxImporter::Create(Manager, "ReboundImporter");
    if (!Importer)
    {
        return Fail("Failed to create FbxImporter.");
    }
    if (!Importer->Initialize(AbsoluteSourcePath.string().c_str(), -1, Manager->GetIOSettings()))
    {
        const FString ErrorMessage = FString("Failed to initialize importer: ") + Importer->GetStatus().GetErrorString();
        Importer->Destroy();
        return Fail(ErrorMessage);
    }

    FbxScene* Scene = FbxScene::Create(Manager, "ReboundScene");
    if (!Scene)
    {
        Importer->Destroy();
        return Fail("Failed to create FbxScene.");
    }
    if (!Importer->Import(Scene))
    {
        Importer->Destroy();
        return Fail("Failed to import source FBX");
    }

    Importer->Destroy();
    FbxNode* SceneRoot = Scene->GetRootNode();

    FFbxImporter::NormalizeScene(Scene);

    TArray<FbxNode*> BoneNodes;
    TArray<FMatrix> NewEngineGlobalMatrices;
    TArray<FbxAMatrix> NewFbxGlobalMatrices;

    BoneNodes.SetNum(MeshData.RefBones.Num());
    NewEngineGlobalMatrices.SetNum(MeshData.RefBones.Num());
    NewFbxGlobalMatrices.SetNum(MeshData.RefBones.Num());

    std::unordered_map<FbxNode*, int32> BoneIndexByNode;
    for (int32 BoneIndex = 0; BoneIndex < MeshData.RefBones.Num(); ++BoneIndex)
    {
        const FSkeletalMeshRefBone& Bone = MeshData.RefBones[BoneIndex];
        if (Bone.SourceNodePath.empty())
        {
            return Fail(FString("Bone has no source node path: ") + Bone.Name.ToString());
        }

        FbxNode* BoneNode = FindNodeBySourcePath(SceneRoot, Bone.SourceNodePath);
        if (Bone.ParentIndex < -1 || Bone.ParentIndex >= BoneIndex)
        {
            return Fail(FString("Bone has an invalid parent index: ") + Bone.Name.ToString());
        }

        FMatrix NewGlobalMatrix = LocalPoseMatrices[BoneIndex];
        if (Bone.ParentIndex >= 0)
        {
            NewGlobalMatrix = NewGlobalMatrix * NewEngineGlobalMatrices[Bone.ParentIndex];
        }

        BoneNodes[BoneIndex] = BoneNode;
        NewEngineGlobalMatrices[BoneIndex] = NewGlobalMatrix;
        NewFbxGlobalMatrices[BoneIndex] = ConvertEngineMatrix(NewGlobalMatrix);
        BoneIndexByNode[BoneNode] = BoneIndex;
    }

    FbxAnimEvaluator* AnimationEvaluator = Scene->GetAnimationEvaluator();
    if (!AnimationEvaluator)
    {
        return Fail("Scene has no animation evaluator.");
    }

    for (FbxNode* BoneNode : BoneNodes)
    {
        BoneNode->ResetPivotSet(FbxNode::eSourcePivot);
        BoneNode->SetTransformationInheritType(FbxTransform::eInheritRSrs);
        BoneNode->SetRotationActive(true);
        BoneNode->SetRotationOrder(FbxNode::eSourcePivot, eEulerXYZ);
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
    {
        FbxVector4 Translation;
        FbxVector4 Rotation;
        FbxVector4 Scale;

        const int32 ParentBoneIndex = MeshData.RefBones[BoneIndex].ParentIndex;
        const bool bDirectBoneParent = ParentBoneIndex >= 0
            && BoneNodes[BoneIndex]->GetParent() == BoneNodes[ParentBoneIndex];
        const bool bDirectSceneRoot = ParentBoneIndex < 0
            && BoneNodes[BoneIndex]->GetParent() == SceneRoot;

        if (bDirectBoneParent || bDirectSceneRoot)
        {
            const FbxAMatrix TargetLocalMatrix = ConvertEngineMatrix(LocalPoseMatrices[BoneIndex]);
            Translation = TargetLocalMatrix.GetT();
            Rotation = TargetLocalMatrix.GetR();
            Scale = TargetLocalMatrix.GetS();
        }
        else
        {
            FbxAMatrix TargetGlobalMatrix = NewFbxGlobalMatrices[BoneIndex];
            AnimationEvaluator->ComputeLocalTRSFromGlobal(Translation, Rotation, Scale, BoneNodes[BoneIndex], 
                TargetGlobalMatrix, FBXSDK_TIME_INFINITE, FbxNode::eSourcePivot, false, true);
        }

        BoneNodes[BoneIndex]->LclTranslation.Set(FbxDouble3(Translation[0], Translation[1], Translation[2]));
        BoneNodes[BoneIndex]->LclRotation.Set(FbxDouble3(Rotation[0], Rotation[1], Rotation[2]));
        BoneNodes[BoneIndex]->LclScaling.Set(FbxDouble3(Scale[0], Scale[1], Scale[2]));
        AnimationEvaluator->Reset();
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
    {
        const FbxAMatrix ActualGlobal = BoneNodes[BoneIndex]->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE);
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
            {
                const double Expected = NewEngineGlobalMatrices[BoneIndex].M[Row][Column];
                const double Actual = ActualGlobal.Get(Row, Column);
                if (!std::isfinite(Actual) || std::fabs(Expected - Actual) > 0.0001 * (1.0 + std::fabs(Expected)))
                {
                    return Fail("FBX node transform does not match the edited pose.");
                }
            }
        }
        NewFbxGlobalMatrices[BoneIndex] = ActualGlobal;
    }

    std::vector<FbxNode*> NodesToVisit;
    std::vector<FbxNode*> TargetMeshNodes;
    NodesToVisit.push_back(SceneRoot);

    while (!NodesToVisit.empty())
    {
        FbxNode* Node = NodesToVisit.back();
        NodesToVisit.pop_back();

        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
        {
            NodesToVisit.push_back(Node->GetChild(ChildIndex));
        }

        FbxMesh* Mesh = Node->GetMesh();
        if (!Mesh)
        {
            continue;
        }

        bool bUsesEditedSkeleton = false;
        const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            if (!Skin)
            {
                continue;
            }

            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (!Cluster || Cluster->GetControlPointIndicesCount() == 0)
                {
                    continue;
                }

                if (BoneIndexByNode.contains(Cluster->GetLink()))
                {
                    bUsesEditedSkeleton = true;
                }
            }
        }

        if (bUsesEditedSkeleton)
        {
            TargetMeshNodes.push_back(Node);
        }
    }

    if (TargetMeshNodes.empty())
    {
        return Fail("No skinned mesh uses the edited skeleton.");
    }

    for (FbxNode* MeshNode : TargetMeshNodes)
    {
        FbxMesh* Mesh = MeshNode->GetMesh();
        const int32 ControlPointCount = Mesh->GetControlPointsCount();
        if (ControlPointCount <= 0)
        {
            return Fail("Mesh has no control points.");
        }

        const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            if (!Skin)
            {
                return Fail(FString("Mesh has an invalid skin: ") + MeshNode->GetName());
            }

            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (!Cluster || Cluster->GetControlPointIndicesCount() == 0)
                {
                    continue;
                }

                if (!Cluster->GetLink() || !BoneIndexByNode.contains(Cluster->GetLink()))
                {
                    return Fail(FString("Mesh has a cluster outside the edited skeleton: ") + MeshNode->GetName());
                }
            }
        }

        std::vector<FbxVector4> BakedControlPoints(static_cast<size_t>(ControlPointCount));
        const FbxVector4* OriginalControlPoints = Mesh->GetControlPoints();
        for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointCount; ++ControlPointIndex)
        {
            BakedControlPoints[ControlPointIndex] = OriginalControlPoints[ControlPointIndex];
        }

        FbxAMatrix GeometryMatrix;
        GeometryMatrix.SetT(MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot));
        GeometryMatrix.SetR(MeshNode->GetGeometricRotation(FbxNode::eSourcePivot));
        GeometryMatrix.SetS(MeshNode->GetGeometricScaling(FbxNode::eSourcePivot));

        std::vector<FVector> WeightedPoints(static_cast<size_t>(ControlPointCount), FVector::ZeroVector);
        std::vector<double> WeightSums(static_cast<size_t>(ControlPointCount), 0.0);

        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (!Cluster || Cluster->GetControlPointIndicesCount() == 0)
                {
                    continue;
                }

                const int32 BoneIndex = BoneIndexByNode.at(Cluster->GetLink());

                FbxAMatrix MeshBind;
                FbxAMatrix BoneBind;
                Cluster->GetTransformMatrix(MeshBind);
                Cluster->GetTransformLinkMatrix(BoneBind);

                const FMatrix MeshToBind = ConvertFbxMatrix(MeshBind * GeometryMatrix);
                const FMatrix SourceBoneBind = ConvertFbxMatrix(BoneBind);
                const FMatrix CachedBindIdentity = SourceBoneBind * MeshData.RefBones[BoneIndex].InverseBindMatrix;

                for (int32 Row = 0; Row < 4; ++Row)
                {
                    for (int32 Column = 0; Column < 4; ++Column)
                    {
                        if (!std::isfinite(CachedBindIdentity.M[Row][Column]) ||
                            std::fabs(CachedBindIdentity.M[Row][Column] - (Row == Column ? 1.0f : 0.0f)) > 0.001f)
                        {
                            return Fail(FString("Source FBX bind differs from loaded mesh; reimport source: ") + Cluster->GetLink()->GetName());
                        }
                    }
                }

                const FMatrix BakeMatrix = MeshToBind * MeshData.RefBones[BoneIndex].InverseBindMatrix 
                    * NewEngineGlobalMatrices[BoneIndex] * MeshToBind.Inverse();
                const int* PointIndices = Cluster->GetControlPointIndices();
                const double* Weights = Cluster->GetControlPointWeights();

                for (int32 InfluenceIndex = 0; InfluenceIndex < Cluster->GetControlPointIndicesCount(); ++InfluenceIndex)
                {
                    const int32 PointIndex = PointIndices[InfluenceIndex];
                    const double Weight = Weights[InfluenceIndex];

                    if (PointIndex < 0 || PointIndex >= ControlPointCount || !std::isfinite(Weight) || Weight < 0.0)
                    {
                        return Fail("Invalid skin influence during bake.");
                    }

                    const FbxVector4& Point = OriginalControlPoints[PointIndex];
                    WeightedPoints[PointIndex] += BakeMatrix.TransformPosition(FVector(static_cast<float>(Point[0]), 
                        static_cast<float>(Point[1]), static_cast<float>(Point[2]))) * static_cast<float>(Weight);
                    WeightSums[PointIndex] += Weight;
                }
            }
        }

        for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointCount; ++ControlPointIndex)
        {
            if (WeightSums[ControlPointIndex] > 0.0)
            {
                const FVector Point = WeightedPoints[ControlPointIndex] / static_cast<float>(WeightSums[ControlPointIndex]);
                BakedControlPoints[ControlPointIndex] = FbxVector4(Point.X, Point.Y, Point.Z);
            }
            Mesh->SetControlPointAt(BakedControlPoints[ControlPointIndex], ControlPointIndex);
        }
        
        if (!Mesh->GenerateTangentsDataForAllUVSets(true))
        {
            HTR_LOG(Warning, "[FBX Export] Failed to regenerate tangents: {}", MeshNode->GetName());
        }

        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (!Cluster)
                {
                    continue;
                }

                const auto BoneIndexIterator = BoneIndexByNode.find(Cluster->GetLink());
                if (BoneIndexIterator == BoneIndexByNode.end())
                {
                    continue;
                }

                Cluster->SetTransformLinkMatrix(NewFbxGlobalMatrices[BoneIndexIterator->second]);
            }
        }
    }

    Scene->SetCurrentAnimationStack(nullptr);
    while (Scene->GetSrcObjectCount<FbxAnimStack>() > 0)
    {
        FbxAnimStack* AnimationStack = Scene->GetSrcObject<FbxAnimStack>(0);
        if (!AnimationStack)
        {
            break;
        }
        AnimationStack->Destroy(true);
    }
    AnimationEvaluator->Reset();

    for (int32 PoseIndex = Scene->GetPoseCount() - 1; PoseIndex >= 0; --PoseIndex)
    {
        FbxPose* Pose = Scene->GetPose(PoseIndex);
        if (!Pose || !Pose->IsBindPose())
        {
            continue;
        }
        Scene->RemovePose(PoseIndex);
        Pose->Destroy();
    }

    Manager->CreateMissingBindPoses(Scene);
    bool bFoundValidBindPose = false;
    for (int32 PoseIndex = 0; PoseIndex < Scene->GetPoseCount(); ++PoseIndex)
    {
        FbxPose* Pose = Scene->GetPose(PoseIndex);
        if (!Pose || !Pose->IsBindPose())
        {
            continue;
        }

        const int32 OriginalEntryCount = Pose->GetCount();
        for (int32 EntryIndex = 0; EntryIndex < OriginalEntryCount; ++EntryIndex)
        {
            FbxNode* EntryNode = Pose->GetNode(EntryIndex);
            for (FbxNode* Parent = EntryNode ? EntryNode->GetParent() : nullptr;
                Parent; Parent = Parent->GetParent())
            {
                if (Pose->Find(Parent) < 0)
                {
                    const auto BoneIt = BoneIndexByNode.find(Parent);
                    const FbxAMatrix Global = BoneIt != BoneIndexByNode.end()
                        ? NewFbxGlobalMatrices[BoneIt->second]
                        : Parent->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE);

                    if (Pose->Add(Parent, FbxMatrix(Global), false) < 0)
                    {
                        return Fail(FString("Failed to add bind pose ancestor: ") + Parent->GetName());
                    }
                }
                if (Parent == SceneRoot)
                {
                    break;
                }
            }
        }

        FbxStatus BindPoseStatus;
        if (!Pose->IsValidBindPose(SceneRoot, 0.0001, &BindPoseStatus))
        {
            NodeList MissingAncestors;
            NodeList MissingDeformers;
            NodeList MissingDeformerAncestors;
            NodeList WrongMatrices;

            Pose->IsValidBindPoseVerbose(SceneRoot, MissingAncestors, MissingDeformers,
                MissingDeformerAncestors, WrongMatrices, 0.0001, &BindPoseStatus);
            const auto LogNodes = [](const char* Category, const NodeList& Nodes)
            {
                for (int32 Index = 0; Index < Nodes.GetCount(); ++Index)
                    HTR_LOG(Error, "[FBX Export] Bind pose {}: {}", Category, Nodes[Index]->GetName());
            };

            LogNodes("missing ancestor", MissingAncestors);
            LogNodes("missing deformer", MissingDeformers);
            LogNodes("missing deformer ancestor", MissingDeformerAncestors);
            LogNodes("wrong matrix", WrongMatrices);
            return Fail(FString("Generated bind pose is invalid: ") + BindPoseStatus.GetErrorString());
        }
        bFoundValidBindPose = true;
    }
    if (!bFoundValidBindPose)
    {
        return Fail("No new bind pose was generated.");
    }

    PathError.clear();
    const std::filesystem::path DestinationDirectory = AbsoluteDestinationPath.parent_path();
    if (!DestinationDirectory.empty())
    {
        std::filesystem::create_directories(DestinationDirectory, PathError);
        if (PathError)
        {
            return Fail(FString("Failed to create destination directory: ") + PathError.message());
        }
    }

    FbxExporter* Exporter = FbxExporter::Create(Manager, "ReboundExporter");
    if (!Exporter)
    {
        return Fail("Failed to create FbxExporter.");
    }
    if (!Exporter->Initialize(AbsoluteDestinationPath.string().c_str(), -1, Manager->GetIOSettings()))
    {
        Exporter->Destroy();
        return Fail("Failed to initialize exporter");
    }

    const bool bExported = Exporter->Export(Scene);
    if (!bExported)
    {
        HTR_LOG(Error, "[FBX Export] Failed to write FBX: {}", Exporter->GetStatus().GetErrorString());
    }
    Exporter->Destroy();
    Manager->Destroy();
    return bExported;
}
