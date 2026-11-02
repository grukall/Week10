#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
}

void USkeletalMeshComponent::SetBoneLocalTransform(const FName& BoneName, const FMatrix& LocalTransform)
{
}

void USkeletalMeshComponent::SetSkeletalMeshPose(const TArray<FMatrix>& LocalTransforms)
{
}

const FMatrix& USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
    // TODO: 반환값 구현.
}

void USkeletalMeshComponent::ResetToReferencePose()
{
}

const TArray<FBox>& USkeletalMeshComponent::GetBonePickBounds() const
{
    // TODO: 반환값 구현.
}

void USkeletalMeshComponent::OnMeshDataChanged()
{
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
}

void USkeletalMeshComponent::UpdateBonePickBounds()
{
}
