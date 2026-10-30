#include "EnginePCH.h"
#include "GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void AGameModeBase::PreInitializeComponents()
{
	if (!GameStateClass)
		GameStateClass = AGameStateBase::StaticClass();

	UWorld* OwningWorld = GetWorld();
	if (!OwningWorld)
		return;

	GameState = Cast<AGameStateBase>(OwningWorld->SpawnActor(GameStateClass));
	if (!GameState)
	{
		HTR_LOG(Error, "[GameMode] failed to spawn GameState");
		return;
	}
	// GameMode와 마찬가지로 씬 파일에 저장하지 않는다.
	GameState->SetFlags(EObjectFlags::RF_Transient);
	OwningWorld->SetGameState(GameState);
}

void AGameModeBase::StartPlay()
{
	// 액터들이 BeginPlay에서 EnableInput(0)을 부를 수 있도록 먼저 만든다
	GetOrCreatePlayerController(0);

	if (GameState)
		GameState->HandleBeginPlay();

}

APlayerController* AGameModeBase::GetOrCreatePlayerController(int32 PlayerIndex)
{
	UWorld* OwningWorld = GetWorld();
	if (!OwningWorld || PlayerIndex < 0)
		return nullptr;

	if (APlayerController* Existing = OwningWorld->GetPlayerController(PlayerIndex))
		return Existing;

	if (!PlayerControllerClass)
		PlayerControllerClass = APlayerController::StaticClass();

	APlayerController* Controller = Cast<APlayerController>(OwningWorld->SpawnActor(PlayerControllerClass));
	if (!Controller)
	{
		HTR_LOG(Error, "[GameMode] failed to spawn PlayerController {}", PlayerIndex);
		return nullptr;
	}
	// GameMode와 마찬가지로 씬 파일에 저장하지 않는다.
	Controller->SetFlags(EObjectFlags::RF_Transient);
	Controller->SetPlayerIndex(PlayerIndex);
	//PCM 생성
	Controller->SpawnPlayerCameraManager();
	OwningWorld->AddPlayerController(Controller);
	return Controller;
}
