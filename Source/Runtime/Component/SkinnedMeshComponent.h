#pragma once

#include "MeshComponent.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"

class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

public:
	USkinnedMeshComponent();
	~USkinnedMeshComponent() override = default;

    void TickComponent(float DeltaTime) override;
	// SkeletalMeshData를 설정하면 내부적으로 Skinning 관련 버퍼를 초기화
    void SetSkeletalMeshData(const FSkeletalMeshData* InSkeletalMeshData);
    // 공통 갱신 진입점.
    void RefreshMesh();
    // RefreshMesh 이후의 CPU 스키닝 결과를 읽는다.
    const TArray<FVertexPNCT>& GetSkinnedVertices() const;
    // Bone들의 누적 변환
    const TArray<FMatrix>& GetComponentSpaceTransforms() const;
    // 렌더링에서 사용할 인스턴스별 출력 정점 버퍼.
    const FVertexBuffer* GetDynamicVertexBuffer() const;
    // 현재 변형된 메시 전체의 로컬 AABB
    FBox CalcLocalBounds() const override;

protected:
    // 새로운 메시의 본계층에 맞게 다시 초기화
    virtual void OnMeshDataChanged();
    // 자식이 ComponentSpaceTransforms를 갱신하고 MarkSkinningDirty를 호출한다.
    virtual void RefreshBoneTransforms();
    // 자식이 본 계층과 기준 포즈를 읽기 위한 접근 함수.
    const FSkeletalMeshData* GetSkeletalMeshData() const;
    // 자식이 스키닝 결과의 재계산을 요청한다.
    void MarkSkinningDirty();
	// RefBones와 같은 인덱스. 자식 클래스가 부모부터 누적한 현재 본 변환을 채운다.
	TArray<FMatrix> ComponentSpaceTransforms;

private:
    // SkeletalMeshData가 변경될때 멤버 변수를 초기화
    void InitializeSkinningResources();
    // Skinning 수행
    void UpdateSkinning();
    // SkinnedVertices를 기존 DynamicVertexBuffer에 업로드한다.
    void UploadSkinnedVertices();
	// 외부 에셋이 소유하는 기준 데이터. 포즈를 조작해도 원본은 변경하지 않는다.
	const FSkeletalMeshData* SkeletalMeshData = nullptr;
	// 본별 InverseBindMatrix * ComponentSpaceTransforms로 계산한 스키닝 행렬.
	TArray<FMatrix> SkinMatrices;
    // SkinMatrices의 역행렬 후 전치한 행렬
	TArray<FMatrix> NormalMatrices;
	// 기준 정점에서 CPU 스키닝한 결과. 위치·법선·접선은 메시 로컬 공간이다.
	TArray<FVertexPNCT> SkinnedVertices;
	// 현재 변형된 메시 전체를 감싸는 메시 로컬 공간 AABB.
	FBox LocalBounds{ FVector::ZeroVector, FVector::ZeroVector };
	// SkinnedVertices를 업로드할 인스턴스별 GPU 정점 버퍼.
	TUniquePtr<FVertexBuffer> DynamicVertexBuffer;

	bool bSkinningDirty = true;
};
