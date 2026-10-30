#include "EnginePCH.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Component/SceneComponent.h"
#include "UObject/Class.h"
#include "UObject/Property.h"

#include "UObject/Function.h"
#include "Serialization/TypeSerializer.h"
#include "Engine/EffectManager.h"
#include "Component/ParticleSubUVComponent.h"
#include "Script/LuaScriptInstance.h"
#include "Script/LuaScriptManager.h"

#include <algorithm>
#include <cmath>

// 이벤트 키의 JSON 인자를 리플렉션 매개변수 타입 / Lua 값으로 바꾼다 (아래 TriggerEvent에서 쓴다)
static bool JsonToScriptValue(const json& In, EPropertyType Type, FScriptValue& Out);
static sol::object JsonToLua(sol::state_view Lua, const json& In);

// TypeSerializer의 from_json은 모양을 검사하지 않으므로, 숫자 Count개짜리 배열인지 먼저 본다
static bool IsNumberArray(const json& In, size_t Count)
{
	if (!In.is_array() || In.size() != Count)
		return false;
	for (const json& Element : In)
	{
		if (!Element.is_number())
			return false;
	}
	return true;
}

// 시퀀스 Effects 프리셋 값 하나. 키 → 필드 규칙은 ApplyEffectDescField가 Lua RegisterEffect와 같이 쓴다. 벡터는 [x, y, z]
struct FJsonEffectValue : IEffectValueReader
{
	explicit FJsonEffectValue(const json& InValue) : Value(InValue) {}
	bool ReadNumber(float& Out) const override { if (!Value.is_number()) return false; Out = Value.get<float>(); return true; }
	bool ReadBool(bool& Out) const override { if (!Value.is_boolean()) return false; Out = Value.get<bool>(); return true; }
	bool ReadString(FString& Out) const override { if (!Value.is_string()) return false; Out = Value.get<FString>(); return true; }
	bool ReadVector(FVector& Out) const override { if (!IsNumberArray(Value, 3)) return false; Out = Value.get<FVector>(); return true; }
	const json& Value;
};

// 모르는 키는 경고만 하고, 값이 틀리면 그 프리셋을 버린다
static bool JsonToEffectDesc(const FString& Name, const json& In, FEffectDesc& Desc)
{
	if (!In.is_object())
		return false;

	for (const auto& [Key, Value] : In.items())
	{
		FString Error;
		bool bUnknownKey = false;
		if (ApplyEffectDescField(Desc, Key, FJsonEffectValue(Value), Error, bUnknownKey))
			continue;
		if (bUnknownKey)
		{
			HTR_LOG(Warning, "LevelSequence: effect '{}': unknown key '{}' ignored", Name, Key);
			continue;
		}
		HTR_LOG(Warning, "LevelSequence: effect '{}': {}", Name, Error);
		return false;
	}
	return !Desc.TexturePath.empty();
}

// 모르는 이름이면 Cubic (Lua SetViewTarget의 기본값과 같다)
static EViewTargetBlendFunction ParseBlendFunction(const FString& Name)
{
	EViewTargetBlendFunction Function = EViewTargetBlendFunction::Cubic;
	TryParseViewTargetBlendFunction(Name, Function);
	return Function;
}

ULevelSequencePlayer::~ULevelSequencePlayer()
{
	// 여기서는 액터를 Destroy 하지 않는다. 월드를 비울 때(ClearActors) 액터 배열을 도는 중에 불릴 수 있어서다.
	// 정리가 필요하면 지우기 전에 Stop을 부른다 (ALevelSequenceActor는 EndPlay에서 부른다).
	// 남은 스폰 액터는 RF_Transient라 씬에 저장되지 않고, 월드와 함께 지워진다.
	// 엔진 종료 때는 월드가 먼저 지워졌을 수 있으므로 살아 있을 때만 등록을 푼다.
	if (World && FindObjectByUUID(WorldUUID) == World)
		World->UnregisterSequencePlayer(this);
}

