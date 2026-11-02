#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	RefreshMesh();
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
	RefreshMesh();
}

void USkinnedMeshComponent::RefreshMesh()
{
	if (!SkeletalMeshData)
		return;

	RefreshBoneTransforms(); // 자식이 PoseDirty 및 MarkSkinningDirty를 설정

	if (!bSkinningDirty)
		return;

    UpdateSkinning();
    UploadSkinnedVertices();
	OnTransformDirty();

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

    RefreshMesh();
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

void USkinnedMeshComponent::RefreshBoneTransforms()
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

	bSkinningDirty = true;
}

void USkinnedMeshComponent::InitializeSkinningResources()
{
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
	LocalBounds = SkeletalMeshData->ReferenceBounds;
	const uint32 BufferSize = SkinnedVertices.Num() * sizeof(FVertexPNCT);
	DynamicVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BufferSize,sizeof(FVertexPNCT));
}

void USkinnedMeshComponent::UpdateSkinning()
{
	for (int32 BoneIndex = 0; BoneIndex < SkinMatrices.Num(); ++BoneIndex)
	{
		SkinMatrices[BoneIndex] = SkeletalMeshData->RefBones[BoneIndex].InverseBindMatrix * ComponentSpaceTransforms[BoneIndex];
		NormalMatrices[BoneIndex] = SkinMatrices[BoneIndex].Inverse().GetTransposed();
	}

	FVector Min = SkinnedVertices.Num() > 0 ? FVector(FLT_MAX, FLT_MAX, FLT_MAX) : FVector::ZeroVector;
	FVector Max = SkinnedVertices.Num() > 0 ? FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) : FVector::ZeroVector;

	for (uint32 index = 0; index < SkinnedVertices.Num(); ++index)
	{
		const FVertexPNCT& Vertex = SkeletalMeshData->Vertices[index].Vertex;
		const FSkeletalMeshVertexInfluences& InfluenceInfo = SkeletalMeshData->Vertices[index].InfluenceInfo;
		const FVector SourceTangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
		const FVector SourceBitangent = Vertex.Normal.Cross(SourceTangent) * Vertex.Tangent.W;

		FVector AfterPosition = FVector::ZeroVector;
		FVector AfterNormal = FVector::ZeroVector;
		FVector AfterTangent = FVector::ZeroVector;
		FVector AfterBitangent = FVector::ZeroVector;
		
		for(uint32 i = InfluenceInfo.FirstInfluence; i < InfluenceInfo.FirstInfluence + InfluenceInfo.InfluenceCount; ++i)
		{
			const FSkeletalMeshInfluence& Influence = SkeletalMeshData->Influences[i];
			AfterPosition += SkinMatrices[Influence.BoneIndex].TransformPosition(Vertex.Position) * Influence.Weight;
			AfterNormal += NormalMatrices[Influence.BoneIndex].TransformVector(Vertex.Normal) * Influence.Weight;
			AfterTangent += SkinMatrices[Influence.BoneIndex].TransformVector(SourceTangent) * Influence.Weight;
			AfterBitangent += SkinMatrices[Influence.BoneIndex].TransformVector(SourceBitangent) * Influence.Weight;
		}

		const FVector N = AfterNormal.GetSafeNormal(SMALL_NUMBER, Vertex.Normal);
		const FVector T = (AfterTangent - N * AfterTangent.Dot(N)).GetSafeNormal(); // 그람 슈미츠 직교화
		const float W = N.Cross(T).Dot(AfterBitangent) >= 0.0f ? 1.0f : -1.0f;

		SkinnedVertices[index].Position = AfterPosition;
		SkinnedVertices[index].Normal = N;
		SkinnedVertices[index].Tangent = FVector4(T, W);
		SkinnedVertices[index].Color = Vertex.Color;
		SkinnedVertices[index].UV = Vertex.UV;

		Min.X = Min.X > AfterPosition.X ? AfterPosition.X : Min.X;
		Min.Y = Min.Y > AfterPosition.Y ? AfterPosition.Y : Min.Y;
		Min.Z = Min.Z > AfterPosition.Z ? AfterPosition.Z : Min.Z;
		Max.X = Max.X < AfterPosition.X ? AfterPosition.X : Max.X;
		Max.Y = Max.Y < AfterPosition.Y ? AfterPosition.Y : Max.Y;
		Max.Z = Max.Z < AfterPosition.Z ? AfterPosition.Z : Max.Z;
	}

	LocalBounds.Min = Min;
	LocalBounds.Max = Max;
}

void USkinnedMeshComponent::UploadSkinnedVertices()
{
	if (!DynamicVertexBuffer || SkinnedVertices.IsEmpty())
		return;

	const uint32 BufferSize = SkinnedVertices.Num() * sizeof(FVertexPNCT);
	RenderCommand::UpdateBufferData(DynamicVertexBuffer.get(), SkinnedVertices.GetData(), BufferSize);
}
