#include "EnginePCH.h"
#include "GameStateBase.h"
#include "Engine/World.h"

void AGameStateBase::HandleBeginPlay()
{
	// UE는 WorldSettings->NotifyBeginPlay()를 부른다. WorldSettings가 없으므로 World에 둔다.
	if (UWorld* OwningWorld = GetWorld())
		OwningWorld->NotifyBeginPlay();
}
