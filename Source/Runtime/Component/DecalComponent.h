#pragma once
#include "EnginePCH.h"
#include "SceneComponent.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Core/EngineString.h"

class FLineBatcher;
class UMaterial;

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(DecalColor)
		PROPERTY(FadeScreenSize)
		PROPERTY(FadeStartDelay)
		PROPERTY(FadeDuration)
		PROPERTY(FadeInDuration)
		PROPERTY(FadeInStartDelay)
		PROPERTY(bDestroyOwnerAfterFade)
		PROPERTY(DecalMaterial)
		FUNCTION(GetFadeStartDelay)
		FUNCTION(GetFadeDuration)
		FUNCTION(GetFadeInStartDelay)
		FUNCTION(GetFadeInDuration)
		FUNCTION(SetFadeOut)
		FUNCTION(SetFadeIn)
		FUNCTION(SetFadeScreenSize)
		FUNCTION(SetDecalColor)
		FUNCTION(SetDecalMaterial)
		FUNCTION(GetDecalMaterial)
	REFLECT_END()

public:

	UDecalComponent();
	virtual ~UDecalComponent() = default;

	//=====================================
	//1. Decal 외형
	/** Decal size in local space (does not include the component scale), technically redundant but there for convenience */
	FVector DecalSize;

	/** Decal color, can be accessed using the material Decal Color node. */
	FLinearColor DecalColor = FLinearColor::White;


	void DrawDebug(FLineBatcher* LineBatcher) const;

	//=====================================
	//2. Fade

	//데칼이 화면에서 차지하는 크기가 이 값보다 작아지면 흐려지다가 사라짐, 0이면 끔.
	//데칼 박스를 감싸는 구의 반지름 * 카메라까지의 거리
	float FadeScreenSize = 0.0f;


	//시간 Fade
	//생긴 뒤 얼마나 기다렸다가 나타나기 시작할지, 그동안 alpha는 0
	float FadeInStartDelay = 0.0f;

	//0에서 1까지 나타나는데 걸리는 시간, 0이면 fadein없이 바로 보임
	float FadeInDuration = 0.0f;

	//셍긴 뒤 얼마나 유지하다가 사라지기 시작할지
	float FadeStartDelay = 0.0f;

	//1에서 0까지 사라지는 데 걸린 시간
	float FadeDuration = 0.0f;

	//완전히 사라지면(alpha = 0) 액터를 삭제할지
	bool bDestroyOwnerAfterFade = false;

	float GetFadeStartDelay() const { return FadeStartDelay; }
	float GetFadeDuration() const { return FadeDuration; }
	float GetFadeInStartDelay() const { return FadeInStartDelay; }
	float GetFadeInDuration() const { return FadeInDuration; }

	/**
	* Sets the decal's fade start time, duration and if the owning actor should be destroyed after the decal is fully faded out.
	* The default value of 0 for FadeStartDelay and FadeDuration makes the decal persistent. See DecalLifetimeOpacity material
	* node to control the look of "fading out."
	*
	* @param StartDelay - Time in seconds to wait before beginning to fade out the decal.
	* @param Duration - Time in second for the decal to fade out.
	* @param DestroyOwnerAfterFade - Should the owning actor automatically be destroyed after it is completely faded out.
	*/
	void SetFadeOut(float StartDelay, float Duration, bool DestroyOwnerAfterFade = true);
	void SetFadeIn(float StartDelay, float Duration);
	void SetFadeScreenSize(float NewFadeScreenSize);
	void SetDecalColor(const FLinearColor& Color);

	void SetTexture(class UTexture2D* InTexture);
	void SetTexture(const FString& InTexturePath);

	void SetDecalMaterial(UMaterial* NewDecalMaterial);
	UMaterial* GetDecalMaterial() const;

public:

	//=====================================
	//3, Decal Proxy
	struct FDeferredDecalSceneProxy* SceneProxy = nullptr;
	FDeferredDecalSceneProxy* CreateSceneProxy();

	void MarkSceneProxyDirty();
	virtual void PostEditChangeProperty(const FProperty& Property) override;
	virtual void OnTransformDirty() override;

protected:
	/** Decal material. */
	UMaterial* DecalMaterial = nullptr;

public:

	//~ Begin UActorComponent Interface
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void SetActive(bool bNewActive);
	//~ End UActorComponent Interface


	//~Begin UObject Interface
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	//~ End UObject Interface

private:
	// BeginPlay부터 흐른 시간. 에디터에서는 Tick하지 않으므로 Fade 없이 항상 보인다(UE와 같다).
	float LifeTime = 0.0f;
	// SetFadeIn/SetFadeOut을 부른 시각. UE처럼 지연은 호출한 순간부터 센다.
	float FadeInStartTime = 0.0f;
	float FadeOutStartTime = 0.0f;

	// 시간 Fade 알파. UE DecalLifetimeOpacity처럼 페이드인과 페이드아웃 중 작은 값이다.
	float ComputeLifetimeAlpha() const;
	void UpdateProxyFadeAlpha();
};