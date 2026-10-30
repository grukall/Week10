#include "EnginePCH.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "CameraTypes.h"

UCameraModifier_CameraShake::~UCameraModifier_CameraShake()
{
	RemoveAllCameraShakes();
}

bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	for (int32 Index = 0; Index < ActiveShakes.Num();)
	{
		UCameraShakeBase* Shake = ActiveShakes[Index].Get();
		if (Shake)
			Shake->UpdateAndApplyCameraShake(DeltaTime, 1.0f, InOutPOV);
		if (!Shake || Shake->IsFinished())
		{
			ActiveShakes.RemoveAt(Index);
			delete Shake;
		}
		else
			++Index;
	}
	return false;
}

UCameraShakeBase* UCameraModifier_CameraShake::AddCameraShake(
	UClass* ShakeClass, float Scale, ECameraShakePlaySpace Space)
{
	if (!ShakeClass || !ShakeClass->IsChildOf(UCameraShakeBase::StaticClass()))
		return nullptr;
	return AddCameraShake(NewObject<UCameraShakeBase>(this, ShakeClass, NAME_None,
		EObjectFlags::RF_Transient), Scale, Space);
}

UCameraShakeBase* UCameraModifier_CameraShake::AddCameraShake(
	UCameraShakeBase* Shake, float Scale, ECameraShakePlaySpace Space)
{
	if (!Shake)
		return nullptr;
	bool bAlreadyAdded = false;
	for (const auto& Active : ActiveShakes)
		bAlreadyAdded |= Active.Get() == Shake;
	if (!bAlreadyAdded)
		ActiveShakes.Add(TWeakObjectPtr<UCameraShakeBase>(Shake));
	Shake->StartShake(Scale, Space);
	return Shake;
}

void UCameraModifier_CameraShake::RemoveCameraShake(UCameraShakeBase* Shake)
{
	for (int32 Index = 0; Index < ActiveShakes.Num(); ++Index)
	{
		if (ActiveShakes[Index].Get() == Shake)
		{
			ActiveShakes.RemoveAt(Index);
			delete Shake;
			return;
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakes()
{
	const auto Shakes = ActiveShakes;
	ActiveShakes.Empty();
	for (const auto& Shake : Shakes)
		delete Shake.Get();
}
