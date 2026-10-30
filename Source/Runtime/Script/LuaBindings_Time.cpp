#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include "UObject/Casts.h"
#include <sol/sol.hpp>


void RegisterLuaTimeAPI(sol::environment& Env, UObject* Owner)
{
	// HitStop(Duration, Dilation = 0): 실제 시간으로 Duration 동안 게임 시간을 멈춘다(또는 Dilation배로 늦춘다).
	Env.set_function("HitStop",
		[Owner](float Duration, sol::optional<float> Dilation)
		{
			if (UWorld* World = FindGameWorldForObject(Owner))
				World->StartHitStop(Duration, Dilation.value_or(0.0f));
		});

	// Slomo(Dilation, Duration): 0.1초에 걸쳐 Dilation배로 내려가 Duration 동안 유지한 뒤 돌아온다.
	Env.set_function("Slomo",
		[Owner](float Dilation, float Duration)
		{
			if (UWorld* World = FindGameWorldForObject(Owner))
				World->StartSlomo(Dilation, Duration);
		});

	// 기본 배율. 슬로모·히트스톱은 이 값 위에 곱해진다.
	Env.set_function("SetTimeDilation",
		[Owner](float Dilation)
		{
			if (UWorld* World = FindGameWorldForObject(Owner))
				World->SetTimeDilation(Dilation);
		});

	// 이번 프레임에 실제로 적용되는 최종 배율. 게임 월드가 아니면 1
	Env.set_function("GetTimeDilation",
		[Owner]()
		{
			UWorld* World = FindGameWorldForObject(Owner);
			return World ? World->GetTimeDilation() : 1.0f;
		});
}
