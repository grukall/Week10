#include "EnginePCH.h"
#include "FbxImporter.h"

#include "SkeletalMeshImportData.h"
#include "Core/EngineLog.h"

#include <fbxsdk.h>
#include <unordered_map>
#include <cmath>
#include <filesystem>
#include <limits>

namespace
{
    // FBX 본 노드의 포인터를 그룹 내부의 RefBones 배열 번호로 바꾼다.
    using FBoneIndexMap = std::unordered_map<const FbxNode*, int32>;

    // 같은 스켈레톤 루트에 연결된 원본 메시 노드들을 묶는다.
    struct FFbxSkeletalMeshGroup
    {
        FbxNode* RootNode = nullptr;
        TArray<FbxNode*> MeshNodes;
    };

    // 앞서 검증한 행렬 배치를 유지하며 double 값을 엔진의 float로 복사한다.
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

    // 데이터를 추출하기 전에 장면의 좌표축과 길이 단위를 통일한다.
    void NormalizeScene(FbxScene* Scene)
    {
        // 엔진: +X 앞, +Y 오른쪽, +Z 위, 왼손 좌표계.
        // FBX의 Front는 화면 밖을 향하므로 엔진 앞 방향과 부호가 반대다.
        const FbxAxisSystem EngineAxis(
            FbxAxisSystem::eZAxis,
            static_cast<FbxAxisSystem::EFrontVector>(-FbxAxisSystem::eParityEven),
            FbxAxisSystem::eLeftHanded
        );

        const bool bAxisChanged = Scene->GetGlobalSettings().GetAxisSystem() != EngineAxis;
        const double SourceUnitCm = Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor();

        if (bAxisChanged)
            EngineAxis.DeepConvertScene(Scene);

        // 1유닛 = 1m
        FbxSystemUnit::m.ConvertScene(Scene);

        HTR_LOG(Info, "[FBX Coordinate] AxisChanged={}, UnitCm={} -> {}", bAxisChanged, SourceUnitCm, Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor());
    }

    // Cluster의 본에서 부모로 올라가 메시 그룹을 식별할 루트를 찾는다.
    FbxNode* FindSkeletonRoot(FbxNode* Link, FbxNode* SceneRoot)
    {
        FbxNode* Root = Link;

        while (Root && Root->GetParent())
        {
            FbxNode* Parent = Root->GetParent();
            FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();

            if (Parent == SceneRoot || !Attribute)
                break;

            const auto Type = Attribute->GetAttributeType();
            if (Type != FbxNodeAttribute::eMesh && Type != FbxNodeAttribute::eNull && Type != FbxNodeAttribute::eSkeleton)
                break;

            if (Type == FbxNodeAttribute::eMesh && static_cast<FbxMesh*>(Attribute)->GetDeformerCount(FbxDeformer::eSkin) > 0)
                break;

            Root = Parent;
        }

        return Root;
    }

    // Scene의 가상 루트는 제외하고 원본 노드의 계층 경로를 만든다.
    // 예: Armature/Hips/Spine
    FString BuildSourceNodePath(FbxNode* Node)
    {
        FString NodePath;

        while (Node && Node->GetParent())
        {
            if (NodePath.empty())
                NodePath = Node->GetName();
            else
                NodePath = FString(Node->GetName()) + "/" + NodePath;

            Node = Node->GetParent();
        }

        return NodePath;
    }

    // 부모부터 DFS로 수집하므로 부모 본의 배열 번호가 자식보다 앞선다.
    void CollectBones(FbxNode* Node, FSkeletalMeshImportData& OutData, FBoneIndexMap& BoneMap)
    {
        if (!Node)
            return;

        if (Node->GetSkeleton())
        {
            FImportedRefBone Bone;
            Bone.Name = Node->GetName();
            Bone.SourceNodePath = BuildSourceNodePath(Node);
            Bone.ParentIndex = -1;

            FbxNode* ParentNode = Node->GetParent();

            // 중간에 일반 노드가 있어도 가장 가까운 수집된 부모 본을 찾는다.
            while (ParentNode)
            {
                const auto ParentIt = BoneMap.find(ParentNode);

                if (ParentIt != BoneMap.end())
                {
                    Bone.ParentIndex = ParentIt->second;
                    break;
                }

                ParentNode = ParentNode->GetParent();
            }

            const int32 BoneIndex = OutData.Skeleton.RefBones.Num();

            OutData.Skeleton.RefBones.Add(Bone);
            BoneMap[Node] = BoneIndex;
        }

        // 현재 Node가 본이 아니어도 자식에는 본이 있을 수 있다.
        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
            CollectBones(Node->GetChild(ChildIndex), OutData, BoneMap);
    }

    // BoneMap으로 변환할 수 없는 Cluster를 무시하면 가중치가 조용히 사라진다.
    // 메시 추출 전에 원본 참조와 점 번호를 확인한다.
    bool ValidateClusterReferences(const FFbxSkeletalMeshGroup& Group, const FBoneIndexMap& BoneMap)
    {
        for (FbxNode* MeshNode : Group.MeshNodes)
        {
            FbxMesh* Mesh = MeshNode->GetMesh();
            if (!Mesh)
            {
                HTR_LOG(Error, "[FBX Validate] Group mesh node has no mesh: Node={}", MeshNode->GetName());
                return false;
            }

            for (int32 SkinIndex = 0; SkinIndex < Mesh->GetDeformerCount(FbxDeformer::eSkin); ++SkinIndex)
            {
                FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
                if (!Skin)
                {
                    HTR_LOG(Error, "[FBX Validate] Missing skin: Mesh={}, Skin={}", MeshNode->GetName(), SkinIndex);
                    return false;
                }

                for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
                {
                    FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                    if (!Cluster)
                    {
                        HTR_LOG(Error, "[FBX Validate] Missing cluster: Mesh={}, Cluster={}", MeshNode->GetName(), ClusterIndex);
                        return false;
                    }

                    const int32 Count = Cluster->GetControlPointIndicesCount();
                    if (Count < 0)
                        return false;

                    if (Count == 0)
                        continue;

                    const int* PointIndices = Cluster->GetControlPointIndices();
                    const double* Weights = Cluster->GetControlPointWeights();
                    if (!PointIndices || !Weights)
                    {
                        HTR_LOG(Error, "[FBX Validate] Missing influence array: Mesh={}, Cluster={}", MeshNode->GetName(), ClusterIndex);
                        return false;
                    }

                    bool bHasPositiveWeight = false;
                    for (int32 InfluenceIndex = 0; InfluenceIndex < Count; ++InfluenceIndex)
                    {
                        const double Weight = Weights[InfluenceIndex];
                        if (PointIndices[InfluenceIndex] < 0 || PointIndices[InfluenceIndex] >= Mesh->GetControlPointsCount() ||
                            !std::isfinite(Weight) || Weight < 0.0 || Weight > std::numeric_limits<float>::max())
                        {
                            HTR_LOG(Error, "[FBX Validate] Invalid source influence: Mesh={}, Cluster={}, Entry={}", MeshNode->GetName(), ClusterIndex, InfluenceIndex);
                            return false;
                        }
                        bHasPositiveWeight |= Weight > 0.0;
                    }

                    // 가중치가 전혀 없는 Cluster는 변형에 참여하지 않는다.
                    if (bHasPositiveWeight && BoneMap.find(Cluster->GetLink()) == BoneMap.end())
                    {
                        HTR_LOG(Error, "[FBX Validate] Weighted cluster refers to an uncollected bone: Mesh={}, Cluster={}, Link={}",
                            MeshNode->GetName(), ClusterIndex, Cluster->GetLink() ? Cluster->GetLink()->GetName() : "None");
                        return false;
                    }
                }
            }
        }

        return true;
    }

