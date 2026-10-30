#pragma once
#include "EnginePCH.h"
#include "SceneComponent.h"
#include "Render/Material.h"
#include "Math/Vector.h"
#include "Math/Color.h"

class FLineBatcher;

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(DecalColor)
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

//	//공간 Fade
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	float FadeScreenSize;
//
//
//	//시간 Fade
//	/**
//	* Time in seconds to wait before beginning to fade out the decal. Set fade duration and start delay to 0 to make persistent.
//	*/
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	float FadeStartDelay;
//
//	/**
//	* Time in seconds for the decal to fade out. Set fade duration and start delay to 0 to make persistent. Only fades in active simulation or game.
//	*/
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	float FadeDuration;
//
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	float FadeInDuration;
//
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	float FadeInStartDelay;
//
//	/**
//	* Automatically destroys the owning actor after fully fading out.
//	*/
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	uint8 bDestroyOwnerAfterFade : 1;
//
//	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Decal")
//	ENGINE_API float GetFadeStartDelay() const;
//
//	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Decal")
//	ENGINE_API float GetFadeDuration() const;
//
//	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Decal")
//	ENGINE_API float GetFadeInStartDelay() const;
//
//	UFUNCTION(BlueprintCallable, Category = "Rendering|Components|Decal")
//	ENGINE_API float GetFadeInDuration() const;
//
//
//	
//public:
//
//	//=====================================
//	//3, Decal Proxy
//	/** The decal proxy. */
//	FDeferredDecalProxy* SceneProxy;
//
//
//protected:
//	/** Decal material. */
//	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Decal)
//	TObjectPtr<class UMaterialInterface> DecalMaterial;
//
//	UMaterial* DecalMaterial;
//
//
//public:
//
//	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const
//	{
//		return (ElementIndex == 0) ? DecalMaterial : NULL;
//	}
//	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
//	{
//		if (ElementIndex == 0)
//		{
//			SetDecalMaterial(InMaterial);
//		}
//	}
//
//	//~ Begin UActorComponent Interface
//	virtual void InitializeComponent() override;
//	virtual void BeginPlay() override;
//	virtual void EndPlay() override;
//	virtual void TickComponent(float DeltaTime) override;
//	virtual void SetActive(bool bNewActive);
//	//~ End UActorComponent Interface
//
//
//	//~Begin UObject Interface
//	virtual void Serialize(FStructuredArchive::FRecord Record) override;
//	//~ End UObject Interface

};