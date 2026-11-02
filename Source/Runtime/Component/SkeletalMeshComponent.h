#pragma once

#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

public:
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

   // 부모 본 기준의 '완성된 현재 Local 행렬'을 저장
    void SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform);

    // 이름으로 본 인덱스를 찾은 뒤 위의 인덱스 기반 Setter로 연결
    void SetBoneLocalTransform(const FName& BoneName, const FMatrix& LocalTransform);

    // 전체 현재 Local 포즈를 교체
    void SetSkeletalMeshPose(const TArray<FMatrix>& LocalTransforms);

    // Index의 Local 포즈.
    const FMatrix& GetBoneLocalTransform(int32 BoneIndex) const;

    // 각 본의 RefLocalMatrix를 BoneSpaceTransforms에 복사
    void ResetToReferencePose();

    // 본별 AABB 
    const TArray<FBox>& GetBonePickBounds() const;

protected:
    // 메시 변경 시 Local 포즈와 본 선택 Bounds 배열의 크기를 설정
    void OnMeshDataChanged() override;

    // 부모->자식 순서로 현재 변환 누적
    void RefreshBoneTransforms() override;

private:
    // 갱신된 Bone 원점을 기준으로 AABB
    void UpdateBonePickBounds();

	// RefBones와 같은 인덱스. 부모 본 기준의 현재 Local 포즈.
	TArray<FMatrix> BoneSpaceTransforms;

	// 각 본의 현재 Head 주변을 감싸는 피킹용 AABB.
	// RefBones와 같은 인덱스이며, 본 포즈를 누적한 뒤 갱신한다.
	TArray<FBox> BonePickBounds;

	bool bPoseDirty = false;
};
