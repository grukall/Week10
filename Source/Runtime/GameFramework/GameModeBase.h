#pragma once

#include "GameFramework/Actor.h"

class AGameStateBase;
class APlayerController;

// 게임 규칙을 맡는 액터. 플레이 월드(Game·PIE)마다 하나 스폰되고 월드와 함께 사라진다.
// 에디터 월드에는 없고 씬 파일에도 저장되지 않는다(RF_Transient).
class AGameModeBase : public AActor
{
	DECLARE_CLASS(AGameModeBase, AActor)
public:
	AGameModeBase() = default;
	virtual ~AGameModeBase() = default;

	// UE에서는 액터 초기화 중에 불린다. 여기서는 UWorld::SetGameMode가 스폰 직후 부른다.
	// GameStateClass로 GameState를 스폰하고 World에 알린다.
	void PreInitializeComponents();

	// UWorld::BeginPlay가 부른다. GameState를 거쳐 월드의 모든 액터 BeginPlay를 시작시킨다.
	virtual void StartPlay();

	AGameStateBase* GetGameState() const { return GameState; }

	// 그 번호의 PlayerController가 없으면 스폰한다 (UE UGameplayStatics::CreatePlayer).
	// 0번은 StartPlay에서 미리 만든다. 2인 플레이처럼 더 필요하면 스크립트의 EnableInput(1)이 여기서 만든다.
	APlayerController* GetOrCreatePlayerController(int32 PlayerIndex);

	UClass* GameStateClass = nullptr;
	UClass* PlayerControllerClass = nullptr;

protected:
	AGameStateBase* GameState = nullptr;
};
