#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Collision/HitResult.h"
#include "Collision/Ray.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USkinnedMeshComponent::Serialize(json& Handle, bool bIsLoading)
{
    Super::Serialize(Handle, bIsLoading);
    if (bIsLoading)
    {
        // 리플렉션으로 복원된 에셋에 맞춰 런타임 버퍼를 만들고, 로드한 재질 덮어쓰기는 보존한다.
        SetSkeletalMeshData(SkeletalMesh ? &SkeletalMesh->GetMeshData() : nullptr);
        MarkRenderStateDirty();
        OnTransformDirty();
    }
}

void USkinnedMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);
    if (Record.GetUnderlyingArchive().IsLoading())
    {
        SetSkeletalMeshData(SkeletalMesh ? &SkeletalMesh->GetMeshData() : nullptr);
        MarkRenderStateDirty();
        OnTransformDirty();
    }
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	RefreshMesh(INDEX_NONE);
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMesh == InSkeletalMesh && SkeletalMeshData == (InSkeletalMesh ? &InSkeletalMesh->GetMeshData() : nullptr))
        return;

    SkeletalMesh = InSkeletalMesh;
    ClearOverrideMaterials();
    SetSkeletalMeshData(SkeletalMesh ? &SkeletalMesh->GetMeshData() : nullptr);
    MarkRenderStateDirty();
    OnTransformDirty();
}

void USkinnedMeshComponent::SetSkeletalMeshData(const FSkeletalMeshData* InSkeletalMeshData)
{
    if (SkeletalMesh && InSkeletalMeshData != &SkeletalMesh->GetMeshData())
    {
        SkeletalMesh = nullptr;
        ClearOverrideMaterials();
        MarkRenderStateDirty();
    }
	if (InSkeletalMeshData == SkeletalMeshData)
		return;

	SkeletalMeshData = InSkeletalMeshData;
	InitializeSkinningResources();
	OnMeshDataChanged();
	MarkSkinningDirty();
	RefreshMesh(INDEX_NONE);
}

void USkinnedMeshComponent::RefreshMesh(int32 BoneIndex)
{
	if (!SkeletalMeshData || BoneIndex < INDEX_NONE
		|| (BoneIndex != INDEX_NONE && !SkeletalMeshData->RefBones.IsValidIndex(BoneIndex)))
		return;

	PrepareDirtyBoneRanges();
	RefreshBoneTransforms(BoneIndex);

	if (!bSkinningDirty)
		return;

    UpdateSkinning();
    if (!DirtyVertexIndices.IsEmpty())
    {
        UploadSkinnedVertices();
        OnTransformDirty();
    }

	ClearDirtyBoneRanges();
	bSkinningDirty = false;
}

const TArray<FVertexPNCT>& USkinnedMeshComponent::GetSkinnedVertices() const
{
	return SkinnedVertices;
}

const TArray<FMatrix>& USkinnedMeshComponent::GetComponentSpaceTransforms() const
{
	return ComponentSpaceTransforms;
}

const FVertexBuffer* USkinnedMeshComponent::GetDynamicVertexBuffer() const
{
	return DynamicVertexBuffer.get();
}

FBox USkinnedMeshComponent::CalcLocalBounds() const
{
	return LocalBounds;
}

