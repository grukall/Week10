#include "EnginePCH.h"
#include "LevelSequenceActor.h"

#include "Engine/World.h"
#include "Audio/AudioManager.h"
#include "GameFramework/PlayerController.h"
#include "PlayerCameraManager.h"

namespace
{
	// 엔진 종료 때 FEngineLoop가 모든 UObject를 배열 끝(나중에 만든 것)부터 지운다.
	// 그러면 이 액터가 BeginPlay에서 만든 플레이어·에셋이 액터보다 먼저 지워질 수 있으므로,
	// 아직 살아 있을 때만 지운다 (UUID 맵에서 같은 객체를 찾으면 살아 있다).
	void DeleteIfAlive(UObject* Object, uint32 ObjectUUID)
	{
		if (Object && FindObjectByUUID(ObjectUUID) == Object)
			delete Object;
	}
}

ALevelSequenceActor::~ALevelSequenceActor()
{
	DeleteIfAlive(SequencePlayer, SequencePlayerUUID);
	SequencePlayer = nullptr;

	if (bOwnsSequence)
		DeleteIfAlive(Sequence, SequenceUUID);
	Sequence = nullptr;
}

ALevelSequenceActor* ALevelSequenceActor::SpawnAndPlay(UWorld* World, const FString& Path, const FMovieSceneSequencePlaybackSettings& Settings, bool bInDestroyOnFinish)
{
	if (!World)
		return nullptr;

	ALevelSequenceActor* Actor = World->SpawnActor<ALevelSequenceActor>();
	if (!Actor)
		return nullptr;

	// 실행 중에 띄운 연출이므로 씬에 저장하지 않는다
	Actor->SetFlags(EObjectFlags::RF_Transient);
	Actor->SequencePath = Path;
	Actor->bDestroyOnFinish = bInDestroyOnFinish;
	Actor->SetPlaybackSettings(Settings);

	// BeginPlay는 다음 프레임에 오므로 기다리지 않고 지금 읽어 바로 재생한다
	if (!Actor->LoadSequenceFromPath())
	{
		Actor->Destroy();
		return nullptr;
	}

	Actor->InitializePlayer();
	Actor->Play();
	return Actor;
}

void ALevelSequenceActor::BeginPlay()
{
	Super::BeginPlay();

	if (!LoadSequenceFromPath())
		return;

	// SetSequence·SpawnAndPlay로 만들었으면 이미 초기화했다. 그 사이 Play를 불렀을 수 있으니 다시 하지 않는다.
	if (!SequencePlayer)
		InitializePlayer();
	if (bAutoPlay && !SequencePlayer->IsPlaying())
		Play();
}

void ALevelSequenceActor::EndPlay()
{
	// 시점을 돌려주고 스폰한 액터를 지운다. 소멸자에서는 액터를 지울 수 없으므로 여기서 한다.
	// 끝까지 재생하고 Keep State로 멈춘 뒤 지워지는 경우(bDestroyOnFinish)는 바꾼 값을 그대로 둔다.
	if (SequencePlayer)
		SequencePlayer->ReleaseBindings(bRestoreState || SequencePlayer->IsPlaying());

	Super::EndPlay();
}

bool ALevelSequenceActor::LoadSequenceFromPath()
{
	if (Sequence)
		return true;
	if (SequencePath.empty())
		return false;

	Sequence = ULevelSequence::LoadFromFile(SequencePath);
	bOwnsSequence = Sequence != nullptr;
	SequenceUUID = Sequence ? Sequence->GetUUID() : 0;
	if (!Sequence)
		HTR_LOG(Warning, "LevelSequenceActor '{}': failed to load '{}'", GetName(), SequencePath);
	return Sequence != nullptr;
}

FMovieSceneSequencePlaybackSettings ALevelSequenceActor::GetPlaybackSettings() const
{
	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = bAutoPlay;
	Settings.bRestoreState = bRestoreState;
	Settings.LoopCount = LoopCount;
	Settings.PlayRate = PlayRate;
	Settings.StartTime = StartTime;
	return Settings;
}

void ALevelSequenceActor::SetPlaybackSettings(const FMovieSceneSequencePlaybackSettings& Settings)
{
	bAutoPlay = Settings.bAutoPlay;
	bRestoreState = Settings.bRestoreState;
	LoopCount = Settings.LoopCount;
	PlayRate = Settings.PlayRate;
	StartTime = Settings.StartTime;

	if (SequencePlayer)
		SequencePlayer->GetSettings() = Settings;
}

void ALevelSequenceActor::SetSequence(ULevelSequence* InSequence)
{
	if (Sequence == InSequence)
		return;

	if (bOwnsSequence)
		DeleteIfAlive(Sequence, SequenceUUID);
	Sequence = InSequence;
	SequenceUUID = Sequence ? Sequence->GetUUID() : 0;
	bOwnsSequence = false;

	InitializePlayer();
}

void ALevelSequenceActor::InitializePlayer()
{
	if (!SequencePlayer)
	{
		SequencePlayer = NewObject<ULevelSequencePlayer>(this);
		SequencePlayerUUID = SequencePlayer->GetUUID();
		// 플레이어는 이 액터가 소유하므로 구독을 따로 풀 필요가 없다
		SequencePlayer->OnFinished.AddDynamic(this, &ALevelSequenceActor::HandleSequenceFinished);
	}

	SequencePlayer->Initialize(Sequence, GetWorld(), GetPlaybackSettings());
}

void ALevelSequenceActor::HandleSequenceFinished()
{
	// 플레이어 Tick 안에서 불리지만 Destroy는 Frame 끝에서 delete 하므로 안전하다
	if (bDestroyOnFinish)
		Destroy();
}

void ALevelSequenceActor::Play()
{
	if (SequencePlayer)
		SequencePlayer->Play();
}

void ALevelSequenceActor::Pause()
{
	if (SequencePlayer)
		SequencePlayer->Pause();
}

void ALevelSequenceActor::Stop()
{
	if (SequencePlayer)
		SequencePlayer->Stop();
}

bool ALevelSequenceActor::IsPlaying() const
{
	return SequencePlayer && SequencePlayer->IsPlaying();
}

void ALevelSequenceActor::SetPlaybackPosition(float Time)
{
	if (SequencePlayer)
		SequencePlayer->SetPlaybackPosition(Time);
}

void ALevelSequenceActor::PlaySound(const FString& Path, float Volume)
{
	PlayingSounds.Add(Path, FAudioManager::Get().Play(Path, Volume));
}

void ALevelSequenceActor::StopSound(const FString& Path)
{
	if (const uint32* Id = PlayingSounds.Find(Path))
	{
		FAudioManager::Get().Stop(*Id);
		PlayingSounds.Remove(Path);
	}
}

void ALevelSequenceActor::SpawnEffect(const FString& Name, const FVector& Location)
{
	if (UWorld* OwningWorld = GetWorld())
		OwningWorld->GetEffectManager().SpawnEffect(FName(Name), Location);
}

void ALevelSequenceActor::StartCameraFade(float FromAlpha, float ToAlpha, float Duration)
{
	UWorld* OwningWorld = GetWorld();
	APlayerController* PC = OwningWorld ? OwningWorld->GetPlayerController(0) : nullptr;
	if (APlayerCameraManager* PCM = PC ? PC->GetCameraPlayerManager() : nullptr)
		PCM->StartCameraFade(FromAlpha, ToAlpha, Duration, FLinearColor(0, 0, 0, 1), ToAlpha > 0.0f);   // 덮는 쪽이면 끝나도 유지
}