ULevelSequencePlayer* ULevelSequencePlayer::CreateLevelSequencePlayer(UWorld* World, ULevelSequence* LevelSequence, FMovieSceneSequencePlaybackSettings Settings, ALevelSequenceActor*& OutActor)
{
	OutActor = nullptr;
	if (!World || !LevelSequence)
		return nullptr;

	ALevelSequenceActor* Actor = World->SpawnActor<ALevelSequenceActor>();
	if (!Actor)
		return nullptr;

	// 실행 중에 만든 연출용 액터이므로 씬에 저장하지 않는다
	Actor->SetFlags(EObjectFlags::RF_Transient);
	Actor->SetPlaybackSettings(Settings);
	Actor->SetSequence(LevelSequence);

	OutActor = Actor;
	return Actor->GetSequencePlayer();
}

void ULevelSequencePlayer::Initialize(ULevelSequence* InSequence, UWorld* InWorld, const FMovieSceneSequencePlaybackSettings& InSettings)
{
	if (World != InWorld)
	{
		if (World)
			World->UnregisterSequencePlayer(this);
		if (InWorld)
			InWorld->RegisterSequencePlayer(this);
	}

	InvalidateBindings();

	Sequence = InSequence;
	World = InWorld;
	WorldUUID = World ? World->GetUUID() : 0;
	Settings = InSettings;
	CurrentTime = Settings.StartTime;
	LoopsDone = 0;
	bPlaying = false;
}

void ULevelSequencePlayer::Play()
{
	if (!Sequence || !World)
		return;

	// 끝에서 다시 누르면 처음부터 재생한다
	const float Duration = GetDuration();
	if (Settings.PlayRate >= 0.0f && CurrentTime >= Duration)
		CurrentTime = 0.0f;
	else if (Settings.PlayRate < 0.0f && CurrentTime <= 0.0f)
		CurrentTime = Duration;

	bPlaying = true;
	LoopsDone = 0;
	RegisterEffects();

	// 다음 Tick까지 기다리면 첫 프레임에 원래 자세가 한 번 보인다
	Evaluate(CurrentTime);
	// 카메라 자세를 먼저 쓴 뒤에 넘어가야 첫 프레임부터 맞는 시점이 나온다
	ActivateCineCamera();
	// 시작 시간에 놓인 이벤트도 부른다
	FireEvents(CurrentTime, CurrentTime, true);
}

void ULevelSequencePlayer::Pause()
{
	bPlaying = false;
}

void ULevelSequencePlayer::Stop()
{
	bPlaying = false;
	CurrentTime = Settings.StartTime;
	LoopsDone = 0;
	InvalidateBindings();
}

void ULevelSequencePlayer::SetPlaybackPosition(float Time)
{
	if (!Sequence)
		return;

	CurrentTime = std::clamp(Time, 0.0f, GetDuration());
	Evaluate(CurrentTime);
}

void ULevelSequencePlayer::Tick(float DeltaTime)
{
	if (!bPlaying || !Sequence)
		return;

	const float Duration = GetDuration();
	if (Duration <= 0.0f)
	{
		Finish();
		return;
	}

	const float PreviousTime = CurrentTime;
	CurrentTime += DeltaTime * Settings.PlayRate;

	// 끝을 넘었으면 남은 반복이 있을 때 넘친 만큼 반대쪽에서 이어 가고, 없으면 끝에서 멈춘다
	const bool bForward = Settings.PlayRate >= 0.0f;
	const float EndTime = bForward ? Duration : 0.0f;
	const bool bPassedEnd = bForward ? CurrentTime >= Duration : CurrentTime <= 0.0f;
	if (bPassedEnd)
	{
		const bool bCanLoop = Settings.LoopCount < 0 || LoopsDone < Settings.LoopCount;
		if (!bCanLoop)
		{
			CurrentTime = EndTime;
			Evaluate(CurrentTime);
			FireEvents(PreviousTime, CurrentTime, false);
			Finish();
			return;
		}

		// 되감기 전 남은 구간의 이벤트를 먼저 부르고, 반대쪽 끝부터 다시 센다
		FireEvents(PreviousTime, EndTime, false);
		++LoopsDone;
		CurrentTime = std::fmod(CurrentTime, Duration);
		if (CurrentTime < 0.0f)
			CurrentTime += Duration;

		Evaluate(CurrentTime);
		UpdateCineCamera();
		FireEvents(bForward ? 0.0f : Duration, CurrentTime, true);
		return;
	}

	Evaluate(CurrentTime);
	UpdateCineCamera();
	FireEvents(PreviousTime, CurrentTime, false);
}

