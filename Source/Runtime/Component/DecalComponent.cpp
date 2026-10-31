#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Render/LineBatcher.h"
#include "Render/Material.h"
#include "Math/Box.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "Render/DeferredDecalSceneProxy.h"
#include "Engine/Scene.h"

namespace
{
	// Min/Max로 코너 8개를 만든다. BOX_EDGES가 기대하는 순서다.
	void MakeCorners(const FVector& Min, const FVector& Max, FVector* OutCorners)
	{
		OutCorners[0] = { Min.X, Min.Y, Min.Z };
		OutCorners[1] = { Max.X, Min.Y, Min.Z };
		OutCorners[2] = { Max.X, Max.Y, Min.Z };
		OutCorners[3] = { Min.X, Max.Y, Min.Z };
		OutCorners[4] = { Min.X, Min.Y, Max.Z };
		OutCorners[5] = { Max.X, Min.Y, Max.Z };
		OutCorners[6] = { Max.X, Max.Y, Max.Z };
		OutCorners[7] = { Min.X, Max.Y, Max.Z };
	}
}

UDecalComponent::UDecalComponent() : DecalSize(4.f, 8.f, 8.f)
{
	DecalMaterial = UAssetManager::Get().GetAssetByPath<UMaterial>("DecalMaterial");
}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher) return;

	const FMatrix World = GetWorldMatrix();
	LineBatcher->AddOrientedBox(
		FBox({ -DecalSize.X, -DecalSize.Y, -DecalSize.Z }, { DecalSize.X, DecalSize.Y, DecalSize.Z })
		, World
		, { 0, 255, 0, 1 });

	// 투영 방향(로컬 +X)을 Directional Light처럼 화살표로 그린다. 행벡터 규칙이라 0행이 월드 X축이다.
	const FVector Forward = FVector(World[0][0], World[0][1], World[0][2]).GetSafeNormal();
	const float Length = 3.0f;
	const float Thickness = 0.1f;
	const float HeadSize = 0.5f;
	LineBatcher->AddArrow(GetWorldLocation(), Forward, Length, Thickness, HeadSize, { 0, 1, 0, 1 });
}

void UDecalComponent::SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade)
{
	FadeStartDelay = StartDelay;
	FadeDuration = Duration;
	bDestroyOwnerAfterFade = DestroyOwnerAfterFade;

	//TODO : SetLifeSpan 추가하기
	//if (SceneProxy != nullptr)
	//{
	//	GetWorld()->Scene->UpdateDecalFadeOutTime(this);
	//}
	//else
	//{
	//}
}

void UDecalComponent::SetFadeIn(float StartDelay, float Duration)
{
	FadeInStartDelay = StartDelay;
	FadeInDuration = Duration;

	//if (SceneProxy != nullptr)
	//{
	//	GetWorld()->Scene->UpdateDecalFadeInTime(this);
	//}
	//else
	//{
	//	MarkRenderStateDirty();
	//}
}

void UDecalComponent::SetFadeScreenSize(float NewFadeScreenSize)
{
	FadeScreenSize = NewFadeScreenSize;
	//MarkRenderStateDirty();
}

void UDecalComponent::SetDecalColor(const FLinearColor & Color)
{
	DecalColor = Color;
	//MarkRenderStateDirty();
}

void UDecalComponent::SetDecalMaterial(UMaterial* NewDecalMaterial)
{
	DecalMaterial = NewDecalMaterial;

	//PrecachePSOs();
	//MarkRenderStateDirty();
}

UMaterial* UDecalComponent::GetDecalMaterial() const
{
	return DecalMaterial;
}

FDeferredDecalSceneProxy* UDecalComponent::CreateSceneProxy()
{
	return new FDeferredDecalSceneProxy(this);
}

void UDecalComponent::MarkSceneProxyDirty()
{
	if (SceneProxy)
	{
		SceneProxy->GetScene()->MarkDecalDirty(SceneProxy);
	}
}

void UDecalComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	MarkSceneProxyDirty();
}

void UDecalComponent::OnTransformDirty()
{
	Super::OnTransformDirty();
	MarkSceneProxyDirty();
}

void UDecalComponent::InitializeComponent()
{
	Super::InitializeComponent();

}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDecalComponent::EndPlay()
{
	Super::EndPlay();
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	//TODO : Fade 관련 값들을 계산해서 Alpha값 설정
	if (SceneProxy)
	{
		SceneProxy->FadeAlpha = 1.f;
	}
}

void UDecalComponent::SetActive(bool bNewActive)
{
	Super::SetActive(bNewActive);
}

void UDecalComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties": DecalSize, DecalColor, Fade 값

	// 리플렉션은 머티리얼을 에셋 경로로만 저장해, 텍스처를 바꾼 인스턴스는 null이 된다. Billboard처럼 따로 저장한다.
	UMaterial* Loaded = DecalMaterial;
	UMaterial::SerializeMaterialReference(Record.EnterField("Material"), Loaded);

	if (Record.GetUnderlyingArchive().IsLoading())
	{
		// 불러오기에 실패하면(null) 생성자가 넣어 둔 기본 머티리얼을 유지한다
		if (Loaded)
			DecalMaterial = Loaded;
		MarkSceneProxyDirty();
	}
}


void UDecalComponent::SetTexture(UTexture2D* InTexture)
{
	if (!InTexture) return;

	if (!DecalMaterial)
	{
		DecalMaterial = UAssetManager::GetAssetByPath<UMaterial>("DecalMaterial");
	}
	if (!DecalMaterial) return;

	if (!DecalMaterial->bIsInstance)
	{
		DecalMaterial = UMaterial::CreateInstance(DecalMaterial);
	}

	if (DecalMaterial->Textures.Num() > 0)
	{
		DecalMaterial->Textures[0] = InTexture;
	}
	else
	{
		DecalMaterial->Textures.Add(InTexture);
	}
}

void UDecalComponent::SetTexture(const FString& InTexturePath)
{
	if (UTexture2D* Tex = UAssetManager::Get().LoadTexture(InTexturePath))
	{
		SetTexture(Tex);
	}
}
