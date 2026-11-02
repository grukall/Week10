#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	RefreshMesh();
}

void USkinnedMeshComponent::SetSkeletalMeshData(const FSkeletalMeshData* InSkeletalMeshData)
{
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

	OnTransformDirty();
}

void USkinnedMeshComponent::UploadSkinnedVertices()
{
	if (!DynamicVertexBuffer || SkinnedVertices.IsEmpty())
		return;

	const uint32 BufferSize = SkinnedVertices.Num() * sizeof(FVertexPNCT);
	RenderCommand::UpdateBufferData(DynamicVertexBuffer.get(), SkinnedVertices.GetData(), BufferSize);
}