void ULevelSequencePlayer::RegisterEffects()
{
	if (!Sequence || !World || !Sequence->Effects.is_object() || Sequence->Effects.empty())
		return;

	FEffectManager& Effects = World->GetEffectManager();
	for (const auto& [Name, DescJson] : Sequence->Effects.items())
	{
		FEffectDesc Desc;
		if (!JsonToEffectDesc(Name, DescJson, Desc))
		{
			HTR_LOG(Warning, "LevelSequence: effect '{}' is invalid, skipped", Name);
			continue;
		}
		Effects.RegisterEffect(FName(Name), Desc);
	}
}

void ULevelSequencePlayer::UpdateCineCamera()
{
	// 카메라 컷이 끝나는 시간을 지나면 이전 ViewTarget으로 돌아간다
	const float EndTime = Sequence ? Sequence->CineCameraEndTime : -1.0f;
	if (bCineCameraActive && EndTime >= 0.0f && CurrentTime >= EndTime)
	{
		DeactivateCineCamera();
		return;
	}

	// Play 때 아직 PlayerController가 없었거나(BeginPlay 순서) 반복으로 처음에 돌아왔으면 다시 넘어간다
	if (!bCineCameraActive)
		ActivateCineCamera();
}

void ULevelSequencePlayer::FireEvents(float FromTime, float ToTime, bool bIncludeFrom)
{
	if (!Sequence)
		return;

	// 재생 방향과 상관없이 작은 쪽부터 센다. 시작점(FromTime)은 이전 프레임에 이미 불렀으므로 뺀다.
	const bool bForward = FromTime <= ToTime;
	const float Low = bForward ? FromTime : ToTime;
	const float High = bForward ? ToTime : FromTime;
	const auto InRange = [&](float Time)
	{
		if (Time < Low || Time > High)
			return false;
		if (!bIncludeFrom && Time == FromTime)
			return false;
		return true;
	};

	// 마스터 트랙의 이벤트는 이 플레이어를 가진 액터(ALevelSequenceActor, 디렉터)에서 부른다
	if (AActor* Director = Cast<AActor>(GetOuter()))
	{
		const FWeakObjectPtr WeakDirector(Director);
		for (const FSequenceTrack& Track : Sequence->MasterTracks)
		{
			if (Track.Type != ESequenceTrackType::Event)
				continue;

			for (const FSequenceSection& Section : Track.Sections)
			{
				for (const FSequenceEventKey& Event : Section.Events)
				{
					if (!InRange(Event.Time))
						continue;
					if (AActor* Target = Cast<AActor>(WeakDirector.Get()))
						TriggerEvent(Target, Event);
					if (!bBindingsResolved)
						return;
				}
			}
		}
	}

	for (int32 i = 0; i < Sequence->Bindings.Num() && i < BoundActors.Num(); ++i)
	{
		for (const FSequenceTrack& Track : Sequence->Bindings[i].Tracks)
		{
			if (Track.Type != ESequenceTrackType::Event)
				continue;

			for (const FSequenceSection& Section : Track.Sections)
			{
				for (const FSequenceEventKey& Event : Section.Events)
				{
					if (!InRange(Event.Time))
						continue;

					// 이벤트(Lua)가 Stop을 불러 바인딩을 비울 수 있으므로 사본을 돌고, 비워졌으면 남은 이벤트는 버린다
					const TArray<FWeakObjectPtr> Targets = BoundActors[i];
					for (const FWeakObjectPtr& Weak : Targets)
					{
						if (AActor* Actor = Cast<AActor>(Weak.Get()))
							TriggerEvent(Actor, Event);
						if (!bBindingsResolved)
							return;
					}
				}
			}
		}
	}
}

