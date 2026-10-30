#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include "UObject/Casts.h"
#include <sol/sol.hpp>

UWorld* FindGameWorldForObject(UObject* Object)
{
	AActor* Actor = Cast<AActor>(Object);
	if (!Actor)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
			Actor = Component->GetOwner();
	}

	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
		return nullptr;
	return World;
}

void RegisterLuaBindings(sol::state& Lua)
{
	// 리플렉션이 FVector·FRotator 등을 돌려주므로 수학 타입을 먼저 등록한다.
	// 액터·컴포넌트는 모두 FLuaObjectRef 핸들 하나로 넘어가므로 액터 전용 usertype은 없다.
	RegisterLuaMathBindings(Lua);
	RegisterLuaObjectBindings(Lua);
	RegisterLuaInputBindings(Lua);
	RegisterLuaGameBindings(Lua);
	RegisterLuaAudioBindings(Lua);
}