bool USkinnedMeshComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
    const FRay LocalRay = ToLocalRay(WorldRay, GetWorldMatrix());
    float T = FLT_MAX;
    if (!LineTraceComponentLocal(LocalRay, T))
        return false;

    OutHit.HitComponent = this;
    OutHit.Distance = T;
    OutHit.ImpactPoint = WorldRay.PointAt(T);
    OutHit.FaceIndex = INDEX_NONE;
    return true;
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
    if (!SkeletalMeshData || SkinnedVertices.IsEmpty())
        return false;

    float BoundsT;
    if (!RayIntersectsAABB(LocalRay, LocalBounds.Min, LocalBounds.Max, BoundsT)
        || BoundsT >= OutT)
    {
        return false;
    }

    const TArray<uint32>& Indices = SkeletalMeshData->Indices;
    const uint32 IndexCount = static_cast<uint32>(Indices.Num());
    const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.Num());
    float NearestT = OutT;
    bool bHit = false;

    for (uint32 Index = 0; Index + 2 < IndexCount; Index += 3)
    {
        const uint32 I0 = Indices[Index];
        const uint32 I1 = Indices[Index + 1];
        const uint32 I2 = Indices[Index + 2];
        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
            continue;

        float TriangleT;
        if (RayIntersectsTriangle(LocalRay,
            SkinnedVertices[I0].Position,
            SkinnedVertices[I1].Position,
            SkinnedVertices[I2].Position,
            TriangleT) && TriangleT < NearestT)
        {
            NearestT = TriangleT;
            bHit = true;
        }
    }

    // 로컬 레이 방향을 정규화하지 않으므로 기존 최근접 월드 레이 t와 비교할 수 있다.
    if (bHit)
        OutT = NearestT;
    return bHit;
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
    return SkeletalMesh ? SkeletalMesh->GetNumMaterials() : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
    return SkeletalMesh && SlotIndex >= 0 && SlotIndex < GetNumMaterials()
        ? SkeletalMesh->GetMeshData().MaterialSlots[SlotIndex].Name : FString();
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
    return SkeletalMesh && SlotIndex >= 0 && SlotIndex < GetNumMaterials()
        ? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void USkinnedMeshComponent::SubmitToRenderQueue(FRenderQueue& Queue)
{
    if (!SkeletalMesh || !IsVisible())
        return;

    RefreshMesh(INDEX_NONE);
    const FIndexBuffer* Indices = SkeletalMesh->GetIndexBuffer();
    if (!DynamicVertexBuffer || !DynamicVertexBuffer->GetBuffer() || !Indices || !Indices->GetBuffer())
        return;

    const FMatrix* World = Queue.StoreWorldMatrix(GetWorldMatrix());
    for (const FSkeletalMeshSection& Section : SkeletalMesh->GetMeshData().Sections)
    {
        UMaterial* Material = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
        if (!Material || Section.IndexCount == 0)
            continue;

        FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
        Packet.Proxy = GetSceneProxy();
        Packet.Model = World;
        Packet.VertexBuffer = DynamicVertexBuffer.get();
        Packet.IndexBuffer = Indices;
        Packet.Material = Material;
        Packet.StartIndex = Section.StartIndex;
        Packet.IndexCount = Section.IndexCount;
    }
}

void USkinnedMeshComponent::OnMeshDataChanged()
{
	// USkeletalMeshComponent::OnMeshDataChanged()
}

void USkinnedMeshComponent::RefreshBoneTransforms(int32 BoneIndex)
{
	// USkeletalMeshComponent::RefreshBoneTransforms()
}

const FSkeletalMeshData* USkinnedMeshComponent::GetSkeletalMeshData() const
{
	return SkeletalMeshData;
}

void USkinnedMeshComponent::MarkSkinningDirty()
{
	if (!SkeletalMeshData)
		return;

	ClearDirtyBoneRanges();
	if (!SkeletalMeshData->RefBones.IsEmpty())
	{
		DirtyBoneStarts.Add(0);
		DirtyBoneEndIndices[0] = SkeletalMeshData->RefBones.Num();
	}
	bSkinningDirty = true;
}

void USkinnedMeshComponent::MarkSkinningDirty(int32 FirstBone, int32 EndBone)
{
	if (!SkeletalMeshData)
		return;
	if (!DirtyBoneEndIndices.IsValidIndex(FirstBone) || EndBone <= FirstBone
		|| EndBone > SkeletalMeshData->RefBones.Num())
	{
		MarkSkinningDirty();
		return;
	}
	int32& SavedEnd = DirtyBoneEndIndices[FirstBone];
	if (SavedEnd == INDEX_NONE)
		DirtyBoneStarts.Add(FirstBone);
	SavedEnd = std::max(SavedEnd, EndBone);
	bSkinningDirty = true;
}

void USkinnedMeshComponent::PrepareDirtyBoneRanges()
{
	std::sort(DirtyBoneStarts.begin(), DirtyBoneStarts.end());
	int32 WriteIndex = 0;
	for (int32 ReadIndex = 0; ReadIndex < DirtyBoneStarts.Num(); ++ReadIndex)
	{
		const int32 FirstBone = DirtyBoneStarts[ReadIndex];
		if (WriteIndex > 0)
		{
			const int32 PreviousStart = DirtyBoneStarts[WriteIndex - 1];
			if (FirstBone <= DirtyBoneEndIndices[PreviousStart])
			{
				DirtyBoneEndIndices[PreviousStart] = std::max(DirtyBoneEndIndices[PreviousStart], DirtyBoneEndIndices[FirstBone]);
				DirtyBoneEndIndices[FirstBone] = INDEX_NONE;
				continue;
			}
		}
		DirtyBoneStarts[WriteIndex++] = FirstBone;
	}
	DirtyBoneStarts.SetNum(WriteIndex, false);
}

void USkinnedMeshComponent::ClearDirtyBoneRanges()
{
	for (int32 FirstBone : DirtyBoneStarts)
		DirtyBoneEndIndices[FirstBone] = INDEX_NONE;
	DirtyBoneStarts.Reset();
}

