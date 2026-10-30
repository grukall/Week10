#pragma once

#include "GameFramework/Actor.h"

// 게임 진행 상태(점수, 남은 시간 등)를 담는 액터. GameMode가 스폰하고 월드와 함께 사라진다.
// 규칙은 GameMode가 정하고, 다른 액터·HUD는 GameState를 읽는다.
class AGameStateBase : public AActor
{
	DECLARE_CLASS(AGameStateBase, AActor)
public:
	AGameStateBase() = default;
	virtual ~AGameStateBase() = default;

	// GameMode::StartPlay가 부른다. 월드에 액터들의 BeginPlay를 알리게 한다.
	virtual void HandleBeginPlay();
};
