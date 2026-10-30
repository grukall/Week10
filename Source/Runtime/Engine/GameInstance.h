#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class UWorld;
class AGameModeBase;

// 게임 한 판(세션) 동안 살아 있는 객체. FWorldContext와 생명주기를 같이한다.
// Game 컨텍스트는 프로세스가 끝날 때까지, PIE 컨텍스트는 Play부터 Stop까지 유지된다.
// 월드를 다시 불러와도 바뀌지 않으므로 재시작 뒤에도 남아야 할 값(최고 점수 등)을 둔다.
class UGameInstance : public UObject
{
	DECLARE_CLASS(UGameInstance, UObject)
public:
	UGameInstance() = default;
	virtual ~UGameInstance() = default;

	// UEngine::CreateNewWorldContext가 만든 직후 / DestroyWorldContext가 지우기 직전에 호출한다.
	virtual void Init();
	virtual void Shutdown();

	// UE의 CreateGameModeForURL. World에 GameMode 액터를 스폰해 돌려준다.
	// TODO: 씬·프로젝트 설정에서 GameMode 클래스를 고르는 규칙. 지금은 AGameModeBase 고정이다.
	virtual AGameModeBase* CreateGameModeForWorld(UWorld* InWorld);
};