void ULevelSequencePlayer::TriggerEvent(AActor* Actor, const FSequenceEventKey& Event)
{
	// "Component.Func"면 그 컴포넌트, 아니면 액터에서 찾는다
	UObject* Target = Actor;
	FString FunctionName = Event.Function;
	const size_t Dot = Event.Function.find('.');
	if (Dot != FString::npos)
	{
		Target = Actor->FindComponentByName(FName(Event.Function.substr(0, Dot)));
		FunctionName = Event.Function.substr(Dot + 1);
	}

	if (!Target)
	{
		HTR_LOG(Warning, "LevelSequence: event '{}' on '{}': component not found", Event.Function, Actor->GetName());
		return;
	}

	// 1) 리플렉션 FUNCTION
	if (const FFunction* Function = Target->GetClass()->FindFunction(FunctionName))
	{
		TArray<FScriptValue> Args;
		const int32 ArgCount = Event.Args.is_array() ? static_cast<int32>(Event.Args.size()) : 0;
		if (ArgCount != Function->ParamTypes.Num())
		{
			HTR_LOG(Warning, "LevelSequence: event '{}' needs {} args, got {}", Event.Function, Function->ParamTypes.Num(), ArgCount);
			return;
		}
		for (int32 a = 0; a < ArgCount; ++a)
		{
			FScriptValue& Value = Args.AddDefaulted_GetRef();
			if (!JsonToScriptValue(Event.Args[a], Function->ParamTypes[a], Value))
			{
				HTR_LOG(Warning, "LevelSequence: event '{}' arg {} has the wrong type", Event.Function, a + 1);
				return;
			}
		}

		FScriptValue Return;
		TArray<FScriptValue> OutParams;
		FString Error;
		if (!Function->Invoke(Target, Args, Return, OutParams, Error))
			HTR_LOG(Warning, "LevelSequence: event '{}' failed: {}", Event.Function, Error);
		return;
	}

	// 2) 액터에 붙은 Lua 스크립트 함수 (UE의 Director Blueprint 이벤트에 해당)
	FLuaScriptInstance* Script = Target == Actor ? Actor->GetScriptInstance() : nullptr;
	if (Script && Script->HasFunction(FunctionName.c_str()))
	{
		sol::state_view Lua(FLuaScriptManager::Get().GetState());
		std::vector<sol::object> Args;
		if (Event.Args.is_array())
		{
			for (const json& Arg : Event.Args)
				Args.push_back(JsonToLua(Lua, Arg));
		}
		Script->Call(FunctionName.c_str(), sol::as_args(Args));
		return;
	}

	HTR_LOG(Warning, "LevelSequence: event '{}' not found on '{}' (no reflected function or script function)", Event.Function, Actor->GetName());
}

static bool JsonToScriptValue(const json& In, EPropertyType Type, FScriptValue& Out)
{
	switch (Type)
	{
	case EPropertyType::Float:
		if (!In.is_number()) return false;
		Out = FScriptValue(std::in_place_type<float>, In.get<float>());
		return true;
	case EPropertyType::Int:
		if (!In.is_number()) return false;
		Out = FScriptValue(std::in_place_type<int32>, In.get<int32>());
		return true;
	case EPropertyType::Bool:
		if (!In.is_boolean()) return false;
		Out = FScriptValue(std::in_place_type<bool>, In.get<bool>());
		return true;
	case EPropertyType::String:
		if (!In.is_string()) return false;
		Out = FScriptValue(std::in_place_type<FString>, In.get<FString>());
		return true;
	case EPropertyType::Name:
		if (!In.is_string()) return false;
		Out = FScriptValue(std::in_place_type<FName>, FName(In.get<FString>()));
		return true;
	case EPropertyType::Vector:
		if (!IsNumberArray(In, 3)) return false;
		Out = FScriptValue(std::in_place_type<FVector>, In.get<FVector>());
		return true;
	case EPropertyType::Rotator:
		if (!IsNumberArray(In, 3)) return false;
		Out = FScriptValue(std::in_place_type<FRotator>, In.get<FRotator>());
		return true;
	case EPropertyType::Vector4:
		if (!IsNumberArray(In, 4)) return false;
		Out = FScriptValue(std::in_place_type<FVector4>, In.get<FVector4>());
		return true;
	case EPropertyType::LinearColor:
		if (!IsNumberArray(In, 4)) return false;
		Out = FScriptValue(std::in_place_type<FLinearColor>, In.get<FLinearColor>());
		return true;
	default:
		return false;
	}
}