    // 재질 속성에 직접 연결된 파일 텍스처의 경로를 읽는다. 이미지 로딩은 AssetManager에서 한다.
    FString ExtractTexturePath(FbxSurfaceMaterial* SourceMaterial, const char* PropertyName)
    {
        FbxProperty Property = SourceMaterial->FindProperty(PropertyName);
        if (!Property.IsValid())
            return {};

        const int32 TextureCount = Property.GetSrcObjectCount<FbxFileTexture>();

        for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
        {
            FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(TextureIndex);
            if (!Texture)
                continue;

            const char* RelativePath = Texture->GetRelativeFileName();
            if (RelativePath && RelativePath[0] != '\0')
                return FString(RelativePath);

            const char* AbsolutePath = Texture->GetFileName();
            if (AbsolutePath && AbsolutePath[0] != '\0')
                return FString(AbsolutePath);
        }

        return {};
    }

    // Lambert/Phong 재질의 기본 속성과 텍스처 경로를 엔진 자료형에 복사한다.
    void ExtractMaterialProperties(FbxSurfaceMaterial* SourceMaterial, FImportedMaterial& OutMaterial)
    {
        if (!SourceMaterial)
            return;

        OutMaterial.Name = SourceMaterial->GetName();

        if (FbxSurfaceLambert* Lambert = FbxCast<FbxSurfaceLambert>(SourceMaterial))
        {
            const FbxDouble3 Diffuse = Lambert->Diffuse.Get();
            const double DiffuseFactor = Lambert->DiffuseFactor.Get();

            OutMaterial.BaseColor = FLinearColor(static_cast<float>(Diffuse[0] * DiffuseFactor), static_cast<float>(Diffuse[1] * DiffuseFactor), static_cast<float>(Diffuse[2] * DiffuseFactor), 1.0f);
            OutMaterial.SpecularColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }

        if (FbxSurfacePhong* Phong = FbxCast<FbxSurfacePhong>(SourceMaterial))
        {
            const FbxDouble3 Specular = Phong->Specular.Get();
            const double SpecularFactor = Phong->SpecularFactor.Get();

            OutMaterial.SpecularColor = FLinearColor(static_cast<float>(Specular[0] * SpecularFactor), static_cast<float>(Specular[1] * SpecularFactor), static_cast<float>(Specular[2] * SpecularFactor), 1.0f);
            OutMaterial.Shininess = static_cast<float>(Phong->Shininess.Get());
        }

        OutMaterial.DiffuseTexturePath = ExtractTexturePath(SourceMaterial, FbxSurfaceMaterial::sDiffuse);
        OutMaterial.NormalTexturePath = ExtractTexturePath(SourceMaterial, FbxSurfaceMaterial::sNormalMap);

        HTR_LOG(Info, "[FBX Texture] Material={}, Diffuse={}, Normal={}", OutMaterial.Name, OutMaterial.DiffuseTexturePath, OutMaterial.NormalTexturePath);
        HTR_LOG(Info, "[FBX MaterialData] Name={}, BaseColor=({}, {}, {}), Shininess={}", OutMaterial.Name, OutMaterial.BaseColor.R, OutMaterial.BaseColor.G, OutMaterial.BaseColor.B, OutMaterial.Shininess);
    }

