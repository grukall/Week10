#pragma once
#include "Core/EngineString.h"
#include "Core/EngineLog.h"
#include "Script/LuaCoroutineScheduler.h"


// Call이 가변 인자 템플릿이라 헤더에 구현이 있어야 하므로 sol을 여기서 include한다.
// Actor.h처럼 널리 쓰이는 헤더는 이 파일 대신 전방 선언만 둔다.
#include <sol/sol.hpp>

class UObject;

// 액터나 컴포넌트 하나에 붙는 Lua 스크립트. 스크립트 전용 environment(전역 공간)를 소유한다.
// 스크립트에서 obj는 소유 객체이고, 컴포넌트 스크립트에서는 owner로 소유 액터도 쓸 수 있다.
class FLuaScriptInstance
{
public:
	FLuaScriptInstance(UObject* InOwner, const FString& InPath);
	~FLuaScriptInstance();

	// 경로가 비어 있거나 로드에 실패하면 nullptr. 실패 원인은 Load가 로그로 남긴다.
	static std::unique_ptr<FLuaScriptInstance> CreateAndLoad(UObject* InOwner, const FString& InPath);

	// environment를 만들고 파일을 실행해 함수들을 등록한다. 실패하면 로그를 남기고 false.
	// 실패하면 이전 environment를 그대로 둔다.
	bool Load();

	// PIE 중 파일이 바뀌었을 때 다시 로드한다. 문법 에러면 이전 스크립트를 그대로 둔다.
	// 스크립트 전역 값은 새 environment에서 다시 시작하고, BeginPlay는 다시 부르지 않는다.
	// 새로 추가한 Tick, OnOverlap 같은 함수는 BeginPlay 때 등록이 끝나서 PIE를 다시 시작해야 불린다.
	bool Reload();
	bool IsLoaded() const { return bLoaded; }
	const FString& GetPath() const { return Path; }

	// 스크립트가 FuncName을 함수로 정의했는지. 예: Tick이 있을 때만 Tick 함수를 등록한다.
	bool HasFunction(const char* FuncName)
	{
		if (!bLoaded)
			return false;

		sol::object Value = Env[FuncName];
		return Value.get_type() == sol::type::function;
	}

	// 스크립트에 FuncName 함수가 있으면 호출한다. 없으면 아무것도 하지 않는다.
	// Lua 쪽 에러는 로그만 남기고 엔진은 계속 돈다.
	template <typename... Args>
	void Call(const char* FuncName, Args&&... InArgs)
	{
		if (!bLoaded)
			return;

		sol::protected_function Func = Env[FuncName];
		if (!Func.valid())
			return;

		sol::protected_function_result Result = Func(std::forward<Args>(InArgs)...);
		if (!Result.valid())
		{
			sol::error Err = Result;
			HTR_LOG(Error, "[Lua] {} : {}() 실패 : {}", Path, FuncName, Err.what());
		}
	}

	// Frame 마다 한 번 호출
	void TickCoroutines(float Dt)
	{
		if (bLoaded) Scheduler.Tick(Dt);
	}

	// 소유 객체가 EndPlay될 때 남은 코루틴을 모두 멈추기
	void StopCoroutines() { Scheduler.StopAll(); }

private:
	UObject* Owner = nullptr;
	FString Path;
	sol::environment Env;
	// 멤버는 선언의 역순으로 파괴되므로 Env 다음에 선언
	FLuaCoroutineScheduler Scheduler;
	bool bLoaded = false;
};
