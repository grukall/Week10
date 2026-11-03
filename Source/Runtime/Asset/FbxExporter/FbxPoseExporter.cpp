#include "EnginePCH.h"
#include "FbxPoseExporter.h"

#include "Core/EngineLog.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/FbxImporter/FbxImporter.h"

#include <fbxsdk.h>
#include <fbxsdk/utils/fbxdeformationsevaluator.h>
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
        HTR_LOG(Error, "[FBX Export] Failed to create FbxManager.");
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
        const FString ErrorMessage = FString("Failed to import source FBX: ") + Importer->GetStatus().GetErrorString();
        Importer->Destroy();
        return Fail(ErrorMessage);
    }
    Importer->Destroy();
    FbxNode* SceneRoot = Scene->GetRootNode();
    if (!SceneRoot)
    {
        return Fail("Source FBX has no scene root.");
    }

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
        if (!BoneNode)
        {
            return Fail(FString("Bone node was not found in source FBX: ") + Bone.SourceNodePath);
        }
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

    // 기존 애니메이션 삭제 블록이 있던 위치에 추가
    FbxAnimStack* EvaluationStack = FbxAnimStack::Create(Scene, "RebindEvaluationStack");
    FbxAnimLayer* EvaluationLayer = FbxAnimLayer::Create(Scene, "RebindEvaluationLayer");
    if (!EvaluationStack || !EvaluationLayer)
    {
        return Fail("Failed to create temporary animation evaluation context.");
    }
    EvaluationStack->AddMember(EvaluationLayer);
    Scene->SetCurrentAnimationStack(EvaluationStack);

    FbxAnimEvaluator* AnimationEvaluator = Scene->GetAnimationEvaluator();
    if (!AnimationEvaluator)
    {
        return Fail("Scene has no animation evaluator.");
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
    {
        FbxVector4 Translation;
        FbxVector4 Rotation;
        FbxVector4 Scale;
        FbxAMatrix TargetGlobalMatrix = NewFbxGlobalMatrices[BoneIndex];

        AnimationEvaluator->ComputeLocalTRSFromGlobal(Translation, Rotation, Scale, BoneNodes[BoneIndex], TargetGlobalMatrix, FBXSDK_TIME_INFINITE, FbxNode::eSourcePivot, false, true);
        BoneNodes[BoneIndex]->LclTranslation.Set(FbxDouble3(Translation[0], Translation[1], Translation[2]));
        BoneNodes[BoneIndex]->LclRotation.Set(FbxDouble3(Rotation[0], Rotation[1], Rotation[2]));
        BoneNodes[BoneIndex]->LclScaling.Set(FbxDouble3(Scale[0], Scale[1], Scale[2]));
        AnimationEvaluator->Reset();
    }
    for (int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
    {
        NewFbxGlobalMatrices[BoneIndex] = BoneNodes[BoneIndex]->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE);
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
            return Fail(FString("Mesh has no control points: ") + MeshNode->GetName());
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

        FbxDeformationsEvaluator DeformationEvaluator;
        if (!DeformationEvaluator.Init(MeshNode, Mesh))
        {
            return Fail(FString("Failed to initialize mesh deformation: ") + MeshNode->GetName());
        }

        FbxAMatrix GeometryMatrix;
        GeometryMatrix.SetT(MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot));
        GeometryMatrix.SetR(MeshNode->GetGeometricRotation(FbxNode::eSourcePivot));
        GeometryMatrix.SetS(MeshNode->GetGeometricScaling(FbxNode::eSourcePivot));
        FbxAMatrix MeshEvaluationGlobal = MeshNode->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE) * GeometryMatrix;

        FbxGeometryConverter GeometryConverter(Manager);
        GeometryConverter.ComputeEdgeSmoothingFromNormals(Mesh);

        if (!DeformationEvaluator.ComputeSkinDeformation(BakedControlPoints.data(), FBXSDK_TIME_INFINITE, &MeshEvaluationGlobal, nullptr))
        {
            return Fail(FString("Failed to bake mesh control points: ") + MeshNode->GetName());
        }

        for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointCount; ++ControlPointIndex)
        {
            Mesh->SetControlPointAt(BakedControlPoints[ControlPointIndex], ControlPointIndex);
        }

        //Mesh->GenerateNormals(true, false);
        
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

    // TargetMeshNodes 처리 루프가 완전히 끝난 다음, 기존 bind pose 삭제 코드 전에 추가
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
        FbxStatus BindPoseStatus;
        if (!Pose->IsValidBindPose(SceneRoot, 0.0001, &BindPoseStatus))
        {
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
        const FString ErrorMessage = FString("Failed to initialize exporter: ") + Exporter->GetStatus().GetErrorString();
        Exporter->Destroy();
        return Fail(ErrorMessage);
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
