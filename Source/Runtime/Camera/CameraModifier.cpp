#include "EnginePCH.h"
#include "Camera/CameraModifier.h"
#include "PlayerCameraManager.h"

void UCameraModifier::InitializeFor(APlayerCameraManager* Owner)
{
	CameraOwner = Owner;
}

APlayerCameraManager* UCameraModifier::GetCameraOwner() const
{
	return CameraOwner.Get();
}