static sol::object JsonToLua(sol::state_view Lua, const json& In)
{
	if (In.is_boolean())
		return sol::make_object(Lua, In.get<bool>());
	if (In.is_number())
		return sol::make_object(Lua, In.get<double>());
	if (In.is_string())
		return sol::make_object(Lua, In.get<FString>());
	// 숫자 3개짜리 배열은 FVector로 넘긴다
	if (IsNumberArray(In, 3))
		return sol::make_object(Lua, In.get<FVector>());
	return sol::make_object(Lua, sol::lua_nil);
}

void ULevelSequencePlayer::Finish()
{
	bPlaying = false;

	// 시점은 bRestoreState와 상관없이 항상 돌려준다. 끝난 연출 카메라에 화면이 묶여 있으면 안 된다.
	DeactivateCineCamera();

	if (Settings.bRestoreState)
		InvalidateBindings();

	OnFinished.Broadcast();
}

void ULevelSequencePlayer::ActivateCineCamera()
{
	if (bCineCameraActive || !Sequence || !World)
		return;

	// 카메라 컷이 이미 끝난 시간이면 넘어가지 않는다
	if (Sequence->CineCameraEndTime >= 0.0f && CurrentTime >= Sequence->CineCameraEndTime)
		return;

	const int32 BindingIndex = Sequence->FindCineCameraBinding();
	if (BindingIndex == INDEX_NONE)
		return;

	APlayerController* Controller = World->GetPlayerController(0);
	if (!::IsValid(Controller) || !Controller->GetCameraPlayerManager())
		return;

	// 대상을 못 찾았으면 ResolveBindings가 이미 경고했다
	const TArray<AActor*> Cameras = GetBoundActors(BindingIndex);
	if (Cameras.IsEmpty())
		return;

	PreviousViewTarget = FWeakObjectPtr(Controller->GetCameraPlayerManager()->GetViewTarget());
	CineCamera = FWeakObjectPtr(Cameras[0]);
	bCineCameraActive = true;

	Controller->SetViewTargetWithBlend(Cameras[0], Sequence->CameraBlendInTime,
		ParseBlendFunction(Sequence->CameraBlendFunction), Sequence->CameraBlendExp, false);
	HTR_LOG(Info, "LevelSequence: view target -> cine camera '{}' (blend {}s)", Cameras[0]->GetName(), Sequence->CameraBlendInTime);
}

void ULevelSequencePlayer::DeactivateCineCamera()
{
	if (!bCineCameraActive)
		return;

	bCineCameraActive = false;
	AActor* Cine = Cast<AActor>(CineCamera.Get());
	AActor* Previous = Cast<AActor>(PreviousViewTarget.Get());
	CineCamera.Reset();
	PreviousViewTarget.Reset();

	APlayerController* Controller = World ? World->GetPlayerController(0) : nullptr;
	APlayerCameraManager* CameraManager = ::IsValid(Controller) ? Controller->GetCameraPlayerManager() : nullptr;
	if (!CameraManager)
		return;

	// 연출 중에 게임플레이가 다른 대상으로 바꿨으면 그 결정을 덮어쓰지 않는다.
	// 시네 카메라로 블렌드하는 중이면 아직 떠나는 쪽이 ViewTarget이므로 되돌린다.
	if (!CameraManager->IsBlending() && CameraManager->GetViewTarget() != Cine)
		return;

	if (!Previous)
		Previous = World->GetMainCamera();

	// 스폰한 시네 카메라는 곧 지워질 수 있으므로 떠나는 쪽 POV를 전환 시작 순간 값으로 고정한다 (bLockOutgoing)
	const float BlendTime = Sequence ? Sequence->CameraBlendOutTime : 0.0f;
	const EViewTargetBlendFunction BlendFunction = Sequence ? ParseBlendFunction(Sequence->CameraBlendFunction) : EViewTargetBlendFunction::Cubic;
	const float BlendExp = Sequence ? Sequence->CameraBlendExp : 2.0f;
	Controller->SetViewTargetWithBlend(Previous, BlendTime, BlendFunction, BlendExp, true);
	HTR_LOG(Info, "LevelSequence: view target -> restored '{}' (blend {}s)", Previous ? Previous->GetName() : FString("None"), BlendTime);
}

