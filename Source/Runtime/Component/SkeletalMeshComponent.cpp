#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data || BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num())
        return;

    BoneSpaceTransforms[BoneIndex] = LocalTransform;
    bPoseDirty = true;
}

void USkeletalMeshComponent::SetBoneLocalTransform(const FName& BoneName, const FMatrix& LocalTransform)
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data)
        return;

	for (int32 i = 0; i < Data->RefBones.Num(); ++i)
	{
		if (Data->RefBones[i].Name == BoneName)
		{
			SetBoneLocalTransform(i, LocalTransform);
			return;
		}
	}
}

void USkeletalMeshComponent::SetSkeletalMeshPose(const TArray<FMatrix>& LocalTransforms)
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data || LocalTransforms.Num() != Data->RefBones.Num())
		return;

	BoneSpaceTransforms = LocalTransforms;
	bPoseDirty = true;
}

const FMatrix& USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num())
		return FMatrix::Identity;

	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::ResetToReferencePose()
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();

    if (!Data)
        return;

    BoneSpaceTransforms.SetNum(Data->RefBones.Num());

    for (int32 i = 0; i < Data->RefBones.Num(); ++i)
    {
        BoneSpaceTransforms[i] = Data->RefBones[i].RefLocalMatrix;
    }

    bPoseDirty = true;
}

const TArray<FBox>& USkeletalMeshComponent::GetBonePickBounds() const
{
	return BonePickBounds;
}

void USkeletalMeshComponent::OnMeshDataChanged()
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();

    if (!Data)
    {
        BoneSpaceTransforms.Empty();
        BonePickBounds.Empty();
        bPoseDirty = false;
        return;
    }

    BonePickBounds.SetNum(Data->RefBones.Num());
    ResetToReferencePose();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
    if (!bPoseDirty)
        return;

    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data)
        return;

    for (int32 i = 0; i < BoneSpaceTransforms.Num(); ++i)
    {
        const int32 ParentIndex = Data->RefBones[i].ParentIndex;
        ComponentSpaceTransforms[i] = ParentIndex == -1  ? BoneSpaceTransforms[i] : BoneSpaceTransforms[i] * ComponentSpaceTransforms[ParentIndex];
    }

	UpdateBonePickBounds();
	bPoseDirty = false;
	MarkSkinningDirty();
}

void USkeletalMeshComponent::UpdateBonePickBounds()
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();

    if (!Data)
        return;

    const float MeshDiagonal = (Data->ReferenceBounds.Max - Data->ReferenceBounds.Min).Size();

    const float HalfSize = MeshDiagonal * 0.005f; // AABB최소 크기 하드 코딩
    const FVector Extent(HalfSize, HalfSize, HalfSize);

    for (int32 i = 0; i < ComponentSpaceTransforms.Num(); ++i)
    {
        const FVector Center = ComponentSpaceTransforms[i].TransformPosition(FVector::ZeroVector);
        BonePickBounds[i] = FBox{ Center - Extent, Center + Extent };
    }
}