    // 탄젠트와 Binormal의 저장 방식을 해석해 현재 꼭짓점에 해당하는 벡터를 읽는다.
    bool ReadFbxVectorElement(const FbxLayerElementTemplate<FbxVector4>* Element, int32 ControlPointIndex, int32 PolygonIndex, int32 PolygonVertexIndex, FbxVector4& OutValue)
    {
        if (!Element)
            return false;

        int32 Index = -1;

        // 이 값이 원본 점별인지, 면의 꼭짓점별인지 확인한다.
        switch (Element->GetMappingMode())
        {
        case FbxLayerElement::eByControlPoint:
            Index = ControlPointIndex;
            break;

        case FbxLayerElement::eByPolygonVertex:
            Index = PolygonVertexIndex;
            break;

        case FbxLayerElement::eByPolygon:
            Index = PolygonIndex;
            break;

        case FbxLayerElement::eAllSame:
            Index = 0;
            break;

        default:
            return false;
        }

        if (Index < 0)
            return false;

        // 인덱스 배열을 사용하는 경우 실제 데이터 번호로 바꾼다.
        if (Element->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
        {
            if (Index >= Element->GetIndexArray().GetCount())
                return false;

            Index = Element->GetIndexArray().GetAt(Index);
        }
        else if (Element->GetReferenceMode() != FbxLayerElement::eDirect)
        {
            return false;
        }

        if (Index < 0 || Index >= Element->GetDirectArray().GetCount())
            return false;

        OutValue = Element->GetDirectArray().GetAt(Index);

        return std::isfinite(OutValue[0]) && std::isfinite(OutValue[1]) && std::isfinite(OutValue[2]);
    }

    // 메시 하나의 형상, 면별 속성, 본 가중치를 ImportData에 추가한다.
    void ExtractMesh(FbxNode* Node, FbxMesh* Mesh, const FBoneIndexMap& BoneMap, FSkeletalMeshImportData& OutData)
    {
        FImportedMesh ImportedMesh;
        ImportedMesh.Name = Node->GetName();

        // 1. 메시 로컬의 점 위치. 공통 기준 공간으로 옮기는 작업은 최종 데이터 변환에서 한다.
        const int32 PointCount = Mesh->GetControlPointsCount();
        const FbxVector4* ControlPoints = Mesh->GetControlPoints();

        for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
        {
            const FbxVector4& Point = ControlPoints[PointIndex];

            FVector Position(
                static_cast<float>(Point[0]),
                static_cast<float>(Point[1]),
                static_cast<float>(Point[2])
            );

            ImportedMesh.Points.Add(Position);
        }

        // 2. Node의 재질 번호 → 그룹 전체 Materials 번호
        TArray<int32> MaterialMap;

        for (int32 MaterialIndex = 0; MaterialIndex < Node->GetMaterialCount(); ++MaterialIndex)
        {
            FbxSurfaceMaterial* SourceMaterial = Node->GetMaterial(MaterialIndex);

            if (!SourceMaterial)
            {
                MaterialMap.Add(-1);
                continue;
            }

            FImportedMaterial Material;
            ExtractMaterialProperties(SourceMaterial, Material);

            MaterialMap.Add(OutData.Materials.Num());
            OutData.Materials.Add(Material);
        }

        FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);
        int32 MaterialPolygonCount = 0;

        // 3. 첫 번째 UV 세트를 UV0로 사용
        FbxStringList UVSetNames;
        Mesh->GetUVSetNames(UVSetNames);

        const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;
        int32 UVWedgeCount = 0;
        int32 NormalWedgeCount = 0;

        HTR_LOG(Info, "[FBX TangentSource] Mesh={}, TangentSets={}, BinormalSets={}", ImportedMesh.Name, Mesh->GetElementTangentCount(), Mesh->GetElementBinormalCount());

        const FbxGeometryElementTangent* TangentElement = Mesh->GetElementTangent(0);
        const FbxGeometryElementBinormal* BinormalElement = Mesh->GetElementBinormal(0);
        int32 TangentWedgeCount = 0;

        const FbxGeometryElementVertexColor* ColorElement = Mesh->GetElementVertexColor(0);
        int32 ColorWedgeCount = 0;

        // 4. 면별 재질과 꼭짓점 데이터
        for (int32 PolygonIndex = 0; PolygonIndex < Mesh->GetPolygonCount(); ++PolygonIndex)
        {
            FImportedPolygon Polygon;

            int32 LocalMaterialIndex = -1;

            if (MaterialElement)
            {
                int32 MappingIndex = -1;

                switch (MaterialElement->GetMappingMode())
                {
                case FbxLayerElement::eAllSame:
                    MappingIndex = 0;
                    break;

                case FbxLayerElement::eByPolygon:
                    MappingIndex = PolygonIndex;
                    break;

                default:
                    break;
                }

                const auto& MaterialIndices = MaterialElement->GetIndexArray();

                if (MappingIndex >= 0 && MappingIndex < MaterialIndices.GetCount())
                    LocalMaterialIndex = MaterialIndices.GetAt(MappingIndex);
            }

            if (LocalMaterialIndex >= 0 && LocalMaterialIndex < MaterialMap.Num())
            {
                Polygon.MaterialSlotIndex = MaterialMap[LocalMaterialIndex];

                if (Polygon.MaterialSlotIndex >= 0)
                    ++MaterialPolygonCount;
            }

            const int32 VertexCount = Mesh->GetPolygonSize(PolygonIndex);

            for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                FImportedWedge Wedge;
                Wedge.ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexIndex);

                // 같은 점을 공유하더라도 면마다 UV가 다를 수 있으므로 Wedge에 저장한다.
                if (UVSetName)
                {
                    FbxVector2 UV;
                    bool bUnmapped = false;

                    if (Mesh->GetPolygonVertexUV(PolygonIndex, VertexIndex, UVSetName, UV, bUnmapped) && !bUnmapped)
                    {
                        Wedge.UVs.Add(FVector2(static_cast<float>(UV[0]), static_cast<float>(UV[1])));
                        ++UVWedgeCount;
                    }
                }

                // 노멀은 UV 유무와 관계없이 읽는다.
                FbxVector4 Normal;

                if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexIndex, Normal))
                {
                    Wedge.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));
                    ++NormalWedgeCount;
                }

                // 전체 면의 꼭짓점을 한 줄로 나열했을 때 현재 꼭짓점의 번호다.
                const int32 PolygonVertexIndex = Mesh->GetPolygonVertexIndex(PolygonIndex) + VertexIndex;

                // 원본 탄젠트만 읽는다. 없는 경우의 생성 처리는 아직 구현하지 않았다.
                FbxVector4 SourceTangent;

                if (Wedge.UVs.Num() > 0 && ReadFbxVectorElement(TangentElement, Wedge.ControlPointIndex, PolygonIndex, PolygonVertexIndex, SourceTangent))
                {
                    const FVector Normal = Wedge.Normal.GetSafeNormal();
                    FVector Tangent(static_cast<float>(SourceTangent[0]), static_cast<float>(SourceTangent[1]), static_cast<float>(SourceTangent[2]));

                    // 노멀과 수직이 되도록 정리하고 길이를 1로 맞춘다.
                    Tangent = (Tangent - Normal * FVector::Dot(Normal, Tangent)).GetSafeNormal();

                    if (Normal.SizeSquared() > 0.0f && Tangent.SizeSquared() > 0.0f)
                    {
                        float Sign = 0.0f;

                        // FBX 탄젠트 W에 유효한 방향 부호가 있으면 사용한다.
                        if (std::fabs(std::fabs(SourceTangent[3]) - 1.0) < 0.0001)
                            Sign = SourceTangent[3] < 0.0 ? -1.0f : 1.0f;

                        // Binormal도 있으면 실제 방향으로 부호를 확인한다.
                        FbxVector4 SourceBinormal;

                        if (ReadFbxVectorElement(BinormalElement, Wedge.ControlPointIndex, PolygonIndex, PolygonVertexIndex, SourceBinormal))
                        {
                            const FVector Binormal(static_cast<float>(SourceBinormal[0]), static_cast<float>(SourceBinormal[1]), static_cast<float>(SourceBinormal[2]));
                            const float Orientation = FVector::Dot(FVector::Cross(Normal, Tangent), Binormal);

                            if (std::isfinite(Orientation) && std::fabs(Orientation) > 0.000001f)
                                Sign = Orientation < 0.0f ? -1.0f : 1.0f;
                        }

                        if (Sign != 0.0f)
                        {
                            Wedge.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Sign);
                            ++TangentWedgeCount;
                        }
                    }
                }

                // 색상을 읽지 못한 꼭짓점은 기본 흰색을 유지한다.
                Wedge.Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

                if (ColorElement)
                {
                    int32 ColorIndex = -1;

                    // 점별/면의 꼭짓점별/면별/전체 공통 중 어떤 단위로 저장됐는지 확인한다.
                    switch (ColorElement->GetMappingMode())
                    {
                    case FbxLayerElement::eByControlPoint:
                        ColorIndex = Wedge.ControlPointIndex;
                        break;

                    case FbxLayerElement::eByPolygonVertex:
                        ColorIndex = PolygonVertexIndex;
                        break;

                    case FbxLayerElement::eByPolygon:
                        ColorIndex = PolygonIndex;
                        break;

                    case FbxLayerElement::eAllSame:
                        ColorIndex = 0;
                        break;

                    default:
                        break;
                    }

                    // 간접 참조 방식이면 인덱스 배열을 거쳐 실제 RGBA 위치를 구한다.
                    if (ColorElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
                    {
                        const auto& ColorIndices = ColorElement->GetIndexArray();

                        ColorIndex = ColorIndex >= 0 && ColorIndex < ColorIndices.GetCount()
                            ? ColorIndices.GetAt(ColorIndex)
                            : -1;
                    }
                    else if (ColorElement->GetReferenceMode() != FbxLayerElement::eDirect)
                    {
                        ColorIndex = -1;
                    }

                    const auto& Colors = ColorElement->GetDirectArray();

                    if (ColorIndex >= 0 && ColorIndex < Colors.GetCount())
                    {
                        const FbxColor SourceColor = Colors.GetAt(ColorIndex);

                        if (SourceColor.IsValid())
                        {
                            Wedge.Color = FLinearColor(static_cast<float>(SourceColor.mRed), static_cast<float>(SourceColor.mGreen), static_cast<float>(SourceColor.mBlue), static_cast<float>(SourceColor.mAlpha));
                            ++ColorWedgeCount;
                        }
                    }
                }

                // 면은 Points 번호가 아니라 Wedges 번호를 통해 각 꼭짓점 속성을 참조한다.
                const int32 WedgeIndex = ImportedMesh.Wedges.Num();

                ImportedMesh.Wedges.Add(Wedge);
                Polygon.WedgeIndices.Add(WedgeIndex);
            }

            ImportedMesh.Polygons.Add(Polygon);
        }

        // 노멀/탄젠트는 전체 확보 여부, 색상은 하나라도 읽었는지를 기록한다.
        ImportedMesh.bHasNormals = ImportedMesh.Wedges.Num() > 0 && NormalWedgeCount == ImportedMesh.Wedges.Num();
        ImportedMesh.bHasTangents = ImportedMesh.Wedges.Num() > 0 && TangentWedgeCount == ImportedMesh.Wedges.Num();
        ImportedMesh.bHasVertexColors = ColorWedgeCount > 0;

        HTR_LOG(Info, "[FBX UV] Mesh={}, Set={}, Wedges={}, WithUV={}", ImportedMesh.Name, UVSetName ? UVSetName : "None", ImportedMesh.Wedges.Num(), UVWedgeCount);
        HTR_LOG(Info, "[FBX Normal] Mesh={}, Wedges={}, WithNormal={}", ImportedMesh.Name, ImportedMesh.Wedges.Num(), NormalWedgeCount);
        HTR_LOG(Info, "[FBX Tangent] Mesh={}, Wedges={}, WithTangent={}", ImportedMesh.Name, ImportedMesh.Wedges.Num(), TangentWedgeCount);
        HTR_LOG(Info, "[FBX Color] Mesh={}, Wedges={}, WithColor={}", ImportedMesh.Name, ImportedMesh.Wedges.Num(), ColorWedgeCount);
        HTR_LOG(Info, "[FBX Material] Mesh={}, Slots={}, Polygons={}, WithMaterial={}", ImportedMesh.Name, MaterialMap.Num(), ImportedMesh.Polygons.Num(), MaterialPolygonCount);

        // 5. 원본 점에 연결된 본 번호와 가중치
        const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

        for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));

            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);

                FbxNode* BoneNode = Cluster->GetLink();
                if (!BoneNode)
                    continue;

                const auto BoneIt = BoneMap.find(BoneNode);
                if (BoneIt == BoneMap.end())
                    continue;

                const int32 BoneIndex = BoneIt->second;
                const int32 InfluenceCount = Cluster->GetControlPointIndicesCount();
                const int* PointIndices = Cluster->GetControlPointIndices();
                const double* Weights = Cluster->GetControlPointWeights();

                for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
                {
                    FImportedInfluence Influence;
                    Influence.ControlPointIndex = PointIndices[InfluenceIndex];
                    Influence.BoneIndex = BoneIndex;
                    Influence.Weight = static_cast<float>(Weights[InfluenceIndex]);

                    ImportedMesh.Influences.Add(Influence);
                }
            }
        }

        OutData.Meshes.Add(ImportedMesh);
    }

    // Skin이 없어도 본 아래에 붙어 있는 메시(머리카락 등)는 함께 수집한다.
    void CollectRigidMeshes(FbxNode* Node, const FBoneIndexMap& BoneMap, FSkeletalMeshImportData& OutData)
    {
        if (!Node)
            return;

        FbxMesh* Mesh = Node->GetMesh();

        if (Mesh && Mesh->GetDeformerCount(FbxDeformer::eSkin) == 0)
        {
            FbxNode* ParentNode = Node->GetParent();

            while (ParentNode)
            {
                const auto BoneIt = BoneMap.find(ParentNode);

                if (BoneIt != BoneMap.end())
                {
                    const int32 MeshIndex = OutData.Meshes.Num();

                    ExtractMesh(Node, Mesh, BoneMap, OutData);

                    FImportedMesh& ImportedMesh = OutData.Meshes[MeshIndex];
                    ImportedMesh.AttachedBoneIndex = BoneIt->second;
                    ImportedMesh.SkinningType = EImportedSkinningType::Rigid;

                    // 기본 자세에서 부착 본과 메시 노드의 글로벌 변환
                    const FbxAMatrix BoneDefaultGlobal = ParentNode->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE);
                    const FbxAMatrix MeshDefaultGlobal = Node->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE);

                    // 메시 모양에만 적용되는 추가 변환
                    FbxAMatrix GeometryMatrix;
                    GeometryMatrix.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
                    GeometryMatrix.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
                    GeometryMatrix.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));

                    // 메시의 점 좌표 → 부착 본의 로컬 공간
                    const FbxAMatrix MeshToBone = BoneDefaultGlobal.Inverse() * MeshDefaultGlobal * GeometryMatrix;

                    // 부착 본의 기준 자세를 사용해 공통 공간으로 연결
                    const FMatrix& BoneReferenceGlobal = OutData.Skeleton.RefBones[ImportedMesh.AttachedBoneIndex].RefGlobalMatrix;
                    ImportedMesh.MeshToSkeletonBindMatrix = ConvertFbxMatrix(MeshToBone) * BoneReferenceGlobal;

                    HTR_LOG(Info, "[FBX Rigid] Mesh={}, AttachedBone={}", ImportedMesh.Name, ImportedMesh.AttachedBoneIndex);
                    break;
                }

                ParentNode = ParentNode->GetParent();
            }
        }

        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
            CollectRigidMeshes(Node->GetChild(ChildIndex), BoneMap, OutData);
    }

    // 여러 Cluster가 저장한 기준 행렬이 허용 오차 안에서 같은지 검사한다.
    bool AreMatricesNearlyEqual(const FMatrix& A, const FMatrix& B, float Tolerance = 0.0001f)
    {
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
            {
                const float Difference = std::fabs(A.M[Row][Column] - B.M[Row][Column]);
                if (!std::isfinite(Difference) || Difference > Tolerance)
                    return false;
            }
        }

        return true;
    }

    // 결합 당시의 본/메시 행렬을 확보한다. 일반적인 현재 자세와 구분해서 읽는다.
    bool ExtractBindPose(const FFbxSkeletalMeshGroup& Group, const FBoneIndexMap& BoneMap, FSkeletalMeshImportData& OutData)
    {
        // 0: 미확보, 1: 원본 바인드 정보, 2: 기본 자세로 보완
        TArray<uint8> BoneBindFound;

        for (int32 BoneIndex = 0; BoneIndex < OutData.Skeleton.RefBones.Num(); ++BoneIndex)
            BoneBindFound.Add(0);

        // 1. Cluster에서 메시와 본의 바인드 행렬 추출
        for (int32 MeshIndex = 0; MeshIndex < Group.MeshNodes.Num(); ++MeshIndex)
        {
            FbxNode* MeshNode = Group.MeshNodes[MeshIndex];
            FbxMesh* Mesh = MeshNode->GetMesh();
            // ParseFbx에서 같은 순서로 추가한 스킨 메시와 대응한다.
            FImportedMesh& ImportedMesh = OutData.Meshes[MeshIndex];

            FbxAMatrix GeometryMatrix;
            GeometryMatrix.SetT(MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot));
            GeometryMatrix.SetR(MeshNode->GetGeometricRotation(FbxNode::eSourcePivot));
            GeometryMatrix.SetS(MeshNode->GetGeometricScaling(FbxNode::eSourcePivot));

            bool bMeshBindFound = false;

            const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

            for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
            {
                FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
                const int32 ClusterCount = Skin->GetClusterCount();

                for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
                {
                    FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);

                    FbxNode* BoneNode = Cluster->GetLink();
                    if (!BoneNode)
                        continue;

                    const auto BoneIt = BoneMap.find(BoneNode);
                    if (BoneIt == BoneMap.end())
                        continue;

                    const int32 BoneIndex = BoneIt->second;

                    FbxAMatrix MeshBindMatrix;
                    FbxAMatrix BoneBindMatrix;

                    Cluster->GetTransformMatrix(MeshBindMatrix);
                    const FMatrix NewMeshBindMatrix = ConvertFbxMatrix(MeshBindMatrix * GeometryMatrix);

                    // 첫 행렬을 저장하고, 이후 Cluster들도 같은 기준을 사용하는지 확인한다.
                    if (!bMeshBindFound)
                    {
                        ImportedMesh.MeshToSkeletonBindMatrix = NewMeshBindMatrix;
                        bMeshBindFound = true;
                    }
                    else if (!AreMatricesNearlyEqual(ImportedMesh.MeshToSkeletonBindMatrix, NewMeshBindMatrix))
                    {
                        HTR_LOG(Error, "[FBX Bind] Conflicting mesh matrix: Mesh={}", ImportedMesh.Name);
                        return false;
                    }

                    Cluster->GetTransformLinkMatrix(BoneBindMatrix);

                    const FMatrix NewBindMatrix = ConvertFbxMatrix(BoneBindMatrix);
                    FImportedRefBone& Bone = OutData.Skeleton.RefBones[BoneIndex];

                    if (BoneBindFound[BoneIndex] == 0)
                    {
                        Bone.RefGlobalMatrix = NewBindMatrix;
                        BoneBindFound[BoneIndex] = 1;
                    }
                    else if (!AreMatricesNearlyEqual(Bone.RefGlobalMatrix, NewBindMatrix))
                    {
                        HTR_LOG(Error, "[FBX Bind] Conflicting bone matrix: Bone={}, Name={}, Mesh={}", BoneIndex, Bone.Name, MeshNode->GetName());
                        return false;
                    }
                }
            }
        }

        // 2. Cluster에서 못 찾은 본은 장면의 Bind Pose에서 조회
        FbxScene* Scene = Group.RootNode->GetScene();

        for (const auto& BoneEntry : BoneMap)
        {
            const int32 BoneIndex = BoneEntry.second;
            if (BoneBindFound[BoneIndex])
                continue;

            for (int32 PoseIndex = 0; PoseIndex < Scene->GetPoseCount(); ++PoseIndex)
            {
                FbxPose* Pose = Scene->GetPose(PoseIndex);
                if (!Pose->IsBindPose())
                    continue;

                const int32 EntryIndex = Pose->Find(BoneEntry.first);
                if (EntryIndex < 0)
                    continue;

                const FbxMatrix& Source = Pose->GetMatrix(EntryIndex);
                FImportedRefBone& Bone = OutData.Skeleton.RefBones[BoneIndex];

                for (int32 Row = 0; Row < 4; ++Row)
                {
                    for (int32 Column = 0; Column < 4; ++Column)
                        Bone.RefGlobalMatrix.M[Row][Column] = static_cast<float>(Source.Get(Row, Column));
                }

                BoneBindFound[BoneIndex] = 1;
                HTR_LOG(Info, "[FBX Bind] FromPose: Bone={}, Name={}", BoneIndex, Bone.Name);
                break;
            }
        }

        // 3. 남은 끝본은 기본 로컬 변환과 부모의 기준 행렬로 보완한다. 원본 Bind Pose를 복구한 것은 아니다.
        for (const auto& BoneEntry : BoneMap)
        {
            const int32 BoneIndex = BoneEntry.second;
            if (BoneBindFound[BoneIndex])
                continue;

            FbxNode* Node = const_cast<FbxNode*>(BoneEntry.first);
            FImportedRefBone& Bone = OutData.Skeleton.RefBones[BoneIndex];

            if (Node->GetChildCount() != 0 || Bone.ParentIndex < 0)
                continue;

            if (!BoneBindFound[Bone.ParentIndex])
                continue;

            const auto ParentIt = BoneMap.find(Node->GetParent());
            if (ParentIt == BoneMap.end() || ParentIt->second != Bone.ParentIndex)
                continue;

            const FbxAMatrix LocalMatrix = Node->EvaluateLocalTransform(FBXSDK_TIME_INFINITE);
            const FMatrix& ParentGlobal = OutData.Skeleton.RefBones[Bone.ParentIndex].RefGlobalMatrix;

            Bone.RefGlobalMatrix = ConvertFbxMatrix(LocalMatrix) * ParentGlobal;
            BoneBindFound[BoneIndex] = 2;

            HTR_LOG(Warning, "[FBX Bind] ReferenceFallback: Bone={}, Name={}", BoneIndex, Bone.Name);
        }

        // 4. 최종 미확보 본 확인
        int32 MissingCount = 0;

        for (int32 BoneIndex = 0; BoneIndex < BoneBindFound.Num(); ++BoneIndex)
        {
            if (BoneBindFound[BoneIndex] == 0)
            {
                ++MissingCount;
                HTR_LOG(Warning, "[FBX Bind] Missing: Bone={}, Name={}", BoneIndex, OutData.Skeleton.RefBones[BoneIndex].Name);
            }
        }

        HTR_LOG(Info, "[FBX Bind] Bones={}, Missing={}", BoneBindFound.Num(), MissingCount);
        return MissingCount == 0;
    }

    // 장면을 순회하며 스킨 메시를 찾고, 연결된 스켈레톤 루트별로 나눈다.
    void CollectSkeletalMeshGroups(FbxNode* Node, FbxNode* SceneRoot, TArray<FFbxSkeletalMeshGroup>& Groups)
    {
        if (!Node)
            return;

        FbxMesh* Mesh = Node->GetMesh();

        if (Mesh && Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));

            for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
            {
                FbxNode* BoneNode = Skin->GetCluster(ClusterIndex)->GetLink();
                FbxNode* SkeletonRoot = FindSkeletonRoot(BoneNode, SceneRoot);

                if (!SkeletonRoot)
                    continue;

                bool bAdded = false;

                for (FFbxSkeletalMeshGroup& Group : Groups)
                {
                    if (SkeletonRoot == Group.RootNode)
                    {
                        Group.MeshNodes.Add(Node);
                        bAdded = true;
                    }
                }

                if (!bAdded)
                {
                    FFbxSkeletalMeshGroup SkeletalMeshGroup;

                    SkeletalMeshGroup.RootNode = SkeletonRoot;
                    SkeletalMeshGroup.MeshNodes.Add(Node);

                    Groups.Add(SkeletalMeshGroup);
                }

                break;
            }
        }

        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
            CollectSkeletalMeshGroups(Node->GetChild(ChildIndex), SceneRoot, Groups);
    }

    // 필요할 때 장면의 노드 종류와 부모·자식 구조를 출력하는 진단용 함수다.
    void LogNodeHierarchy(FbxNode* Node, int32 Depth = 0)
    {
        if (!Node)
            return;

        FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
        const char* Type = "None";

        if (Node->GetMesh())
            Type = "Mesh";
        else if (Node->GetSkeleton())
            Type = "Bone";
        else if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eNull)
            Type = "Null";
        else if (Attribute)
            Type = "Other";

        const FString Indent(Depth * 2, ' ');
        HTR_LOG(Info, "[FBX Node] {}{} [{}] Children={}", Indent, Node->GetName(), Type, Node->GetChildCount());

        for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
            LogNodeHierarchy(Node->GetChild(ChildIndex), Depth + 1);
    }

    // 파일에 지정된 SkinningType을 읽는 함수다.
    bool ExtractSkinningTypes(const FFbxSkeletalMeshGroup& Group, FSkeletalMeshImportData& OutData)
    {
        for (int32 MeshIndex = 0; MeshIndex < Group.MeshNodes.Num(); ++MeshIndex)
        {
            FbxMesh* Mesh = Group.MeshNodes[MeshIndex]->GetMesh();
            FImportedMesh& ImportedMesh = OutData.Meshes[MeshIndex];

            const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

            for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
            {
                FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));

                EImportedSkinningType Type = EImportedSkinningType::None;
                const char* TypeName = "None";

                switch (Skin->GetSkinningType())
                {
                case FbxSkin::eRigid:
                    Type = EImportedSkinningType::Rigid;
                    TypeName = "Rigid";
                    break;

                case FbxSkin::eLinear:
                    Type = EImportedSkinningType::Linear;
                    TypeName = "Linear";
                    break;

                case FbxSkin::eDualQuaternion:
                    Type = EImportedSkinningType::DualQuaternion;
                    TypeName = "DualQuaternion";
                    break;

                case FbxSkin::eBlend:
                    Type = EImportedSkinningType::Blend;
                    TypeName = "Blend";
                    break;

                default:
                    HTR_LOG(Error, "[FBX Skin] Unknown type: Mesh={}", ImportedMesh.Name);
                    return false;
                }

                // 현재 구조체는 메시 하나당 방식 하나만 저장한다.
                // 서로 다른 방식이 섞여 있으면 덮어쓰지 않고 실패시킨다.
                if (SkinIndex > 0 && ImportedMesh.SkinningType != Type)
                {
                    HTR_LOG(Error, "[FBX Skin] Conflicting types: Mesh={}", ImportedMesh.Name);
                    return false;
                }

                ImportedMesh.SkinningType = Type;

                HTR_LOG(Info, "[FBX Skin] Mesh={}, Skin={}, Type={}", ImportedMesh.Name, SkinIndex, TypeName);
            }
        }

        return true;
    }

    bool ValidateLinkModes(const FFbxSkeletalMeshGroup& Group, const FSkeletalMeshImportData& ImportData)
    {
        for (int32 MeshIndex = 0; MeshIndex < Group.MeshNodes.Num(); ++MeshIndex)
        {
            FbxMesh* Mesh = Group.MeshNodes[MeshIndex]->GetMesh();
            const FImportedMesh& ImportedMesh = ImportData.Meshes[MeshIndex];

            int32 NormalizeCount = 0;
            int32 TotalOneCount = 0;

            const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

            for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
            {
                FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));

                for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
                {
                    FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                    if (!Cluster || !Cluster->GetLink())
                        continue;

                    switch (Cluster->GetLinkMode())
                    {
                    case FbxCluster::eNormalize:
                        ++NormalizeCount;
                        break;

                    case FbxCluster::eTotalOne:
                        ++TotalOneCount;
                        break;

                    case FbxCluster::eAdditive:
                        HTR_LOG(Error, "[FBX LinkMode] Additive is not supported: Mesh={}", ImportedMesh.Name);
                        return false;

                    default:
                        HTR_LOG(Error, "[FBX LinkMode] Unknown mode: Mesh={}", ImportedMesh.Name);
                        return false;
                    }
                }
            }

            // 같은 메시의 Cluster들은 같은 LinkMode를 사용해야 한다.
            if (NormalizeCount > 0 && TotalOneCount > 0)
            {
                HTR_LOG(Error, "[FBX LinkMode] Conflicting modes: Mesh={}", ImportedMesh.Name);
                return false;
            }

            if (TotalOneCount > 0)
            {
                TArray<double> WeightSums;
                TArray<uint8> HasInfluence;

                for (int32 PointIndex = 0; PointIndex < ImportedMesh.Points.Num(); ++PointIndex)
                {
                    WeightSums.Add(0.0);
                    HasInfluence.Add(0);
                }

                // 이미 추출한 가중치를 원본 점별로 합산한다.
                for (const FImportedInfluence& Influence : ImportedMesh.Influences)
                {
                    const int32 PointIndex = Influence.ControlPointIndex;

                    if (PointIndex < 0 || PointIndex >= WeightSums.Num() ||
                        !std::isfinite(Influence.Weight) || Influence.Weight < 0.0f)
                    {
                        HTR_LOG(Error, "[FBX LinkMode] Invalid influence: Mesh={}", ImportedMesh.Name);
                        return false;
                    }

                    WeightSums[PointIndex] += Influence.Weight;
                    HasInfluence[PointIndex] = 1;
                }

                // 가중치가 등록된 점은 합이 1이어야 현재 최종 데이터로 표현할 수 있다.
                for (int32 PointIndex = 0; PointIndex < WeightSums.Num(); ++PointIndex)
                {
                    if (HasInfluence[PointIndex] && std::fabs(WeightSums[PointIndex] - 1.0) > 0.0001)
                    {
                        HTR_LOG(Error, "[FBX LinkMode] Unsupported TotalOne weight sum: Mesh={}, Point={}, Sum={}", ImportedMesh.Name, PointIndex, WeightSums[PointIndex]);
                        return false;
                    }
                }
            }

            HTR_LOG(Info, "[FBX LinkMode] Mesh={}, Normalize={}, TotalOne={}", ImportedMesh.Name, NormalizeCount, TotalOneCount);
        }

        return true;
    }

    bool IsFiniteMatrix(const FMatrix& Matrix)
    {
        for (int32 Row = 0; Row < 4; ++Row)
            for (int32 Column = 0; Column < 4; ++Column)
                if (!std::isfinite(Matrix.M[Row][Column]))
                    return false;
        return true;
    }

    bool IsFiniteColor(const FLinearColor& Color)
    {
        return std::isfinite(Color.R) && std::isfinite(Color.G) &&
            std::isfinite(Color.B) && std::isfinite(Color.A);
    }

    // SDK 객체를 해제하기 전에 반환할 그룹의 인덱스와 수치값을 검사한다.
    bool ValidateImportData(const FSkeletalMeshImportData& Data)
    {
        const int32 BoneCount = Data.Skeleton.RefBones.Num();
        const int32 MaterialCount = Data.Materials.Num();

        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            const FImportedRefBone& Bone = Data.Skeleton.RefBones[BoneIndex];
            if (Bone.ParentIndex < -1 || Bone.ParentIndex >= BoneIndex || !IsFiniteMatrix(Bone.RefGlobalMatrix))
            {
                HTR_LOG(Error, "[FBX Validate] Invalid bone: Group={}, Bone={}, Name={}", Data.Name, BoneIndex, Bone.Name);
                return false;
            }
        }

        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            const FImportedMaterial& Material = Data.Materials[MaterialIndex];
            if (!IsFiniteColor(Material.BaseColor) || !IsFiniteColor(Material.SpecularColor) ||
                !std::isfinite(Material.Shininess))
            {
                HTR_LOG(Error, "[FBX Validate] Invalid material: Group={}, Material={}", Data.Name, MaterialIndex);
                return false;
            }
        }

        for (const FImportedMesh& Mesh : Data.Meshes)
        {
            const int32 PointCount = Mesh.Points.Num();
            const int32 WedgeCount = Mesh.Wedges.Num();
            if (!IsFiniteMatrix(Mesh.MeshToSkeletonBindMatrix) ||
                Mesh.AttachedBoneIndex < -1 || Mesh.AttachedBoneIndex >= BoneCount)
            {
                HTR_LOG(Error, "[FBX Validate] Invalid mesh transform or attachment: Mesh={}", Mesh.Name);
                return false;
            }

            for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
            {
                const FVector& Point = Mesh.Points[PointIndex];
                if (!std::isfinite(Point.X) || !std::isfinite(Point.Y) || !std::isfinite(Point.Z))
                {
                    HTR_LOG(Error, "[FBX Validate] Invalid point: Mesh={}, Point={}", Mesh.Name, PointIndex);
                    return false;
                }
            }

            for (int32 WedgeIndex = 0; WedgeIndex < WedgeCount; ++WedgeIndex)
            {
                const FImportedWedge& Wedge = Mesh.Wedges[WedgeIndex];
                if (Wedge.ControlPointIndex < 0 || Wedge.ControlPointIndex >= PointCount ||
                    !std::isfinite(Wedge.Normal.X) || !std::isfinite(Wedge.Normal.Y) || !std::isfinite(Wedge.Normal.Z) ||
                    !std::isfinite(Wedge.Tangent.X) || !std::isfinite(Wedge.Tangent.Y) ||
                    !std::isfinite(Wedge.Tangent.Z) || !std::isfinite(Wedge.Tangent.W) || !IsFiniteColor(Wedge.Color))
                {
                    HTR_LOG(Error, "[FBX Validate] Invalid wedge: Mesh={}, Wedge={}", Mesh.Name, WedgeIndex);
                    return false;
                }

                for (const FVector2& UV : Wedge.UVs)
                {
                    if (!std::isfinite(UV.X) || !std::isfinite(UV.Y))
                    {
                        HTR_LOG(Error, "[FBX Validate] Invalid UV: Mesh={}, Wedge={}", Mesh.Name, WedgeIndex);
                        return false;
                    }
                }
            }

            for (int32 PolygonIndex = 0; PolygonIndex < Mesh.Polygons.Num(); ++PolygonIndex)
            {
                const FImportedPolygon& Polygon = Mesh.Polygons[PolygonIndex];
                if (Polygon.WedgeIndices.Num() < 3 || Polygon.MaterialSlotIndex < -1 ||
                    Polygon.MaterialSlotIndex >= MaterialCount)
                {
                    HTR_LOG(Error, "[FBX Validate] Invalid polygon: Mesh={}, Polygon={}", Mesh.Name, PolygonIndex);
                    return false;
                }

                for (int32 WedgeIndex : Polygon.WedgeIndices)
                {
                    if (WedgeIndex < 0 || WedgeIndex >= WedgeCount)
                    {
                        HTR_LOG(Error, "[FBX Validate] Invalid polygon wedge index: Mesh={}, Polygon={}", Mesh.Name, PolygonIndex);
                        return false;
                    }
                }
            }

            for (const FImportedInfluence& Influence : Mesh.Influences)
            {
                if (Influence.ControlPointIndex < 0 || Influence.ControlPointIndex >= PointCount ||
                    Influence.BoneIndex < 0 || Influence.BoneIndex >= BoneCount ||
                    !std::isfinite(Influence.Weight) || Influence.Weight < 0.0f)
                {
                    HTR_LOG(Error, "[FBX Validate] Invalid influence: Mesh={}, Point={}, Bone={}",
                        Mesh.Name, Influence.ControlPointIndex, Influence.BoneIndex);
                    return false;
                }
            }
        }

        HTR_LOG(Info, "[FBX Validate] Group={}, Meshes={}, Bones={}, Materials={} OK",
            Data.Name, Data.Meshes.Num(), BoneCount, MaterialCount);
        return true;
    }

}

