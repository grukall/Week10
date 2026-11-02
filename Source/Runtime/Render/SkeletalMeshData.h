#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Core/NameTypes.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"
#include "Math/Box.h"
#include "Math/Color.h"
#include "Render/Vertex.h"

/* Rule */
// 변환 순서 (SMD : 로컬 → 부모) → (SMC : 컴포넌트) → 월드
// 각 정점 데이터 별로 Influence 정보를 가지는게 아닌 Influence 버퍼 배열에서 읽어오는 방식
// Bone Head의 AABB는 Component에서 관리한다
// Bone Index는 FSkeletalMeshData의 RefBones의 인덱스를 기준으로 한다.

struct FSkeletalMeshInfluence
{
    int32 BoneIndex = -1; // 해당 정점에 영향을 미치는 본
    float Weight = 0.0f;  // 본이 해당 정점에 영향을 미치는 가중치
};

// 정점에 Influence를 저장하는게 아니라 Influences 버퍼에서 값을 불러온다
struct FSkeletalMeshVertexInfluences
{
    uint32 FirstInfluence = 0; // Offset
    uint32 InfluenceCount = 0; // Count
};

// Importer가 전달하는 변경되지 않는 기준 본 데이터
struct FSkeletalMeshRefBone
{
    FName Name = NAME_None; // 본 이름
    int32 ParentIndex = -1; // 부모 본

    // 부모 본 기준의 Reference Pose 변환 = 부모 본에서 얼마나 떨어져 있는가?
    FMatrix RefLocalMatrix = FMatrix::Identity;
    // 메시의 Bind Pose 공간 → 이 본의 Bind Pose 로컬 공간 변환
    // 현재 RefLocalMatrix ~ RootRefLocalMatrix 곱의 역행렬
    // 이 역행렬로 추후에 정점을 정점이 속한 bone의 좌표계로 변환할때 쓸 수 있다
    FMatrix InverseBindMatrix = FMatrix::Identity;
};

// MaterialSlotIndex <-> StartIndex <= index < StartIndex + IndexCount
struct FSkeletalMeshSection
{
    uint32 StartIndex = 0; 
    uint32 IndexCount = 0;
    uint32 MaterialSlotIndex = 0;
};

// 기본 Lit 머티리얼을 구성하기 위한 데이터
// 실제 UMaterial 생성과 텍스처 로딩은 에셋 생성 단계에서 처리한다
struct FSkeletalMaterialSlot
{
    FString Name;

    FLinearColor BaseColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
    FLinearColor SpecularColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

    float Shininess = 32.0f;  // Phong 머티리얼 에서의 기본값.

    FString DiffuseTexturePath;
    FString NormalTexturePath;
};

// 정점 하나의 데이터
struct FSkeletalMeshVertex
{
    FVertexPNCT Vertex; // Position, Normal, Tangent, Color, UV

    // Influences 버퍼 배열에서 해당 정보에 맞게 가져온다
    // 가중치의 합은 반드시 1이어야 한다
    FSkeletalMeshVertexInfluences InfluenceInfo;
};

// 메시 하나의 전체 데이터
struct FSkeletalMeshData
{
    // 메시 정점 정보 및 본
    TArray<FSkeletalMeshVertex> Vertices; // SkeletalMeshVertexs
    TArray<uint32> Indices; // TriangleList
    TArray<FSkeletalMeshRefBone> RefBones;  // 메시 전체에서 공유하는 본 계층과 ReferencePose 변환 
    TArray<FSkeletalMeshInfluence> Influences; // Vertices 들은 해당 Influencse에서 정보를 불러온다.

    // 머터리얼
    TArray<FSkeletalMeshSection> Sections;
    TArray<FSkeletalMaterialSlot> MaterialSlots;

    // AABB
    FBox ReferenceBounds{ FVector::ZeroVector,FVector::ZeroVector };

    bool IsValid()
    {
        // Todo : 인덱스 매핑이 잘되어 있는지 확인하는 함수 완성하기
    }
};