void ULevelSequencePlayer::RestoreState()
{
	// 거꾸로 되돌려야 같은 대상을 여러 번 저장했을 때도 가장 처음 값이 남는다
	for (int32 i = PreAnimatedValues.Num() - 1; i >= 0; --i)
	{
		const FPreAnimatedValue& Saved = PreAnimatedValues[i];
		UObject* Object = Saved.Object.Get();
		if (!Object)
			continue;

		if (!Saved.Property)
		{
			if (USceneComponent* Component = Cast<USceneComponent>(Object))
				Component->SetTransform(Saved.Transform);
			continue;
		}

		void* Ptr = reinterpret_cast<uint8*>(Object) + Saved.Property->Offset;
		if (Saved.Property->Type == EPropertyType::Float)
			*static_cast<float*>(Ptr) = Saved.FloatValue;
		else
			*static_cast<bool*>(Ptr) = Saved.BoolValue;
		Object->PostEditChangeProperty(*Saved.Property);
	}

	PreAnimatedValues.Reset();
}

void ULevelSequencePlayer::ReleaseBindings(bool bRestoreValues)
{
	// 스폰한 시네 카메라를 지우기 전에 시점부터 돌려놓는다
	DeactivateCineCamera();
	if (bRestoreValues)
		RestoreState();
	else
		PreAnimatedValues.Reset();
	DestroySpawnedActors();
	BoundActors.Reset();
	SpawnTrackControlled.Reset();
	bBindingsResolved = false;
}

TArray<AActor*> ULevelSequencePlayer::GetBoundActors(int32 BindingIndex) const
{
	TArray<AActor*> Result;
	if (BindingIndex < 0 || BindingIndex >= BoundActors.Num())
		return Result;

	for (const FWeakObjectPtr& Weak : BoundActors[BindingIndex])
	{
		if (AActor* Actor = Cast<AActor>(Weak.Get()))
			Result.Add(Actor);
	}
	return Result;
}

void ULevelSequencePlayer::FindActorsByTag(UWorld* World, const FString& Tag, TArray<AActor*>& OutActors)
{
	if (!World || !World->GetPersistentLevel() || Tag.empty())
		return;

	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		if (::IsValid(Actor) && Actor->BindingTag == Tag)
			OutActors.Add(Actor);
	}
}

void ULevelSequencePlayer::ResolveBindings()
{
	BoundActors.Reset();
	BoundActors.SetNum(Sequence->Bindings.Num());
	SpawnTrackControlled.Reset();
	SpawnTrackControlled.SetNum(Sequence->Bindings.Num());

	for (int32 i = 0; i < Sequence->Bindings.Num(); ++i)
	{
		const FSequenceBinding& Binding = Sequence->Bindings[i];
		SpawnTrackControlled[i] = 0;

		// 1) 씬에 있으면 빌려 쓴다 (Possessable)
		TArray<AActor*> Found;
		FindActorsByTag(World, Binding.Tag, Found);
		if (Found.Num() > 1)
			HTR_LOG(Warning, "LevelSequence: tag '{}' matches {} actors, all of them are animated", Binding.Tag, Found.Num());

		// 2-a) Spawn 트랙이 있으면 지금 스폰하지 않는다. Evaluate가 트랙 값에 따라 스폰하고 지운다
		if (Found.IsEmpty() && Binding.HasTemplate() && Binding.FindSpawnTrack())
		{
			SpawnTrackControlled[i] = 1;
			continue;
		}

		// 2-b) 없으면 템플릿으로 스폰한다 (Spawnable)
		if (Found.IsEmpty() && Binding.HasTemplate())
		{
			if (AActor* Spawned = ULevelSequence::SpawnFromTemplate(World, Binding.Template))
			{
				Found.Add(Spawned);
				SpawnedActors.Add(FWeakObjectPtr(Spawned));
			}
		}

		// 3) 둘 다 없으면 이 바인딩만 건너뛴다
		if (Found.IsEmpty())
			HTR_LOG(Warning, "LevelSequence: binding '{}' (tag '{}') is unresolved", Binding.Name, Binding.Tag);

		for (AActor* Actor : Found)
			BoundActors[i].Add(FWeakObjectPtr(Actor));
	}

	bBindingsResolved = true;
}

