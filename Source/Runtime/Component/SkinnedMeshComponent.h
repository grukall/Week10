#pragma once

#include "MeshComponent.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"

class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

public:
	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() = default;

	void SetSkeletalMeshData(FSkeletalMeshData* InSkeletalMeshData) { SkeletalMeshData = InSkeletalMeshData; }
	

protected:
	// RefBones와 같은 인덱스. 자식 클래스가 부모부터 누적한 현재 본 변환을 채운다.
	TArray<FMatrix> ComponentSpaceTransforms;
private:
	// 외부 에셋이 소유하는 기준 데이터. 포즈를 조작해도 원본은 변경하지 않는다.
	const FSkeletalMeshData* SkeletalMeshData = nullptr;
	// 본별 InverseBindMatrix * ComponentSpaceTransforms로 계산한 스키닝 행렬.
	TArray<FMatrix> SkinMatrices;
	// 기준 정점에서 CPU 스키닝한 결과. 위치·법선·접선은 메시 로컬 공간이다.
	TArray<FVertexPNCT> SkinnedVertices;
	// 현재 변형된 메시 전체를 감싸는 메시 로컬 공간 AABB.
	FBox LocalBounds{ FVector::ZeroVector, FVector::ZeroVector };
	// SkinnedVertices를 업로드할 인스턴스별 GPU 정점 버퍼.
	TUniquePtr<FVertexBuffer> DynamicVertexBuffer;

	bool bSkinningDirty = true;
};
