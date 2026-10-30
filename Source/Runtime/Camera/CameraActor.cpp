#include "EnginePCH.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "../Component/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PlayerCameraManager.h"

// 카메라 컴포넌트를 기본 서브오브젝트로 만들어 루트에 연결한다.
ACameraActor::ACameraActor()
{
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	SetRootComponent(CameraComponent);
}

void ACameraActor::StartCameraShake(float Strength, float Duration, float Frequency)
{
	UWorld* OwningWorld = GetWorld();
	if (!OwningWorld)
		return;

	for (APlayerController* Controller : OwningWorld->GetPlayerControllers())
	{
		APlayerCameraManager* CameraManager = ::IsValid(Controller) ? Controller->GetCameraPlayerManager() : nullptr;
		if (CameraManager && CameraManager->GetViewTarget() == this)
			CameraManager->StartCameraShake(Strength, Duration, Frequency);
	}
}
