#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Script/LuaReflection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include "Component/ActorComponent.h"
#include "Component/SceneComponent.h"
#include "UObject/Casts.h"
#include <sol/sol.hpp>
#include <cmath>

namespace
{
	APlayerCameraManager* FindCameraManager(UObject* Owner, int32 PlayerIndex)
	{
		UWorld* World = FindGameWorldForObject(Owner);
		APlayerController* Controller = World ? World->GetPlayerController(PlayerIndex) : nullptr;
		return Controller ? Controller->GetCameraPlayerManager() : nullptr;
	}

	// Lua 인자에서 액터를 꺼낸다. nil·파괴된 객체·액터가 아닌 객체면 Lua 에러
	AActor* ToActor(const sol::object& Value, const char* What)
	{
		AActor* Actor = Value.is<FLuaObjectRef>() ? Cast<AActor>(Value.as<FLuaObjectRef>().Weak.Get()) : nullptr;
		if (!Actor)
			throw sol::error(std::format("{}: 살아 있는 액터가 필요합니다", What));
		return Actor;
	}

	EViewTargetBlendFunction ToBlendFunction(const std::string& Name)
	{
		EViewTargetBlendFunction Function;
		if (!TryParseViewTargetBlendFunction(Name, Function))
			throw sol::error(std::format("SetViewTarget: 블렌드 함수는 Linear, Cubic, EaseIn, EaseOut, EaseInOut 중 하나입니다 ('{}')", Name));
		return Function;
	}

	// From에서 To를 바라보는 회전. Pitch·Yaw 부호 규칙을 가정하지 않고, 실제 Forward가 방향과 가장 잘 맞는 조합을 고른다
	FRotator FindLookAtRotation(const FVector& From, const FVector& To)
	{
		const FVector Dir = (To - From).Normalized();
		const float Yaw = FMath::RadiansToDegrees(std::atan2(Dir.Y, Dir.X));
		const float Pitch = FMath::RadiansToDegrees(std::asin(FMath::Clamp(Dir.Z, -1.0f, 1.0f)));

		FRotator Best(Pitch, Yaw, 0.0f);
		float BestDot = -2.0f;
		for (const float PitchSign : { 1.0f, -1.0f })
		{
			for (const float YawSign : { 1.0f, -1.0f })
			{
				const FRotator Candidate(Pitch * PitchSign, Yaw * YawSign, 0.0f);
				const float Dot = FVector::Dot(Candidate.Quaternion().GetForwardVector(), Dir);
				if (Dot > BestDot)
				{
					BestDot = Dot;
					Best = Candidate;
				}
			}
		}
		return Best;
	}
}