void ULevelSequencePlayer::DestroySpawnedActors()
{
	for (const FWeakObjectPtr& Weak : SpawnedActors)
	{
		if (AActor* Actor = Cast<AActor>(Weak.Get()))
			Actor->Destroy();
	}
	SpawnedActors.Reset();
}

void ULevelSequencePlayer::Evaluate(float Time)
{
	if (!Sequence || !World)
		return;

	if (!bBindingsResolved)
		ResolveBindings();

	for (int32 i = 0; i < Sequence->Bindings.Num() && i < BoundActors.Num(); ++i)
	{
		if (SpawnTrackControlled[i])
			UpdateSpawnTrack(i, Time);

		for (const FWeakObjectPtr& Weak : BoundActors[i])
		{
			AActor* Actor = Cast<AActor>(Weak.Get());
			if (!Actor)
				continue;

			for (const FSequenceTrack& Track : Sequence->Bindings[i].Tracks)
			{
				// 켜진 섹션이 없으면 값을 쓰지 않는다. 마지막으로 쓴 값이 그대로 남는다.
				if (const FSequenceSection* Section = Track.FindSection(Time))
					EvaluateTrack(Actor, Track, *Section, Time);
			}
		}
	}
}

void ULevelSequencePlayer::UpdateSpawnTrack(int32 BindingIndex, float Time)
{
	const FSequenceBinding& Binding = Sequence->Bindings[BindingIndex];
	const FSequenceTrack* SpawnTrack = Binding.FindSpawnTrack();

	// 섹션 밖이면 존재하지 않는다
	bool bShouldExist = false;
	if (const FSequenceSection* Section = SpawnTrack ? SpawnTrack->FindSection(Time) : nullptr)
		bShouldExist = !Section->Channels.IsEmpty() && Section->Channels[0].HasKeys() && Section->Channels[0].Evaluate(Time, true) >= 0.5f;

	TArray<FWeakObjectPtr>& Actors = BoundActors[BindingIndex];
	if (bShouldExist && Actors.IsEmpty())
	{
		if (AActor* Spawned = ULevelSequence::SpawnFromTemplate(World, Binding.Template))
		{
			Actors.Add(FWeakObjectPtr(Spawned));
			SpawnedActors.Add(FWeakObjectPtr(Spawned));
		}
	}
	else if (!bShouldExist && !Actors.IsEmpty())
	{
		for (const FWeakObjectPtr& Weak : Actors)
		{
			if (AActor* Actor = Cast<AActor>(Weak.Get()))
				Actor->Destroy();
		}
		Actors.Reset();
	}
}

