#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Script/LuaReflection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include "LevelSequence/LevelSequenceActor.h"
#include "UObject/Casts.h"
#include <sol/sol.hpp>

void RegisterLuaSequenceAPI(sol::environment& Env, UObject* Owner)
{
	// local Seq = PlaySequence(Path, [Options]) → 시퀀스를 띄워 바로 재생한다. 씬에 저장되지 않는다
	// Options(모두 생략 가능):
	//   Loop = 0              반복 횟수. -1이면 무한
	//   Rate = 1.0            재생 속도. 음수면 거꾸로
	//   StartTime = 0.0       시작 시간(초)
	//   RestoreState = true   끝나면 바꾼 값을 되돌린다
	//   DestroyOnFinish = true 끝나면 시퀀스 액터를 지운다. false면 다 쓰고 Seq:Destroy()로 지운다
	// 읽기에 실패하면 nil. 반환한 액터로 Seq:Stop(), Seq:IsPlaying() 등을 부른다
	Env.set_function("PlaySequence",
		[Owner](const FString& Path, sol::optional<sol::table> Options, sol::this_state S) -> sol::object
		{
			UWorld* World = FindGameWorldForObject(Owner);
			if (!World)
				return sol::make_object(S, sol::lua_nil);

			FMovieSceneSequencePlaybackSettings Settings;
			bool bDestroyOnFinish = true;
			if (Options)
			{
				const sol::table& Table = *Options;
				Settings.LoopCount = Table.get_or("Loop", 0);
				Settings.PlayRate = Table.get_or("Rate", 1.0f);
				Settings.StartTime = Table.get_or("StartTime", 0.0f);
				Settings.bRestoreState = Table.get_or("RestoreState", true);
				bDestroyOnFinish = Table.get_or("DestroyOnFinish", true);
			}

			ALevelSequenceActor* Actor = ALevelSequenceActor::SpawnAndPlay(World, Path, Settings, bDestroyOnFinish);
			if (!Actor)
				return sol::make_object(S, sol::lua_nil);
			return LuaReflection::MakeObject(sol::state_view(S), Actor);
		});
}