void RegisterLuaCameraAPI(sol::environment& Env, UObject* Owner)
{
	// CameraShake(Strength, Duration, Frequency = 20): 감쇠하는 사인파로 0번 플레이어 카메라를 흔든다. 여러 번 부르면 겹쳐서 흔들린다.
	Env.set_function("CameraShake",
		[Owner](float Strength, float Duration, sol::optional<float> Frequency)
		{
			if (APlayerCameraManager* Manager = FindCameraManager(Owner, 0))
				Manager->StartCameraShake(Strength, Duration, Frequency.value_or(20.0f));
		});

	// NoiseShake(Strength, Duration, Frequency = 10): 펄린 노이즈로 불규칙하게 흔든다. Frequency가 높을수록 빠르게 떨린다.
	Env.set_function("NoiseShake",
		[Owner](float Strength, float Duration, sol::optional<float> Frequency)
		{
			if (APlayerCameraManager* Manager = FindCameraManager(Owner, 0))
				Manager->StartPerlinNoiseCameraShake(Strength, Duration, Frequency.value_or(10.0f));
		});

	Env.set_function("StopCameraShakes",
		[Owner]()
		{
			if (APlayerCameraManager* Manager = FindCameraManager(Owner, 0))
				Manager->StopAllCameraShakes();
		});

	// StartCameraFade(From, To, Duration, [R = 0], [G = 0], [B = 0], [Hold = false]): 0번 플레이어 화면을 색으로 덮는 양을 From -> To로 바꾼다.
	// Hold가 false면 끝나는 순간 Fade가 꺼진다
	Env.set_function("StartCameraFade",
		[Owner](float From, float To, float Duration, sol::optional<float> R, sol::optional<float> G, sol::optional<float> B, sol::optional<bool> bHold)
		{
			if (APlayerCameraManager* Manager = FindCameraManager(Owner, 0))
				Manager->StartCameraFade(From, To, Duration,
					FLinearColor(R.value_or(0.0f), G.value_or(0.0f), B.value_or(0.0f), 1.0f), bHold.value_or(false));
		});

	Env.set_function("StopCameraFade",
		[Owner]()
		{
			if (APlayerCameraManager* Manager = FindCameraManager(Owner, 0))
				Manager->StopCameraFade();
		});

	// local Main = GetMainCamera() → 월드의 메인 카메라 액터. 게임 월드가 아니면 nil
	Env.set_function("GetMainCamera",
		[Owner](sol::this_state S) -> sol::object
		{
			UWorld* World = FindGameWorldForObject(Owner);
			return LuaReflection::MakeObject(sol::state_view(S), World ? World->GetMainCamera() : nullptr);
		});

	// SetViewTarget(Target, [BlendTime = 0], [BlendFunc = "Cubic"], [BlendExp = 2], [bLockOutgoing = false], [PlayerIndex = 0])
	// BlendTime이 0이면 바로 전환, 0보다 크면 그 시간 동안 블렌드한다. 성공하면 true
	Env.set_function("SetViewTarget",
		[Owner](sol::object Target, sol::optional<float> BlendTime, sol::optional<std::string> BlendFunc,
			sol::optional<float> BlendExp, sol::optional<bool> bLockOutgoing, sol::optional<int32> PlayerIndex)
		{
			AActor* Actor = ToActor(Target, "SetViewTarget");
			APlayerController* Controller = nullptr;
			if (UWorld* World = FindGameWorldForObject(Owner))
				Controller = World->GetPlayerController(PlayerIndex.value_or(0));
			if (!Controller || !Controller->GetCameraPlayerManager())
				return false;

			Controller->SetViewTargetWithBlend(Actor, BlendTime.value_or(0.0f), ToBlendFunction(BlendFunc.value_or("Cubic")),
				BlendExp.value_or(2.0f), bLockOutgoing.value_or(false));
			return true;
		});

	// GetViewTarget([PlayerIndex = 0]) → 지금 시점을 제공하는 액터. 블렌드 중이면 떠나는 쪽
	Env.set_function("GetViewTarget",
		[Owner](sol::optional<int32> PlayerIndex, sol::this_state S) -> sol::object
		{
			APlayerCameraManager* Manager = FindCameraManager(Owner, PlayerIndex.value_or(0));
			return LuaReflection::MakeObject(sol::state_view(S), Manager ? Manager->GetViewTarget() : nullptr);
		});

	// IsViewTargetBlending([PlayerIndex = 0]) → 블렌드가 진행 중이면 true. wait_until과 같이 쓴다
	Env.set_function("IsViewTargetBlending",
		[Owner](sol::optional<int32> PlayerIndex)
		{
			APlayerCameraManager* Manager = FindCameraManager(Owner, PlayerIndex.value_or(0));
			return Manager && Manager->IsBlending();
		});

	// GetCameraLocation / GetCameraRotation([PlayerIndex = 0]) → 지금 화면에 나가는 시점 (블렌드·효과 반영)
	Env.set_function("GetCameraLocation",
		[Owner](sol::optional<int32> PlayerIndex)
		{
			APlayerCameraManager* Manager = FindCameraManager(Owner, PlayerIndex.value_or(0));
			return Manager ? Manager->GetCameraCacheView().Location : FVector();
		});
	Env.set_function("GetCameraRotation",
		[Owner](sol::optional<int32> PlayerIndex)
		{
			APlayerCameraManager* Manager = FindCameraManager(Owner, PlayerIndex.value_or(0));
			return Manager ? Manager->GetCameraCacheView().Rotation : FRotator(0.0f, 0.0f, 0.0f);
		});

	// local Camera = SpawnCamera(Location, Rotation) → 그 자리에 카메라 액터를 만든다. 씬에 저장되지 않는다
	// 다 쓰면 Camera:Destroy()로 지운다
	Env.set_function("SpawnCamera",
		[Owner](const FVector& Location, const FRotator& Rotation, sol::this_state S) -> sol::object
		{
			UWorld* World = FindGameWorldForObject(Owner);
			ACameraActor* Camera = World ? Cast<ACameraActor>(World->SpawnActor(ACameraActor::StaticClass())) : nullptr;
			if (!Camera)
				return sol::make_object(S, sol::lua_nil);

			Camera->SetFlags(EObjectFlags::RF_Transient);
			UCameraComponent* Component = Camera->GetCameraComponent();
			// 게임 카메라는 기본 자유 비행(WASD·마우스)을 쓰지 않는다
			Component->SetExternalInputManaged(true);
			Component->SetWorldLocationAndRotation(Location, Rotation, false);
			return LuaReflection::MakeObject(sol::state_view(S), Camera);
		});

	// local Actor = SpawnMeshActor(MeshPath, Location, [Rotation], [Scale = 1]) → 그 자리에 메시 액터를 만든다. 씬에 저장되지 않는다
	// MeshPath는 씬 파일의 "StaticMesh" 값과 같은 키다. 메시를 못 찾으면 nil. 다 쓰면 Actor:Destroy()로 지운다
	Env.set_function("SpawnMeshActor",
		[Owner](const FString& MeshPath, const FVector& Location, sol::optional<FRotator> Rotation, sol::optional<float> Scale,
			sol::this_state S) -> sol::object
		{
			UWorld* World = FindGameWorldForObject(Owner);
			UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(MeshPath);
			AStaticMeshActor* Actor = (World && Mesh) ? Cast<AStaticMeshActor>(World->SpawnActor(AStaticMeshActor::StaticClass())) : nullptr;
			if (!Actor)
				return sol::make_object(S, sol::lua_nil);

			Actor->SetFlags(EObjectFlags::RF_Transient);
			UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
			Component->SetStaticMesh(Mesh);
			const float Size = Scale.value_or(1.0f);
			Component->SetRelativeScale3D(FVector(Size, Size, Size));
			Component->SetWorldLocationAndRotation(Location, Rotation.value_or(FRotator(0.0f, 0.0f, 0.0f)), false);
			return LuaReflection::MakeObject(sol::state_view(S), Actor);
		});

	// local Rotation = FindLookAtRotation(From, To) → From에서 To를 바라보는 회전
	Env.set_function("FindLookAtRotation",
		[](const FVector& From, const FVector& To)
		{
			return FindLookAtRotation(From, To);
		});

	// local Center, Radius = GetActorBounds(Actor) → 액터의 모든 컴포넌트를 감싸는 월드 AABB의 중심과 반지름
	Env.set_function("GetActorBounds",
		[](sol::object Target)
		{
			AActor* Actor = ToActor(Target, "GetActorBounds");

			bool bHasBounds = false;
			FVector Min, Max;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				const USceneComponent* Scene = Cast<USceneComponent>(Component);
				if (!Scene)
					continue;

				const FBox Box = Scene->CalcBounds();
				if ((Box.Max - Box.Min).Length() <= 0.0f)
					continue;

				Min = bHasBounds ? FVector(FMath::Min(Min.X, Box.Min.X), FMath::Min(Min.Y, Box.Min.Y), FMath::Min(Min.Z, Box.Min.Z)) : Box.Min;
				Max = bHasBounds ? FVector(FMath::Max(Max.X, Box.Max.X), FMath::Max(Max.Y, Box.Max.Y), FMath::Max(Max.Z, Box.Max.Z)) : Box.Max;
				bHasBounds = true;
			}

			if (!bHasBounds)
				return std::make_tuple(Actor->GetActorLocation(), 0.0f);
			return std::make_tuple((Min + Max) * 0.5f, (Max - Min).Length() * 0.5f);
		});

	// local Center, Radius = GetComponentBounds(Component) → 컴포넌트 하나의 월드 AABB 중심과 반지름
	Env.set_function("GetComponentBounds",
		[](sol::object Target)
		{
			const USceneComponent* Scene = Target.is<FLuaObjectRef>() ? Cast<USceneComponent>(Target.as<FLuaObjectRef>().Weak.Get()) : nullptr;
			if (!Scene)
				throw sol::error("GetComponentBounds: 살아 있는 씬 컴포넌트가 필요합니다");

			const FBox Box = Scene->CalcBounds();
			return std::make_tuple((Box.Min + Box.Max) * 0.5f, (Box.Max - Box.Min).Length() * 0.5f);
		});
}