void ULevelSequencePlayer::EvaluateTrack(AActor* Actor, const FSequenceTrack& Track, const FSequenceSection& Section, float Time)
{
	switch (Track.Type)
	{
	case ESequenceTrackType::Transform:
	{
		// PropertyPath가 비었으면 루트, 아니면 그 이름의 컴포넌트를 상대 Transform으로 움직인다
		USceneComponent* Root = Track.PropertyPath.empty()
			? Actor->GetRootComponent()
			: Cast<USceneComponent>(Actor->FindComponentByName(FName(Track.PropertyPath)));
		if (!Root || Section.Channels.Num() < 9)
			return;

		SavePreAnimatedTransform(Root);

		// 키가 없는 채널은 지금 값을 그대로 둔다 (X만 키를 찍었으면 Y·Z는 손대지 않는다)
		FTransform Transform = Root->GetTransform();
		float* Targets[9] =
		{
			&Transform.Location.X, &Transform.Location.Y, &Transform.Location.Z,
			&Transform.Rotation.Pitch, &Transform.Rotation.Yaw, &Transform.Rotation.Roll,
			&Transform.Scale.X, &Transform.Scale.Y, &Transform.Scale.Z,
		};
		for (int32 c = 0; c < 9; ++c)
		{
			if (Section.Channels[c].HasKeys())
				*Targets[c] = Section.Channels[c].Evaluate(Time);
		}
		Root->SetTransform(Transform);
		break;
	}

	case ESequenceTrackType::Float:
	case ESequenceTrackType::Bool:
	{
		if (Section.Channels.IsEmpty() || !Section.Channels[0].HasKeys())
			return;

		const bool bIsBool = Track.Type == ESequenceTrackType::Bool;
		UObject* Target = nullptr;
		const FProperty* Property = nullptr;
		if (!FindTargetProperty(Actor, Track.PropertyPath, bIsBool ? EPropertyType::Bool : EPropertyType::Float, Target, Property))
			return;

		SavePreAnimatedProperty(Target, Property);

		void* Ptr = reinterpret_cast<uint8*>(Target) + Property->Offset;
		if (bIsBool)
			*static_cast<bool*>(Ptr) = Section.Channels[0].Evaluate(Time, true) >= 0.5f;
		else
			*static_cast<float*>(Ptr) = Section.Channels[0].Evaluate(Time);

		// 디테일 패널에서 값을 고쳤을 때와 같은 후처리 (렌더 상태 갱신 등)
		Target->PostEditChangeProperty(*Property);
		break;
	}

	case ESequenceTrackType::Event:
		// 값을 쓰지 않는다. 재생 헤드가 지나갈 때 FireEvents가 부른다
		break;

	case ESequenceTrackType::Spawn:
		// Evaluate가 트랙을 돌기 전에 UpdateSpawnTrack에서 처리했다
		break;
	}
}

bool ULevelSequencePlayer::FindTargetProperty(AActor* Actor, const FString& PropertyPath, EPropertyType Type, UObject*& OutObject, const FProperty*& OutProperty)
{
	OutObject = Actor;
	FString PropertyName = PropertyPath;

	const size_t Dot = PropertyPath.find('.');
	if (Dot != FString::npos)
	{
		OutObject = Actor->FindComponentByName(FName(PropertyPath.substr(0, Dot)));
		PropertyName = PropertyPath.substr(Dot + 1);
	}

	if (!OutObject)
		return false;

	for (UClass* Class = OutObject->GetClass(); Class; Class = Class->Super)
	{
		for (const FProperty& Property : Class->GetProperties())
		{
			if (Property.Name == PropertyName && Property.Type == Type)
			{
				OutProperty = &Property;
				return true;
			}
		}
	}
	return false;
}

void ULevelSequencePlayer::SavePreAnimatedTransform(USceneComponent* Component)
{
	for (const FPreAnimatedValue& Saved : PreAnimatedValues)
	{
		if (!Saved.Property && Saved.Object.Get() == Component)
			return;
	}

	FPreAnimatedValue& Saved = PreAnimatedValues.AddDefaulted_GetRef();
	Saved.Object = FWeakObjectPtr(Component);
	Saved.Transform = Component->GetTransform();
}

void ULevelSequencePlayer::SavePreAnimatedProperty(UObject* Object, const FProperty* Property)
{
	for (const FPreAnimatedValue& Saved : PreAnimatedValues)
	{
		if (Saved.Property == Property && Saved.Object.Get() == Object)
			return;
	}

	FPreAnimatedValue& Saved = PreAnimatedValues.AddDefaulted_GetRef();
	Saved.Object = FWeakObjectPtr(Object);
	Saved.Property = Property;

	const void* Ptr = reinterpret_cast<const uint8*>(Object) + Property->Offset;
	if (Property->Type == EPropertyType::Float)
		Saved.FloatValue = *static_cast<const float*>(Ptr);
	else
		Saved.BoolValue = *static_cast<const bool*>(Ptr);
}
