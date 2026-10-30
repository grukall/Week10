#include "EnginePCH.h"
#include "PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/WaveCameraShake.h"
#include "Camera/PerlinNoiseCameraShake.h"

bool TryParseViewTargetBlendFunction(const FString& Name, EViewTargetBlendFunction& OutFunction)
{
	if (Name == "Linear")         OutFunction = EViewTargetBlendFunction::Linear;
	else if (Name == "Cubic")     OutFunction = EViewTargetBlendFunction::Cubic;
	else if (Name == "EaseIn")    OutFunction = EViewTargetBlendFunction::EaseIn;
	else if (Name == "EaseOut")   OutFunction = EViewTargetBlendFunction::EaseOut;
	else if (Name == "EaseInOut") OutFunction = EViewTargetBlendFunction::EaseInOut;
	else return false;
	return true;
}

APlayerCameraManager::~APlayerCameraManager()
{
	ClearCameraModifiers();
}

void APlayerCameraManager::EndPlay()
{
	ClearCameraModifiers();
	Super::EndPlay();
}

void APlayerCameraManager::InitializeFor(APlayerController* PC)
{
	PCOwner = PC;
	GetCameraShakeModifier();
	AActor* Initial = GetWorld() ? GetWorld()->GetMainCamera() : nullptr;
	SetViewTarget(Initial ? Initial : PC);
	UpdateCamera(0.0f);
}

void APlayerCameraManager::SetViewTarget(AActor* NewTarget, const FViewTargetTransitionParams& TransitionParams)
{
	if (TransitionParams.BlendTime > 0.0f)
	{
		// 블렌드 도중 다시 블렌드하면, 지금 화면에 보이던 섞인 값을 새 출발점으로 고정한다
		// 이렇게 안 하면 출발점이 원래 ViewTarget으로 돌아가 화면이 튄다
		const bool bWasBlending = PendingViewTarget.Target.Get() != nullptr;
		if (bWasBlending)
		{
			ViewTarget.POV = CameraCachePOV;
			ViewTarget.Target = nullptr;
		}

		// 블렌드: 도착 대상은 Pending에 두고, 다 섞이면 UpdateCamera에서 ViewTarget으로 넘긴다
		PendingViewTarget.Target = NewTarget;
		BlendParams = TransitionParams;
		BlendParams.bLockOutgoing |= bWasBlending;
		BlendTimeToGo = TransitionParams.BlendTime;
	}
	else
	{
		// 컷: 바로 교체하고, 진행 중이던 블렌드는 취소
		ViewTarget.Target = NewTarget;
		PendingViewTarget.Target = nullptr;
		BlendTimeToGo = 0.0f;
	}
}

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	const bool bBlending = PendingViewTarget.Target.Get() != nullptr;

	// bLockOutgoing이면 블렌드 중엔 떠나는 쪽을 갱신하지 않아 전환 시작 순간의 POV가 남는다
	if (!bBlending || !BlendParams.bLockOutgoing)
		UpdateViewTarget(ViewTarget, DeltaTime);

	FMinimalViewInfo NewPOV = ViewTarget.POV;
	if (bBlending)
	{
		UpdateViewTarget(PendingViewTarget, DeltaTime);

		BlendTimeToGo -= DeltaTime;
		const float T = FMath::Clamp(1.0f - BlendTimeToGo / BlendParams.BlendTime, 0.0f, 1.0f);
		NewPOV.BlendViewInfo(PendingViewTarget.POV, GetBlendAlpha(T));

		// 다 섞였으면 도착 대상을 현재 대상으로 넘긴다
		if (BlendTimeToGo <= 0.0f)
		{
			ViewTarget = PendingViewTarget;
			PendingViewTarget.Target = nullptr;
			BlendTimeToGo = 0.0f;
			NewPOV = ViewTarget.POV;
		}
	}

	// Modifier(쉐이크 등)는 블렌드까지 끝난 최종 POV에 프레임당 한 번만 적용한다.
	// ViewTarget마다 적용하면 블렌드 중에 쉐이크 시간이 두 번 흐른다
	ApplyCameraModifiers(DeltaTime, NewPOV);

	if (bEnableFading && bAutoAnimateFade)
	{
		FadeTimeRemaining = FMath::Max(FadeTimeRemaining - DeltaTime, 0.0f);
		if (FadeTime > 0.0f)
			FadeAmount = FadeAlpha.X + ((1.0f - FadeTimeRemaining / FadeTime) * (FadeAlpha.Y - FadeAlpha.X));

		if (!bHoldFadeWhenFinished && FadeTimeRemaining <= 0.0f)
			StopCameraFade();
	}

	FillCameraCache(NewPOV);
}

