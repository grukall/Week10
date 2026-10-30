#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Script/LuaReflection.h"
#include "Engine/World.h"
#include "Engine/EffectManager.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include "UObject/Casts.h"
#include <sol/sol.hpp>

namespace
{
	const FLinearColor DefaultTextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// 스크립트 주인의 월드 이펙트 매니저. 게임 월드가 아니면 nullptr
	FEffectManager* FindEffectManagerForObject(UObject* Object)
	{
		UWorld* World = FindGameWorldForObject(Object);
		return World ? &World->GetEffectManager() : nullptr;
	}

	// RegisterEffect 테이블 값 하나. 키 → 필드 규칙은 ApplyEffectDescField가 시퀀스 Effects와 같이 쓴다.
	struct FLuaEffectValue : IEffectValueReader
	{
		explicit FLuaEffectValue(const sol::object& InValue) : Value(InValue) {}
		bool ReadNumber(float& Out) const override { if (!Value.is<float>()) return false; Out = Value.as<float>(); return true; }
		bool ReadBool(bool& Out) const override { if (!Value.is<bool>()) return false; Out = Value.as<bool>(); return true; }
		bool ReadString(FString& Out) const override { if (!Value.is<std::string>()) return false; Out = Value.as<std::string>(); return true; }
		bool ReadVector(FVector& Out) const override { if (!Value.is<FVector>()) return false; Out = Value.as<FVector>(); return true; }
		const sol::object& Value;
	};

	// 테이블의 키를 FEffectDesc 필드에 넣는다. 타입이 틀리면 어느 키인지 알려 주는 Lua 에러를 낸다.
	// 모르는 키는 오타일 가능성이 커서 경고만 하고 넘어간다.
	void ApplyEffectTable(const FString& Effect, const sol::table& Table, FEffectDesc& Desc)
	{
		for (const auto& [KeyObject, Value] : Table)
		{
			if (!KeyObject.is<std::string>())
				continue;

			const std::string Key = KeyObject.as<std::string>();
			if (Key == "Base")
				continue;

			FString Error;
			bool bUnknownKey = false;
			if (ApplyEffectDescField(Desc, Key, FLuaEffectValue(Value), Error, bUnknownKey))
				continue;
			if (bUnknownKey)
				HTR_LOG(Warning, "[Lua] RegisterEffect({}): unknown key '{}' ignored", Effect, Key);
			else
				throw sol::error(std::format("RegisterEffect({}): {}", Effect, Error));
		}
	}
}

void RegisterLuaEffectAPI(sol::environment& Env, UObject* Owner)
{
	// local h = SpawnEffect(name, location [, rotation [, loop [, duration]]])
	// 핸들은 숫자다. 실패하면 -1
	Env.set_function("SpawnEffect",
		[Owner](const FString& Name, const FVector& Location, sol::optional<FRotator> Rotation,
			sol::optional<bool> bLoop, sol::optional<float> Duration) -> int64
		{
			FEffectManager* Effects = FindEffectManagerForObject(Owner);
			if (!Effects)
				return -1;
			return Effects->SpawnEffect(FName(Name), Location, Rotation.value_or(FRotator()),
				bLoop.value_or(false), Duration.value_or(0.0f)).Pack();
		});

	// local h = SpawnEffectAttached(name, actor [, offset [, loop [, duration [, rotation]]]])
	// actor의 루트에 붙어 따라다닌다. offset·rotation은 루트 기준 상대값
	Env.set_function("SpawnEffectAttached",
		[Owner](const FString& Name, sol::object Target, sol::optional<FVector> Offset,
			sol::optional<bool> bLoop, sol::optional<float> Duration, sol::optional<FRotator> Rotation) -> int64
		{
			FEffectManager* Effects = FindEffectManagerForObject(Owner);
			if (!Effects)
				return -1;

			AActor* TargetActor = Target.is<FLuaObjectRef>() ? Cast<AActor>(Target.as<FLuaObjectRef>().Weak.Get()) : nullptr;
			if (!TargetActor)
			{
				HTR_LOG(Warning, "[Lua] SpawnEffectAttached({}): target is not a live actor", Name);
				return -1;
			}

			FTransform Relative;
			Relative.Location = Offset.value_or(FVector(0.0f, 0.0f, 0.0f));
			Relative.Rotation = Rotation.value_or(FRotator());
			return Effects->SpawnEffectAttached(FName(Name), TargetActor, Relative,
				bLoop.value_or(false), Duration.value_or(0.0f)).Pack();
		});

	// SpawnTextEffect(text, location [, color [, duration]])
	Env.set_function("SpawnTextEffect",
		[Owner](const FString& Text, const FVector& Location, sol::optional<FLinearColor> Color, sol::optional<float> Duration)
		{
			if (FEffectManager* Effects = FindEffectManagerForObject(Owner))
				Effects->SpawnTextEffect(Text, Location, Color.value_or(DefaultTextColor), Duration.value_or(1.0f));
		});

	// StopEffect(h [, immediate]) : immediate가 아니면 방출만 멈추고 남은 입자가 사라진 뒤 정리된다
	Env.set_function("StopEffect",
		[Owner](int64 Handle, sol::optional<bool> bImmediate)
		{
			if (FEffectManager* Effects = FindEffectManagerForObject(Owner))
				Effects->StopEffect(FEffectHandle::Unpack(Handle), bImmediate.value_or(false));
		});

	// IsEffectActive(h) : 아직 재생 중이거나 남은 입자가 사라지는 중이면 true
	Env.set_function("IsEffectActive",
		[Owner](int64 Handle) -> bool
		{
			FEffectManager* Effects = FindEffectManagerForObject(Owner);
			return Effects && Effects->IsEffectActive(FEffectHandle::Unpack(Handle));
		});

	// RegisterEffect(name, { Base = "HitSplash", ParticleCount = 20, MinVelocity = FVector(...), ... })
	// 키 이름은 FEffectDesc 필드와 같다. Base가 있으면 그 프리셋에서 시작하고, 없으면 기본값에서 시작한다.
	// 같은 이름이 있으면 덮어쓴다. 프리셋은 플레이가 끝나도 남는다.
	Env.set_function("RegisterEffect",
		[Owner](const FString& Name, const sol::table& Table)
		{
			FEffectManager* Effects = FindEffectManagerForObject(Owner);
			if (!Effects)
				return;

			FEffectDesc Desc;
			if (sol::optional<std::string> Base = Table["Base"])
			{
				const FEffectDesc* BaseDesc = Effects->FindEffect(FName(*Base));
				if (!BaseDesc)
					throw sol::error(std::format("RegisterEffect({}): Base '{}' 프리셋이 없습니다", Name, *Base));
				Desc = *BaseDesc;
			}

			ApplyEffectTable(Name, Table, Desc);
			Effects->RegisterEffect(FName(Name), Desc);
		});
}