void USkinnedMeshComponent::InitializeSkinningResources()
{
	DirtyBoneEndIndices.Empty();
	DirtyBoneStarts.Empty();
	BoneVertexIndices.Empty();
	DirtyVertexFlags.Empty();
	DirtyVertexIndices.Empty();
	VertexBoundsBlocks.Empty();
	bNeedsFullSkinning = true;
	if (!SkeletalMeshData)
	{
		ComponentSpaceTransforms.Empty();
		SkinMatrices.Empty();
		NormalMatrices.Empty();
		SkinnedVertices.Empty();
		LocalBounds = FBox{ FVector::ZeroVector,FVector::ZeroVector };
		DynamicVertexBuffer.reset();
		OnTransformDirty();
		return;
	}

	ComponentSpaceTransforms.SetNum(SkeletalMeshData->RefBones.Num());
	SkinMatrices.SetNum(SkeletalMeshData->RefBones.Num());
	NormalMatrices.SetNum(SkeletalMeshData->RefBones.Num());
	SkinnedVertices.SetNum(SkeletalMeshData->Vertices.Num());
	BoneVertexIndices.SetNum(SkeletalMeshData->RefBones.Num());
	DirtyBoneEndIndices.Init(INDEX_NONE, SkeletalMeshData->RefBones.Num());
	DirtyVertexFlags.Init(0, SkinnedVertices.Num());
	VertexBoundsBlocks.SetNum((SkinnedVertices.Num() + BoundsBlockSize - 1) / BoundsBlockSize);
	for (int32 VertexIndex = 0; VertexIndex < SkinnedVertices.Num(); ++VertexIndex)
	{
		const auto& Info = SkeletalMeshData->Vertices[VertexIndex].InfluenceInfo;
		for (uint32 i = Info.FirstInfluence; i < Info.FirstInfluence + Info.InfluenceCount; ++i)
		{
			const auto& Influence = SkeletalMeshData->Influences[i];
			if (Influence.Weight != 0.0f)
				BoneVertexIndices[Influence.BoneIndex].Add(static_cast<uint32>(VertexIndex));
		}
	}
	LocalBounds = SkeletalMeshData->ReferenceBounds;
	const uint32 BufferSize = SkinnedVertices.Num() * sizeof(FVertexPNCT);
	DynamicVertexBuffer = BufferSize > 0
		? RenderCommand::CreateDefaultVertexBuffer(BufferSize, sizeof(FVertexPNCT)) : nullptr;
}

void USkinnedMeshComponent::UpdateSkinning()
{
	DirtyVertexIndices.Reset();
	for (int32 FirstBone : DirtyBoneStarts)
	{
		const int32 EndBone = DirtyBoneEndIndices[FirstBone];
		for (int32 BoneIndex = FirstBone; BoneIndex < EndBone; ++BoneIndex)
		{
			SkinMatrices[BoneIndex] = SkeletalMeshData->RefBones[BoneIndex].InverseBindMatrix * ComponentSpaceTransforms[BoneIndex];
			NormalMatrices[BoneIndex] = SkinMatrices[BoneIndex].Inverse().GetTransposed();
			if (!bNeedsFullSkinning)
			{
				for (uint32 VertexIndex : BoneVertexIndices[BoneIndex])
				{
					if (!DirtyVertexFlags[VertexIndex])
					{
						DirtyVertexFlags[VertexIndex] = 1;
						DirtyVertexIndices.Add(VertexIndex);
					}
				}
			}
		}
	}

	if (bNeedsFullSkinning)
	{
		for (int32 VertexIndex = 0; VertexIndex < SkinnedVertices.Num(); ++VertexIndex)
			DirtyVertexIndices.Add(static_cast<uint32>(VertexIndex));
	}
	else if (DirtyVertexIndices.Num() == SkinnedVertices.Num())
	{
		for (int32 i = 0; i < DirtyVertexIndices.Num(); ++i)
			DirtyVertexIndices[i] = static_cast<uint32>(i);
	}
	else
	{
		std::sort(DirtyVertexIndices.begin(), DirtyVertexIndices.end());
	}

	for (uint32 VertexIndex : DirtyVertexIndices)
	{
		SkinVertex(VertexIndex);
		DirtyVertexFlags[VertexIndex] = 0;
	}
	if (!DirtyVertexIndices.IsEmpty())
		UpdateSkinningBounds();
	bNeedsFullSkinning = false;
}

