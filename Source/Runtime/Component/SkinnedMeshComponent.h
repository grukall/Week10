#pragma once

#include "MeshComponent.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"
#include "Render/SkeletalMesh.h"

class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	REFLECT_START(USkinnedMeshComponent)
		PROPERTY(SkeletalMesh)
		FUNCTION(GetSkeletalMesh)
		FUNCTION(SetSkeletalMesh)
	REFLECT_END()

public:
	USkinnedMeshComponent();
	~USkinnedMeshComponent() override = default;

    using Super::Serialize;
    void Serialize(json& Handle, bool bIsLoading) override;
    void Serialize(FStructuredArchive::FRecord Record) override;

    void TickComponent(float DeltaTime) override;
	// 렌더링에 필요한 재질과 인덱스 버퍼를 포함한 에셋을 설정한다.
    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	// SkeletalMeshData를 설정하면 내부적으로 Skinning 관련 버퍼를 초기화
    void SetSkeletalMeshData(const FSkeletalMeshData* InSkeletalMeshData);
    // INDEX_NONE이면 컴포넌트가 기록한 변경 범위를 갱신한다.
    // BoneIndex를 지정해도 Setter에 누적된 다른 변경 구간을 함께 처리한다.
    void RefreshMesh(int32 BoneIndex = INDEX_NONE);
    // RefreshMesh 이후의 CPU 스키닝 결과를 읽는다.
    const TArray<FVertexPNCT>& GetSkinnedVertices() const;
    // Bone들의 누적 변환
    const TArray<FMatrix>& GetComponentSpaceTransforms() const;
    // 렌더링에서 사용할 인스턴스별 출력 정점 버퍼.
    const FVertexBuffer* GetDynamicVertexBuffer() const;
    // 현재 변형된 메시 전체의 로컬 AABB
    FBox CalcLocalBounds() const override;
    // 씬 BVH와 LocalBounds로 후보를 거른 뒤 현재 스키닝된 삼각형을 검사한다.
    bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;
    bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
    int32 GetNumMaterials() const override;
    FString GetMaterialSlotName(int32 SlotIndex) const override;
    UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
    void SubmitToRenderQueue(FRenderQueue& Queue) override;

protected:
    // 새로운 메시의 본계층에 맞게 다시 초기화
    virtual void OnMeshDataChanged();
    // Setter에서 기록한 구간의 ComponentSpaceTransforms를 갱신한다.
    virtual void RefreshBoneTransforms(int32 BoneIndex);
    // 자식이 본 계층과 기준 포즈를 읽기 위한 접근 함수.
    const FSkeletalMeshData* GetSkeletalMeshData() const;
    // 전체 포즈 또는 지정한 본 구간의 재계산을 요청한다.
    void MarkSkinningDirty();
    void MarkSkinningDirty(int32 FirstBone, int32 EndBone);
	// RefBones와 같은 인덱스. 자식 클래스가 부모부터 누적한 현재 본 변환을 채운다.
	TArray<FMatrix> ComponentSpaceTransforms;
	// 시작 본 인덱스 -> 갱신 구간 끝(미포함). 변경이 없으면 INDEX_NONE.
	TArray<int32> DirtyBoneEndIndices;
	// 위 배열에서 실제로 변경된 시작 본만 보관한다. 갱신 전에 정렬·병합한다.
	TArray<int32> DirtyBoneStarts;

private:
    USkeletalMesh* SkeletalMesh = nullptr;
    // SkeletalMeshData가 변경될때 멤버 변수를 초기화
    void InitializeSkinningResources();
    void PrepareDirtyBoneRanges();
    void ClearDirtyBoneRanges();
    // 기록된 본 구간에 연결된 정점만 스키닝한다.
    void UpdateSkinning();
    void SkinVertex(uint32 VertexIndex);
    void UpdateSkinningBounds();
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
	// 메시 변경 시 한 번 만드는 본 -> 정점 역참조. 정점은 여러 본 목록에 들어갈 수 있다.
	TArray<TArray<uint32>> BoneVertexIndices;
	TArray<uint8> DirtyVertexFlags;
	TArray<uint32> DirtyVertexIndices;
	// 부분 갱신 중에도 AABB가 정확히 줄어들도록 블록별 Bounds를 캐시한다.
	static constexpr uint32 BoundsBlockSize = 256;
	TArray<FBox> VertexBoundsBlocks;
	// 현재 변형된 메시 전체를 감싸는 메시 로컬 공간 AABB.
	FBox LocalBounds{ FVector::ZeroVector, FVector::ZeroVector };
	// SkinnedVertices를 업로드할 인스턴스별 GPU 정점 버퍼.
	TUniquePtr<FVertexBuffer> DynamicVertexBuffer;

	bool bSkinningDirty = true;
	bool bNeedsFullSkinning = true;
};
