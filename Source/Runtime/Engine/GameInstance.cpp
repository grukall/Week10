#include "EnginePCH.h"
#include "GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

void UGameInstance::Init()
{
	HTR_LOG(Info, "[GameInstance] Init");
}

void UGameInstance::Shutdown()
{
	HTR_LOG(Info, "[GameInstance] Shutdown");
}

AGameModeBase* UGameInstance::CreateGameModeForWorld(UWorld* InWorld)
{
	if (!InWorld)
		return nullptr;

	AGameModeBase* GameMode = InWorld->SpawnActor<AGameModeBase>();
	// GameMode는 씬 파일에 저장하지 않는다. 플레이할 때마다 새로 스폰한다.
	if (GameMode)
		GameMode->SetFlags(EObjectFlags::RF_Transient);
	return GameMode;
}
