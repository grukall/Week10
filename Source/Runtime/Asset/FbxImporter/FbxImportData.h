#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Matrix.h"
#include "Math/Color.h"

// 원본의 스키닝 방식.
// 저장하는 것과 엔진에서 해당 방식을 지원하는 것은 별개다.
enum class EImportedSkinningType : uint8
{
    None,
    Rigid,
    Linear,
    DualQuaternion,
    Blend
};

// 하나의 본이 하나의 원본 점에 미치는 영향
struct FImportedInfluence
{
    int32 ControlPointIndex = -1;

    // 이 그룹의 Skeleton.RefBones 인덱스
    int32 BoneIndex = -1;

    float Weight = 0.0f;
};

// 기준 자세의 본
struct FImportedRefBone
{
    FString Name;
    FString SourceNodePath;

    // Skeleton.RefBones 안의 부모 본 인덱스
    int32 ParentIndex = -1;

    // 기준 자세에서 본 로컬 공간 → 공통 기준 공간
    // 현재 구현에서는 FBX 장면 공간을 사용한다.
    FMatrix RefGlobalMatrix = FMatrix::Identity;
};

// 본 계층과 기준 자세
struct FSkeletonImportData
{
    TArray<FImportedRefBone> RefBones;
};

// 면의 꼭짓점 하나.
// 같은 Point를 사용해도 면에 따라 UV와 Normal이 다를 수 있다.
struct FImportedWedge
{
    int32 ControlPointIndex = -1;

    FVector Normal = FVector::ZeroVector;

    // XYZ: 접선 방향, W: 접선 공간의 방향 부호
    FVector4 Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);

    FLinearColor Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

    // UV 채널별 좌표
    TArray<FVector2> UVs;
};

// 원본 다각형. 아직 삼각형화하지 않는다.
struct FImportedPolygon
{
    // 이 메시의 Wedges 인덱스
    TArray<int32> WedgeIndices;

    // 그룹 전체 Materials 인덱스. 없으면 -1
    int32 MaterialSlotIndex = -1;
};

// 실제 머티리얼 객체 생성 전의 원본 재질 정보
struct FImportedMaterial
{
    FString Name;

    FLinearColor BaseColor =
        FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

    FLinearColor SpecularColor =
        FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

    float Shininess = 32.0f;

    FString DiffuseTexturePath;
    FString NormalTexturePath;
};

// 원본 메시 하나
struct FImportedMesh
{
    FString Name;

    // 메시 로컬 공간의 원본 Control Point 위치
    TArray<FVector> Points;

    TArray<FImportedWedge> Wedges;
    TArray<FImportedPolygon> Polygons;
    TArray<FImportedInfluence> Influences;

    // 기준 자세에서 메시 로컬 공간 → 본과 동일한 공통 기준 공간
    // FBX의 Geometry Transform도 반영해 계산해야 한다.
    // 스태틱 메시에서는 노드의 장면 변환을 저장한다.
    FMatrix MeshToSkeletonBindMatrix = FMatrix::Identity;

    // 스킨 없이 본에 부착된 메시를 위한 정보
    // Skeleton.RefBones 인덱스. 부착 대상이 없으면 -1
    int32 AttachedBoneIndex = -1;

    EImportedSkinningType SkinningType = EImportedSkinningType::None;

    bool bHasNormals = false;
    bool bHasTangents = false;
    bool bHasVertexColors = false;
};

// 스켈레톤 그룹 하나의 임포트 결과
struct FSkeletalMeshImportData
{
    FString Name;

    FSkeletonImportData Skeleton;

    TArray<FImportedMesh> Meshes;
    TArray<FImportedMaterial> Materials;
};

// 순수 스태틱 FBX의 모든 메시 노드를 하나의 에셋 공간으로 합친다.
struct FStaticMeshImportData
{
    TArray<FImportedMesh> Meshes;
    TArray<FImportedMaterial> Materials;
};

struct FFbxImportData
{
    FStaticMeshImportData StaticMesh;
    TArray<FSkeletalMeshImportData> SkeletalGroups;
};
