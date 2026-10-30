#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Script/LuaReflection.h"
#include "Input/InputSystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputComponent.h"
#include "Component/ActorComponent.h"

#include <sol/sol.hpp>

namespace
{
	EKeyCode ToKeyCode(const std::string& Name)
	{
		EKeyCode Key;
		if (!FInputSystem::FindKeyByName(Name, Key))
			throw sol::error(std::format("Input : 알 수 없는 키 이름 '{}'", Name));
		return Key;
	}

	EInputEvent ToInputEvent(const std::string& Name)
	{
		if (Name == "Pressed")
			return EInputEvent::Pressed;
		if (Name == "Released")
			return EInputEvent::Released;
		throw sol::error(std::format("Input : 이벤트는 \"Pressed\" 또는 \"Released\"입니다 ('{}')", Name));
	}

	// 스크립트가 붙은 액터. 컴포넌트 스크립트면 소유 액터.
	AActor* GetOwningActor(UObject* Owner)
	{
		if (AActor* Actor = Cast<AActor>(Owner))
			return Actor;
		if (UActorComponent* Component = Cast<UActorComponent>(Owner))
			return Component->GetOwner();
		return nullptr;
	}

	// Lua에 넘기는 InputComponent 참조. 액터가 파괴된 뒤 쓰면 크래시 대신 Lua 에러가 나도록 액터를 약한 참조로 든다.
	struct FLuaInputRef
	{
		FWeakObjectPtr Actor;

		FInputComponent& Resolve() const
		{
			AActor* Owner = Cast<AActor>(Actor.Get());
			if (!Owner || !Owner->GetInputComponent())
				throw sol::error("InputComponent의 액터가 이미 파괴됐습니다.");
			return *Owner->GetInputComponent();
		}
	};

	// Lua 함수를 엔진 콜백으로 감싼다. Lua 쪽 에러는 로그만 남기고 엔진은 계속 돈다.
	template <typename... Args>
	std::function<void(Args...)> WrapLuaCallback(sol::protected_function Func, FString What)
	{
		return [Func = std::move(Func), What = std::move(What)](Args... InArgs)
		{
			sol::protected_function_result Result = Func(InArgs...);
			if (!Result.valid())
			{
				sol::error Err = Result;
				HTR_LOG(Error, "[Lua] Input {} 실패 : {}", What, Err.what());
			}
		};
	}

	APlayerController* FindOrCreatePlayerController(AActor* Actor, int32 PlayerIndex)
	{
		UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		if (!World || !World->IsGameWorld())
			return nullptr;

		if (APlayerController* Controller = World->GetPlayerController(PlayerIndex))
			return Controller;
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
			return GameMode->GetOrCreatePlayerController(PlayerIndex);
		return nullptr;
	}
}

void RegisterLuaInputBindings(sol::state& Lua)
{
	// 키 상태를 직접 읽는 함수는 두지 않는다. 입력은 뷰포트 → PlayerController → InputComponent로만 들어온다.
	//   local Input = EnableInput(0)
	//   Input:BindAxis("MoveForward", function(Value) ... end)
	//   Input:BindAction("Jump", "Pressed", function() ... end)
	//   Input:BindKey("Space", "Released", function() ... end)
	Lua.new_usertype<FLuaInputRef>("InputComponent",
		sol::no_constructor,
		"BindAxis", [](const FLuaInputRef& Ref, const std::string& AxisName, sol::protected_function Func)
		{
			Ref.Resolve().BindAxis(FName(AxisName), WrapLuaCallback<float>(std::move(Func), "BindAxis " + AxisName));
		},
		"BindAction", [](const FLuaInputRef& Ref, const std::string& ActionName, const std::string& Event, sol::protected_function Func)
		{
			Ref.Resolve().BindAction(FName(ActionName), ToInputEvent(Event), WrapLuaCallback<>(std::move(Func), "BindAction " + ActionName));
		},
		"BindKey", [](const FLuaInputRef& Ref, const std::string& Key, const std::string& Event, sol::protected_function Func)
		{
			Ref.Resolve().BindKey(ToKeyCode(Key), ToInputEvent(Event), WrapLuaCallback<>(std::move(Func), "BindKey " + Key));
		},
		"ClearBindings", [](const FLuaInputRef& Ref) { Ref.Resolve().ClearBindings(); });
}

void RegisterLuaInputAPI(sol::environment& Env, UObject* Owner)
{
	// local Input = EnableInput([PlayerIndex = 0])
	// 이 액터가 그 플레이어의 입력을 받기 시작한다. 플레이 월드가 아니면 nil.
	Env.set_function("EnableInput",
		[Owner](sol::optional<int32> PlayerIndex, sol::this_state S) -> sol::object
		{
			AActor* Actor = GetOwningActor(Owner);
			APlayerController* Controller = FindOrCreatePlayerController(Actor, PlayerIndex.value_or(0));
			if (!Controller || !Actor->EnableInput(Controller))
				return sol::make_object(S, sol::lua_nil);
			return sol::make_object(S, FLuaInputRef{ FWeakObjectPtr(Actor) });
		});

	Env.set_function("DisableInput",
		[Owner]()
		{
			if (AActor* Actor = GetOwningActor(Owner))
				Actor->DisableInput();
		});

	// local PC = GetPlayerController([PlayerIndex = 0])
	// PC:AddAxisMapping("MoveForward", "W", 1.0) / PC:AddActionMapping("Jump", "Space")
	Env.set_function("GetPlayerController",
		[Owner](sol::optional<int32> PlayerIndex, sol::this_state S) -> sol::object
		{
			APlayerController* Controller = FindOrCreatePlayerController(GetOwningActor(Owner), PlayerIndex.value_or(0));
			return LuaReflection::MakeObject(sol::state_view(S), Controller);
		});
}
