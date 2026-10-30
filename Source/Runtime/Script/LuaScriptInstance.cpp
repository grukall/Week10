#include "EnginePCH.h"
#include "Script/LuaScriptInstance.h"
#include "Script/LuaScriptManager.h"
#include "Script/LuaReflection.h"
#include "Script/LuaBindings.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"

FLuaScriptInstance::FLuaScriptInstance(UObject* InOwner, const FString& InPath)
	: Owner(InOwner), Path(InPath)
{
}

// Env는 Lua VM 안의 테이블을 참조로 붙잡고 있다. VM(FLuaScriptManager)보다 먼저 파괴되어야 한다.
FLuaScriptInstance::~FLuaScriptInstance() = default;

std::unique_ptr<FLuaScriptInstance> FLuaScriptInstance::CreateAndLoad(UObject* InOwner, const FString& InPath)
{
	if (InPath.empty())
		return nullptr;

	auto Instance = std::make_unique<FLuaScriptInstance>(InOwner, InPath);
	if (!Instance->Load())
		return nullptr;

	return Instance;
}

bool FLuaScriptInstance::Load()
{
	sol::state& Lua = FLuaScriptManager::Get().GetState();

	// 새 environment에 먼저 실행해 보고, 성공했을 때만 Env를 바꾼다.
	// 리로드가 실패해도 지금 돌던 스크립트는 그대로 남는다.
	// 못 찾은 이름은 전역(print, math, 바인딩된 타입)에서 찾도록 globals를 fallback으로 둔다.
	sol::environment NewEnv(Lua, sol::create, Lua.globals());

	// 실제 클래스에 맞는 usertype으로 넘긴다. 액터는 AActor, 컴포넌트는 UObject로 넘어간다.
	NewEnv["obj"] = LuaReflection::MakeObject(Lua, Owner);
	if (UActorComponent* Component = Cast<UActorComponent>(Owner))
		NewEnv["owner"] = LuaReflection::MakeObject(Lua, Component->GetOwner());

	// start, stop, wait Script 을 Env에 등록
	Scheduler.RegisterLuaAPI(NewEnv);
	RegisterLuaHUDAPI(NewEnv, Owner);
	RegisterLuaInputAPI(NewEnv, Owner);
	RegisterLuaEffectAPI(NewEnv, Owner);
	RegisterLuaTimeAPI(NewEnv, Owner);
	RegisterLuaCameraAPI(NewEnv, Owner);
	RegisterLuaSequenceAPI(NewEnv, Owner);

	sol::protected_function_result Result = Lua.safe_script_file(Path, NewEnv, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Err = Result;
		HTR_LOG(Error, "[Lua] {} 로드 실패 : {}", Path, Err.what());

		// 실행 도중 시작된 Coroutine이 NewEnv를 붙잡고 있을 수 있으므로 정리
		Scheduler.StopAll();
		return false;
	}

	Env = NewEnv;
	bLoaded = true;
	return true;
}

bool FLuaScriptInstance::Reload()
{
	sol::state& Lua = FLuaScriptManager::Get().GetState();

	// 컴파일만 해 보고 실행은 하지 않는다. 문법 에러면 코루틴도 멈추지 않고 지금 스크립트를 그대로 둔다.
	sol::load_result Check = Lua.load_file(Path);
	if (!Check.valid())
	{
		sol::error Err = Check;
		HTR_LOG(Error, "[Lua] {} 리로드 실패, 이전 스크립트 유지 : {}", Path, Err.what());
		return false;
	}

	// 코루틴은 옛 함수와 옛 Env를 붙잡고 있으므로 새 스크립트를 올리기 전에 멈춘다
	Scheduler.StopAll();

	if (!Load())
		return false;

	HTR_LOG(Info, "[Lua] {} 리로드", Path);
	return true;
}
