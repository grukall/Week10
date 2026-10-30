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

	//공간 Fade
	float FadeScreenSize = 0.0f;


	//시간 Fade
	/**
	* Time in seconds to wait before beginning to fade out the decal. Set fade duration and start delay to 0 to make persistent.
	*/
	float FadeStartDelay = 0.0f;

	/**
	* Time in seconds for the decal to fade out. Set fade duration and start delay to 0 to make persistent. Only fades in active simulation or game.
	*/
	float FadeDuration = 0.0f;
	float FadeInDuration = 0.0f;
	float FadeInStartDelay = 0.0f;

	/**
	* Automatically destroys the owning actor after fully fading out.
	*/
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

};