bool FFbxImporter::ResolveTexturePath(const FString& FbxPath, FString& TexturePath)
{
    if (TexturePath.empty())
        return true;

    FString NormalizedPath = TexturePath;

    for (char& Character : NormalizedPath)
    {
        if (Character == '\\')
            Character = '/';
    }

    std::filesystem::path ResolvedPath(NormalizedPath);

    if (!ResolvedPath.is_absolute())
        ResolvedPath = std::filesystem::path(FbxPath).parent_path() / ResolvedPath;

    ResolvedPath = ResolvedPath.lexically_normal();

    std::error_code Error;

    if (!std::filesystem::is_regular_file(ResolvedPath, Error))
    {
        HTR_LOG(Warning, "[FBX Texture] File not found: {}", std::filesystem::absolute(ResolvedPath).generic_string());
        return false;
    }

    TexturePath = ResolvedPath.generic_string();
    HTR_LOG(Info, "[FBX Texture] Resolved: {}", TexturePath);
    return true;
}

bool FFbxImporter::ParseFbx(const FString& Path, TArray<FSkeletalMeshImportData>& OutData)
{
    // SDK와 엔진의 행렬 변환 결과를 비교하는 기존 진단 코드.
    FbxAMatrix TestMatrix;
    TestMatrix.SetTRS(FbxVector4(10, 20, 30), FbxVector4(0, 0, 90), FbxVector4(2, 3, 4));

    const FbxVector4 FbxResult = TestMatrix.MultT(FbxVector4(1, 2, 3));
    const FVector EngineResult = ConvertFbxMatrix(TestMatrix).TransformPosition(FVector(1, 2, 3));

    HTR_LOG(Info, "[MatrixTest] FBX=({}, {}, {})", FbxResult[0], FbxResult[1], FbxResult[2]);
    HTR_LOG(Info, "[MatrixTest] Engine=({}, {}, {})", EngineResult.X, EngineResult.Y, EngineResult.Z);

    // FBX SDK 객체의 생성과 수명을 관리하는 최상위 객체
    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
        return false;

    // 파일을 읽을 때 사용할 입출력 설정을 Manager에 등록
    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    FbxImporter* Importer = FbxImporter::Create(Manager, "");

    // FBX 파일을 열고 형식을 감지한다. 실제 데이터 로딩은 Import()에서 수행한다.
    if (!Importer->Initialize(Path.c_str(), -1, Manager->GetIOSettings()))
    {
        HTR_LOG(Error, "[FBX] Initialize failed: {}", Importer->GetStatus().GetErrorString());
        Manager->Destroy();
        return false;
    }

    FbxScene* Scene = FbxScene::Create(Manager, "Scene");

    // 파일의 객체와 데이터를 Scene에 구성
    if (!Importer->Import(Scene))
    {
        HTR_LOG(Error, "[FBX] Import failed: {}", Importer->GetStatus().GetErrorString());
        Manager->Destroy();
        return false;
    }

    // Importer를 해제해도 Scene에 가져온 데이터는 유지된다.
    Importer->Destroy();

    // 엔진 기준으로 변환한 뒤 데이터를 추출한다.
    NormalizeScene(Scene);

    FbxNode* RootNode = Scene->GetRootNode();
    TArray<FFbxSkeletalMeshGroup> Groups;
    CollectSkeletalMeshGroups(RootNode, RootNode, Groups);

    TArray<FSkeletalMeshImportData> ImportedGroups;

    for (const FFbxSkeletalMeshGroup& Group : Groups)
    {
        FSkeletalMeshImportData ImportedData;

        // 그룹 표시 이름은 스켈레톤 루트 이름을 사용한다. 비어 있으면 파일 이름을 쓴다.
        ImportedData.Name = Group.RootNode->GetName();
        if (ImportedData.Name.empty())
            ImportedData.Name = std::filesystem::path(Path).stem().string();

        FBoneIndexMap BoneMap;

        // 가중치의 본 참조를 변환할 수 있도록 본 목록과 BoneMap을 먼저 만든다.
        CollectBones(Group.RootNode, ImportedData, BoneMap);

        if (!ValidateClusterReferences(Group, BoneMap))
        {
            Manager->Destroy();
            return false;
        }

        for (FbxNode* MeshNode : Group.MeshNodes)
            ExtractMesh(MeshNode, MeshNode->GetMesh(), BoneMap, ImportedData);

        if (!ExtractSkinningTypes(Group, ImportedData))
        {
            Manager->Destroy();
            return false;
        }

        if (!ValidateLinkModes(Group, ImportedData))
        {
            Manager->Destroy();
            return false;
        }

        if (!ExtractBindPose(Group, BoneMap, ImportedData))
        {
            Manager->Destroy();
            return false;
        }

        // 부착 메시의 행렬 계산에는 본의 기준 행렬이 필요하므로 Bind Pose 확보 후 처리한다.
        CollectRigidMeshes(Group.RootNode, BoneMap, ImportedData);

        for (FImportedMaterial& Material : ImportedData.Materials)
        {
            ResolveTexturePath(Path, Material.DiffuseTexturePath);
            ResolveTexturePath(Path, Material.NormalTexturePath);
        }

        HTR_LOG(Info, "[FBX Identity] Group={}, RootPath={}", ImportedData.Name, BuildSourceNodePath(Group.RootNode));

        if (!ValidateImportData(ImportedData))
        {
            Manager->Destroy();
            return false;
        }

        ImportedGroups.Add(std::move(ImportedData));
    }

    // SDK 객체가 사라져도 이름·점·행렬 등 복사된 엔진 데이터는 유지된다.
    OutData = std::move(ImportedGroups);

    // Manager가 소유한 Scene과 나머지 SDK 객체들을 함께 해제한다.
    Manager->Destroy();
    return true;
}