void USkinnedMeshComponent::SkinVertex(uint32 VertexIndex)
{
	const FVertexPNCT& Vertex = SkeletalMeshData->Vertices[VertexIndex].Vertex;
	const auto& InfluenceInfo = SkeletalMeshData->Vertices[VertexIndex].InfluenceInfo;
	const FVector SourceTangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
	const FVector SourceBitangent = Vertex.Normal.Cross(SourceTangent) * Vertex.Tangent.W;
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	FVector Tangent = FVector::ZeroVector;
	FVector Bitangent = FVector::ZeroVector;
	// 변경된 본뿐 아니라 이 정점에 연결된 모든 본의 기여를 다시 합산한다.
	for (uint32 i = InfluenceInfo.FirstInfluence; i < InfluenceInfo.FirstInfluence + InfluenceInfo.InfluenceCount; ++i)
	{
		const auto& Influence = SkeletalMeshData->Influences[i];
		Position += SkinMatrices[Influence.BoneIndex].TransformPosition(Vertex.Position) * Influence.Weight;
		Normal += NormalMatrices[Influence.BoneIndex].TransformVector(Vertex.Normal) * Influence.Weight;
		Tangent += SkinMatrices[Influence.BoneIndex].TransformVector(SourceTangent) * Influence.Weight;
		Bitangent += SkinMatrices[Influence.BoneIndex].TransformVector(SourceBitangent) * Influence.Weight;
	}
	const FVector N = Normal.GetSafeNormal(SMALL_NUMBER, Vertex.Normal);
	const FVector T = (Tangent - N * Tangent.Dot(N)).GetSafeNormal();
	const float W = N.Cross(T).Dot(Bitangent) >= 0.0f ? 1.0f : -1.0f;
	auto& Output = SkinnedVertices[VertexIndex];
	Output.Position = Position;
	Output.Normal = N;
	Output.Tangent = FVector4(T, W);
	Output.Color = Vertex.Color;
	Output.UV = Vertex.UV;
}

void USkinnedMeshComponent::UpdateSkinningBounds()
{
	uint32 PreviousBlock = ~0u;
	for (uint32 VertexIndex : DirtyVertexIndices)
	{
		const uint32 BlockIndex = VertexIndex / BoundsBlockSize;
		if (BlockIndex == PreviousBlock)
			continue;
		PreviousBlock = BlockIndex;
		const uint32 FirstVertex = BlockIndex * BoundsBlockSize;
		const uint32 EndVertex = std::min(FirstVertex + BoundsBlockSize, static_cast<uint32>(SkinnedVertices.Num()));
		FBox Bounds{ SkinnedVertices[FirstVertex].Position, SkinnedVertices[FirstVertex].Position };
		for (uint32 i = FirstVertex + 1; i < EndVertex; ++i)
			Bounds.Expand(FBox{ SkinnedVertices[i].Position, SkinnedVertices[i].Position });
		VertexBoundsBlocks[BlockIndex] = Bounds;
	}

	LocalBounds = VertexBoundsBlocks[0];
	for (int32 i = 1; i < VertexBoundsBlocks.Num(); ++i)
		LocalBounds.Expand(VertexBoundsBlocks[i]);
}

void USkinnedMeshComponent::UploadSkinnedVertices()
{
	if (!DynamicVertexBuffer || DirtyVertexIndices.IsEmpty())
		return;

	int32 RangeCount = 1;
	for (int32 i = 1; i < DirtyVertexIndices.Num(); ++i)
		RangeCount += DirtyVertexIndices[i] != DirtyVertexIndices[i - 1] + 1;
	// 분산된 구간의 API 호출이 너무 많아지면 캐시된 출력 전체를 한 번 전송한다.
	constexpr int32 MaxPartialUploadRanges = 64;
	if (RangeCount > MaxPartialUploadRanges || DirtyVertexIndices.Num() == SkinnedVertices.Num())
	{
		RenderCommand::UpdateBufferRegion(DynamicVertexBuffer.get(), 0, SkinnedVertices.GetData(), SkinnedVertices.Num() * sizeof(FVertexPNCT));
		return;
	}
	int32 First = 0;
	while (First < DirtyVertexIndices.Num())
	{
		int32 End = First + 1;
		while (End < DirtyVertexIndices.Num() && DirtyVertexIndices[End] == DirtyVertexIndices[End - 1] + 1)
			++End;
		const uint32 FirstVertex = DirtyVertexIndices[First];
		const uint32 VertexCount = static_cast<uint32>(End - First);
		RenderCommand::UpdateBufferRegion(DynamicVertexBuffer.get(), FirstVertex * sizeof(FVertexPNCT),
			SkinnedVertices.GetData() + FirstVertex, VertexCount * sizeof(FVertexPNCT));
		First = End;
	}
}
