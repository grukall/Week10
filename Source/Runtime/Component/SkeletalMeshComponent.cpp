#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data || BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num())
        return;

    BoneSpaceTransforms[BoneIndex] = LocalTransform;
    if (!bPoseDirty)
        DirtyBoneIndex = BoneIndex;
    else if (DirtyBoneIndex != BoneIndex)
        DirtyBoneIndex = INDEX_NONE;
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
	DirtyBoneIndex = INDEX_NONE;
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

    DirtyBoneIndex = INDEX_NONE;
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
        DirtyBoneIndex = INDEX_NONE;
        bPoseDirty = false;
        return;
    }

    BonePickBounds.SetNum(Data->RefBones.Num());
    ResetToReferencePose();
}

void USkeletalMeshComponent::RefreshBoneTransforms(int32 BoneIndex)
{
    if (!bPoseDirty)
        return;

    const FSkeletalMeshData* Data = GetSkeletalMeshData();
    if (!Data || BoneIndex < INDEX_NONE
        || (BoneIndex != INDEX_NONE && !BoneSpaceTransforms.IsValidIndex(BoneIndex)))
        return;

    // 호출자가 지정한 본 외에 누적된 변경도 놓치지 않도록 한다.
    if (BoneIndex == INDEX_NONE)
        BoneIndex = DirtyBoneIndex;
    else if (DirtyBoneIndex != BoneIndex)
        BoneIndex = INDEX_NONE;

    int32 FirstBone = 0;
    int32 EndBone = BoneSpaceTransforms.Num();
    if (BoneIndex != INDEX_NONE)
    {
        const int32 SubtreeEnd = Data->RefBones[BoneIndex].SubtreeEndIndex;
        // 범위 정보가 없는 기존 데이터는 전체 갱신으로 처리한다.
        if (SubtreeEnd > BoneIndex && SubtreeEnd <= EndBone)
        {
            FirstBone = BoneIndex;
            EndBone = SubtreeEnd;
        }
    }

    for (int32 i = FirstBone; i < EndBone; ++i)
    {
        const int32 ParentIndex = Data->RefBones[i].ParentIndex;
        ComponentSpaceTransforms[i] = ParentIndex == -1  ? BoneSpaceTransforms[i] : BoneSpaceTransforms[i] * ComponentSpaceTransforms[ParentIndex];
    }

	UpdateBonePickBounds();
	DirtyBoneIndex = INDEX_NONE;
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
