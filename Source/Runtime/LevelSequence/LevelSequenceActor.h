#pragma once

#include "GameFramework/Actor.h"
#include "Containers/Map.h"
#include "LevelSequencePlayer.h"

// 시퀀스 재생기 (UE ALevelSequenceActor). SequencePath의 에셋을 읽어 플레이어에 넘긴다.
// 쓰는 방법은 두 가지다.
//   레벨에 배치: 그 맵 전용 연출. bAutoPlay로 시작하거나 스크립트가 찾아서 Play를 부른다.
//   런타임 생성: 맵과 상관없는 연출. C++은 SpawnAndPlay, Lua는 PlaySequence(path, options)로 띄운다.
// 플레이어는 BeginPlay 이후(게임·PIE)나 SpawnAndPlay에서만 만든다. 에디터 미리보기는 Sequencer가 따로 플레이어를 만든다.
// Lua에서는 Play / Pause / Stop / IsPlaying / SetPlaybackPosition 으로 제어한다.
class ALevelSequenceActor : public AActor
{
	DECLARE_CLASS(ALevelSequenceActor, AActor)
	REFLECT_START(ClassName)
		PROPERTY_FLAGS(SequencePath, CPF_EditAnywhere)
		PROPERTY_FLAGS(bAutoPlay, CPF_EditAnywhere)
		PROPERTY_FLAGS(bRestoreState, CPF_EditAnywhere)
		PROPERTY_FLAGS(LoopCount, CPF_EditAnywhere)
		PROPERTY_FLAGS(PlayRate, CPF_EditAnywhere)
		PROPERTY_FLAGS(StartTime, CPF_EditAnywhere)
		PROPERTY_FLAGS(bDestroyOnFinish, CPF_EditAnywhere)
		FUNCTION(Play)
		FUNCTION(Pause)
		FUNCTION(Stop)
		FUNCTION(IsPlaying)
		FUNCTION(SetPlaybackPosition)
		FUNCTION(PlaySound)
		FUNCTION(StopSound)
		FUNCTION(SpawnEffect)
		FUNCTION(StartCameraFade)
	REFLECT_END()

public:
	ALevelSequenceActor() = default;
	virtual ~ALevelSequenceActor();

	virtual void BeginPlay() override;
	virtual void EndPlay() override;

	// 작업 디렉터리 기준 경로 (예: Assets/Sequences/Intro.sequence)
	FString SequencePath;

	// FMovieSceneSequencePlaybackSettings를 디테일 패널에 펼친 것. 비트필드는 리플렉션할 수 없어 따로 둔다.
	bool bAutoPlay = false;
	bool bRestoreState = true;
	int32 LoopCount = 0;
	float PlayRate = 1.0f;
	float StartTime = 0.0f;

	// 끝까지 재생하면 이 액터를 Destroy 한다. 런타임에 띄우고 잊어버리는 연출용이다.
	bool bDestroyOnFinish = false;

	// Path의 시퀀스를 읽어 재생하는 액터를 스폰하고 바로 Play 한다 (UE CreateLevelSequencePlayer의 경로 버전).
	// 씬에 저장되지 않는 액터다. 에셋은 액터가 읽고 소유한다. 읽기에 실패하면 액터를 지우고 nullptr.
	static ALevelSequenceActor* SpawnAndPlay(UWorld* World, const FString& Path, const FMovieSceneSequencePlaybackSettings& Settings, bool bInDestroyOnFinish = true);

	FMovieSceneSequencePlaybackSettings GetPlaybackSettings() const;
	void SetPlaybackSettings(const FMovieSceneSequencePlaybackSettings& Settings);

	// 이미 읽은 에셋을 쓴다 (소유하지 않는다). SequencePath보다 우선한다.
	void SetSequence(ULevelSequence* InSequence);
	ULevelSequence* GetSequence() const { return Sequence; }
	ULevelSequencePlayer* GetSequencePlayer() const { return SequencePlayer; }

	void Play();
	void Pause();
	void Stop();
	bool IsPlaying() const;
	void SetPlaybackPosition(float Time);

	// 디렉터 함수: 시퀀스의 마스터 Event 트랙이 리플렉션으로 부른다 (UE의 Director Blueprint 역할).
	// 효과음을 튼다. 같은 경로를 다시 틀면 이전 것은 놓고 새 것을 기억한다
	void PlaySound(const FString& Path, float Volume);
	// PlaySound로 튼 Path 소리를 멈춘다
	void StopSound(const FString& Path);
	// 월드의 EffectManager로 이펙트를 Location에 한 번 터뜨린다. 시퀀스의 Effects 프리셋을 쓸 수 있다
	void SpawnEffect(const FString& Name, const FVector& Location);
	// 0번 플레이어 화면을 Duration 동안 검게 FromAlpha → ToAlpha로 덮는다 (UE Fade 트랙 대신)
	void StartCameraFade(float FromAlpha, float ToAlpha, float Duration);

private:
	// 플레이어가 없으면 만들고, 지금 에셋과 설정으로 다시 초기화한다
	void InitializePlayer();
	// 아직 에셋이 없으면 SequencePath에서 읽는다. 에셋이 준비됐으면 true
	bool LoadSequenceFromPath();
	// 플레이어의 OnFinished 구독자
	void HandleSequenceFinished();

	ULevelSequence* Sequence = nullptr;
	uint32 SequenceUUID = 0;
	// SequencePath로 직접 읽은 에셋이면 소멸할 때 지운다
	bool bOwnsSequence = false;

	ULevelSequencePlayer* SequencePlayer = nullptr;
	// 소멸자에서 아직 살아 있는지 확인하는 데 쓴다 (엔진 종료 때 먼저 지워질 수 있다)
	uint32 SequencePlayerUUID = 0;

	// PlaySound로 튼 소리. 경로 → FAudioManager 재생 Id
	TMap<FString, uint32> PlayingSounds;
};
