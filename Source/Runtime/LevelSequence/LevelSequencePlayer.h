#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/Property.h"
#include "UObject/WeakObjectPtr.h"
#include "Core/Delegates/Delegate.h"
#include "Math/Transform.h"
#include "LevelSequence.h"

class UWorld;
class AActor;
class ALevelSequenceActor;
class USceneComponent;

struct FMovieSceneSequencePlaybackSettings
{
	FMovieSceneSequencePlaybackSettings()
		: bAutoPlay(false)
		, bRestoreState(true)
	{}

	/** Auto-play the sequence when created */
	uint32 bAutoPlay : 1;

	/** 끝나면 시퀀스가 바꾼 값을 원래대로 되돌리고, 시퀀스가 스폰한 액터를 지운다 (UE의 When Finished: Restore State) */
	uint32 bRestoreState : 1;

	/** Number of times to loop playback. -1 for infinite, else the number of times to loop before stopping */
	int32 LoopCount = 0;

	/** The rate at which to playback the animation. 음수면 거꾸로 재생한다 */
	float PlayRate = 1.0f;

	/** Start playback at the specified offset from the start of the sequence's playback range */
	float StartTime = 0.0f;
};

// 시퀀스 하나를 재생한다 (UE UMovieSceneSequencePlayer). 하는 일은 매 프레임 세 가지다.
//   ① 시간 진행  ② 지금 켜진 섹션의 채널 보간  ③ 바인딩된 액터에 값 쓰기
// 게임에서는 World가 액터 Tick 전에 Tick을 부르고, 에디터 Sequencer는 SetPlaybackPosition으로 직접 움직인다.
class ULevelSequencePlayer : public UObject
{
	DECLARE_CLASS(ULevelSequencePlayer, UObject)

public:
	ULevelSequencePlayer() = default;
	virtual ~ULevelSequencePlayer();

	/**
	 * Create a new level sequence player.
	 *
	 * @param World The world to play in.
	 * @param LevelSequence The level sequence to play. 호출한 쪽이 소유한다.
	 * @param Settings The desired playback settings
	 * @param OutActor The level sequence actor created to play this sequence.
	 */
	static ULevelSequencePlayer* CreateLevelSequencePlayer(UWorld* World, ULevelSequence* LevelSequence, FMovieSceneSequencePlaybackSettings Settings, ALevelSequenceActor*& OutActor);

	// World에 등록한다. Sequence는 소유하지 않는다.
	void Initialize(ULevelSequence* InSequence, UWorld* InWorld, const FMovieSceneSequencePlaybackSettings& InSettings);

	void Play();
	// 시간과 액터 상태를 그대로 둔 채 멈춘다
	void Pause();
	// 멈추고 처음으로 돌아간다. 바꾼 값을 되돌리고 스폰한 액터를 지운다.
	void Stop();

	// 재생 여부와 상관없이 Time으로 옮겨 바로 평가한다 (에디터 스크럽, 스크립트 점프)
	void SetPlaybackPosition(float Time);

	// World가 매 프레임 부른다. 재생 중이 아니면 아무것도 하지 않는다.
	void Tick(float DeltaTime);

	bool IsPlaying() const { return bPlaying; }
	float GetCurrentTime() const { return CurrentTime; }
	float GetDuration() const { return Sequence ? Sequence->Duration : 0.0f; }
	ULevelSequence* GetSequence() const { return Sequence; }
	FMovieSceneSequencePlaybackSettings& GetSettings() { return Settings; }

	// 시퀀스가 처음 건드리기 전 값으로 모두 되돌린다
	void RestoreState();

	// 에셋이 바뀌었을 때(에디터에서 바인딩·트랙 편집) 다음 평가에서 대상을 다시 찾게 한다.
	// 스폰한 액터는 지우고, 바꾼 값은 되돌린다.
	void InvalidateBindings() { ReleaseBindings(true); }

	// 시퀀스가 만든 것(시네 카메라 시점, 스폰한 액터)을 정리하고 다음 평가에서 대상을 다시 찾게 한다.
	// bRestoreValues가 false면 바꾼 값은 되돌리지 않는다 (끝난 뒤 Keep State로 남긴 경우).
	void ReleaseBindings(bool bRestoreValues);