void APlayerCameraManager::UpdateViewTarget(FViewTarget& OutVT, float DeltaTime)
{
	// 1. 기본값으로 리셋
	OutVT.POV = FMinimalViewInfo();
	OutVT.POV.Projection.FovDegrees = DefaultFOV;
	OutVT.POV.AspectRatio = DefaultAspectRatio;
	OutVT.POV.bConstrainAspectRatio = bDefaultConstrainAspectRatio;

	// 2. Target의 시점 계산
	AActor* Target = OutVT.Target.Get();
	if (!Target)
		return;

	//타겟이 CameraActor면 CameraComponent에서 계산하고 아니면 액터에 붙은 CameraComponent에서 계산
	if (ACameraActor* CamActor = Cast<ACameraActor>(Target))
		CamActor->GetCameraComponent()->GetCameraView(DeltaTime, OutVT.POV);
	else
		Target->CalcCamera(DeltaTime, OutVT.POV);     

}

UCameraModifier* APlayerCameraManager::AddNewCameraModifier(UClass* ModifierClass)
{
	if (!ModifierClass || !ModifierClass->IsChildOf(UCameraModifier::StaticClass()))
		return nullptr;
	UCameraModifier* Modifier = NewObject<UCameraModifier>(this, ModifierClass,
		NAME_None, EObjectFlags::RF_Transient);
	if (!Modifier)
		return nullptr;
	Modifier->InitializeFor(this);
	int32 Index = 0;
	while (Index < ModifierList.Num())
	{
		const UCameraModifier* Existing = ModifierList[Index].Get();
		if (Existing && Existing->Priority > Modifier->Priority)
			break;
		++Index;
	}
	ModifierList.Insert(TWeakObjectPtr<UCameraModifier>(Modifier), Index);
	if (auto* ShakeModifier = Cast<UCameraModifier_CameraShake>(Modifier))
		CachedCameraShakeMod = ShakeModifier;
	return Modifier;
}

void APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier)
{
	for (int32 Index = 0; Index < ModifierList.Num(); ++Index)
	{
		if (ModifierList[Index].Get() == Modifier)
		{
			ModifierList.RemoveAt(Index);
			if (CachedCameraShakeMod.Get() == Modifier)
				CachedCameraShakeMod.Reset();
			delete Modifier;
			return;
		}
	}
}

void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const auto Modifiers = ModifierList;
	for (const auto& Handle : Modifiers)
	{
		UCameraModifier* Modifier = Handle.Get();
		if (Modifier && !Modifier->IsDisabled() && Modifier->ModifyCamera(DeltaTime, InOutPOV))
			break;
	}
}

UCameraModifier_CameraShake* APlayerCameraManager::GetCameraShakeModifier()
{
	if (!CachedCameraShakeMod.IsValid())
		AddNewCameraModifier(UCameraModifier_CameraShake::StaticClass());
	return CachedCameraShakeMod.Get();
}

UCameraShakeBase* APlayerCameraManager::StartCameraShake(
	UClass* ShakeClass, float Scale, ECameraShakePlaySpace Space)
{
	UCameraModifier_CameraShake* Modifier = GetCameraShakeModifier();
	return Modifier ? Modifier->AddCameraShake(ShakeClass, Scale, Space) : nullptr;
}

