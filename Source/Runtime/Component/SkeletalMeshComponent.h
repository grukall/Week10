#pragma once

#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

public:
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() = default;

private:
	// RefBones와 같은 인덱스. 부모 본 기준의 현재 Local 포즈.
	TArray<FMatrix> BoneSpaceTransforms;

	// 각 본의 현재 Head 주변을 감싸는 피킹용 AABB.
	// RefBones와 같은 인덱스이며, 본 포즈를 누적한 뒤 갱신한다.
	TArray<FBox> BonePickBounds;

	bool bPoseDirty = false;
};