	TArray<AActor*> GetBoundActors(int32 BindingIndex) const;
	static void FindActorsByTag(UWorld* World, const FString& Tag, TArray<AActor*>& OutActors);

	// PropertyPath("Prop" 또는 "Component.Prop")가 가리키는 객체와 프로퍼티. 못 찾거나 타입이 다르면 false
	// 에디터 Sequencer가 키를 찍을 때 지금 값을 읽는 데도 쓴다
	static bool FindTargetProperty(AActor* Actor, const FString& PropertyPath, EPropertyType Type, UObject*& OutObject, const FProperty*& OutProperty);

	// 끝까지 재생해 멈췄을 때 (Stop·Pause로는 불리지 않는다)
	TMulticastDelegate<> OnFinished;

private:
	void ResolveBindings();
	void Evaluate(float Time);
	void EvaluateTrack(AActor* Actor, const FSequenceTrack& Track, const FSequenceSection& Section, float Time);
	void DestroySpawnedActors();
	void Finish();

	// FromTime에서 ToTime으로 지나간 구간의 이벤트 키를 부른다 (역재생이면 거꾸로 지나간 구간).
	// 키가 정확히 FromTime에 있으면 bIncludeFrom일 때만 부른다. 이전 프레임에서 이미 불렀기 때문이다.
	// 스크럽(SetPlaybackPosition)에서는 부르지 않는다.
	void FireEvents(float FromTime, float ToTime, bool bIncludeFrom);
	void TriggerEvent(AActor* Actor, const FSequenceEventKey& Event);

	// 시네 카메라 바인딩이 있으면 첫 번째 PlayerController의 ViewTarget을 그 액터로 바꾼다.
	// 원래 ViewTarget은 기억해 두고, DeactivateCineCamera에서 돌려준다.
	// PlayerController가 없는 월드(에디터)에서는 아무것도 하지 않는다.
	void ActivateCineCamera();
	void DeactivateCineCamera();
	// 매 Tick: 카메라 컷 끝(CineCameraEndTime)이 지났으면 돌려주고, 아직 안 넘어갔으면 넘어간다
	void UpdateCineCamera();

	// Spawn 트랙 값에 따라 템플릿 액터를 스폰하거나 지운다
	void UpdateSpawnTrack(int32 BindingIndex, float Time);
	// 시퀀스의 이펙트 프리셋을 월드의 EffectManager에 등록한다 (Play 때)
	void RegisterEffects();


	// 처음 건드리기 직전에 원래 값을 저장해 둔다 (UE의 Pre-Animated State)
	void SavePreAnimatedTransform(USceneComponent* Component);
	void SavePreAnimatedProperty(UObject* Object, const FProperty* Property);

	struct FPreAnimatedValue
	{
		FWeakObjectPtr Object;
		const FProperty* Property = nullptr;     // nullptr면 Transform
		FTransform Transform;
		float FloatValue = 0.0f;
		bool BoolValue = false;
	};
	TArray<FPreAnimatedValue> PreAnimatedValues;

	// Sequence->Bindings와 같은 순서
	TArray<TArray<FWeakObjectPtr>> BoundActors;
	TArray<FWeakObjectPtr> SpawnedActors;
	// Sequence->Bindings와 같은 순서. 1이면 Spawn 트랙이 그 바인딩의 스폰을 맡는다
	// (TArray<bool>은 std::vector<bool>이라 원소 참조를 돌려주지 못해 uint8로 둔다)
	TArray<uint8> SpawnTrackControlled;
	bool bBindingsResolved = false;

	// 시네 카메라로 바꾼 동안만 유효하다
	FWeakObjectPtr CineCamera;
	FWeakObjectPtr PreviousViewTarget;
	bool bCineCameraActive = false;

	ULevelSequence* Sequence = nullptr;
	UWorld* World = nullptr;
	uint32 WorldUUID = 0;
	FMovieSceneSequencePlaybackSettings Settings;

	float CurrentTime = 0.0f;
	bool bPlaying = false;
	// 지금까지 되감은 횟수. LoopCount와 비교한다.
	int32 LoopsDone = 0;
};