UCameraShakeBase* APlayerCameraManager::StartCameraShake(float Strength, float Duration, float Frequency)
{
	UCameraModifier_CameraShake* Modifier = GetCameraShakeModifier();
	if (!Modifier)
		return nullptr;
	UWaveCameraShake* Shake = NewObject<UWaveCameraShake>(Modifier);
	if (!Shake)
		return nullptr;
	Shake->Configure(Strength, Duration, Frequency);
	return Modifier->AddCameraShake(Shake);
}

UCameraShakeBase* APlayerCameraManager::StartPerlinNoiseCameraShake(float Strength, float Duration, float Frequency)
{
	UCameraModifier_CameraShake* Modifier = GetCameraShakeModifier();
	if (!Modifier)
		return nullptr;
	UPerlinNoiseCameraShake* Shake = NewObject<UPerlinNoiseCameraShake>(Modifier);
	if (!Shake)
		return nullptr;
	Shake->Configure(Strength, Duration, Frequency);
	return Modifier->AddCameraShake(Shake);
}

void APlayerCameraManager::StopCameraShake(UCameraShakeBase* Shake)
{
	if (auto* Modifier = CachedCameraShakeMod.Get())
		Modifier->RemoveCameraShake(Shake);
}

void APlayerCameraManager::StopAllCameraShakes()
{
	if (auto* Modifier = CachedCameraShakeMod.Get())
		Modifier->RemoveAllCameraShakes();
}

void APlayerCameraManager::ClearCameraModifiers()
{
	const auto Modifiers = ModifierList;
	ModifierList.Empty();
	CachedCameraShakeMod.Reset();
	for (const auto& Modifier : Modifiers)
		delete Modifier.Get();
}

void APlayerCameraManager::FillCameraCache(const FMinimalViewInfo& InCameraCachePov)
{
	CameraCachePOV = InCameraCachePov;


}

float APlayerCameraManager::GetBlendAlpha(float T) const
{
	const float Exp = BlendParams.BlendExp;
	switch (BlendParams.BlendFunction)
	{
	case EViewTargetBlendFunction::Linear:
		return T;
	case EViewTargetBlendFunction::Cubic:
		// 3t² - 2t³
		return T * T * (3.0f - 2.0f * T);
	case EViewTargetBlendFunction::EaseIn:
		return powf(T, Exp);
	case EViewTargetBlendFunction::EaseOut:
		return powf(T, 1.0f / Exp);
	case EViewTargetBlendFunction::EaseInOut:
		// 언리얼 InterpEaseInOut(0, 1, T, Exp)
		return T < 0.5f ? 0.5f * powf(2.0f * T, Exp)
		                : 1.0f - 0.5f * powf(2.0f * (1.0f - T), Exp);
	}
	return T;
}

void APlayerCameraManager::StartCameraFade(float FromAlpha, float ToAlpha, float InFadeTime, FLinearColor InFadeColor, bool bInHoldWhenFinished)
{
	bEnableFading = true;
	FadeColor = InFadeColor;
	FadeAlpha = FVector2(FromAlpha, ToAlpha);
	FadeTime = InFadeTime;
	FadeTimeRemaining = InFadeTime;
	bAutoAnimateFade = true;
	bHoldFadeWhenFinished = bInHoldWhenFinished;
}

void APlayerCameraManager::StopCameraFade()
{
	if (bEnableFading)
	{
		FadeAmount = FadeAlpha.Y;
		bEnableFading = false;
	}
}

void APlayerCameraManager::SetManualCameraFade(float InFadeAmount, FLinearColor InFadeColor)
{
	bEnableFading = true;
	FadeColor = InFadeColor;
	FadeAmount = InFadeAmount;
	bAutoAnimateFade = false;
	FadeTimeRemaining = 0.0f;
}
