#include "EnginePCH.h"
#include "Audio/AudioManager.h"
#include "miniaudio.h"
#include <algorithm>

struct FAudioManager::FImpl
{
	struct FSound
	{
		ma_sound Sound{};
		bool bInitialized = false;

		~FSound()
		{
			if (bInitialized)
				ma_sound_uninit(&Sound);
		}
	};

	ma_engine Engine{};
	ma_sound_group BGMGroup{};
	ma_sound_group SFXGroup{};
	bool bEngineInitialized = false;
	bool bBGMGroupInitialized = false;
	bool bSFXGroupInitialized = false;
	TMap<uint32, std::unique_ptr<FSound>> Effects;
	std::unique_ptr<FSound> BGM;
	FString BGMPath;

	~FImpl()
	{
		Effects.Reset();
		BGM.reset();
		if (bSFXGroupInitialized)
			ma_sound_group_uninit(&SFXGroup);
		if (bBGMGroupInitialized)
			ma_sound_group_uninit(&BGMGroup);
		if (bEngineInitialized)
			ma_engine_uninit(&Engine);
	}

	std::unique_ptr<FSound> PlaySound(const FString& Path, ma_sound_group* Group, float Volume, bool bLoop)
	{
		auto Sound = std::make_unique<FSound>();
		ma_result Result = ma_sound_init_from_file(&Engine, Path.c_str(),
			MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, Group, nullptr, &Sound->Sound);
		if (Result == MA_SUCCESS)
		{
			Sound->bInitialized = true;
			ma_sound_set_volume(&Sound->Sound, std::clamp(Volume, 0.0f, 1.0f));
			ma_sound_set_looping(&Sound->Sound, bLoop);
			Result = ma_sound_start(&Sound->Sound);
		}
		if (Result != MA_SUCCESS)
		{
			HTR_LOG(Warning, "[Audio] failed to play {}: {}", Path, ma_result_description(Result));
			return nullptr;
		}
		return Sound;
	}
};

FAudioManager::FAudioManager() = default;
FAudioManager::~FAudioManager() = default;

FAudioManager& FAudioManager::Get()
{
	static FAudioManager Instance;
	return Instance;
}

bool FAudioManager::Initialize()
{
	if (Impl)
		return true;

	auto State = std::make_unique<FImpl>();
	ma_result Result = ma_engine_init(nullptr, &State->Engine);
	State->bEngineInitialized = Result == MA_SUCCESS;
	if (Result == MA_SUCCESS)
	{
		Result = ma_sound_group_init(&State->Engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &State->BGMGroup);
		State->bBGMGroupInitialized = Result == MA_SUCCESS;
	}
	if (Result == MA_SUCCESS)
	{
		Result = ma_sound_group_init(&State->Engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &State->SFXGroup);
		State->bSFXGroupInitialized = Result == MA_SUCCESS;
	}
	if (Result != MA_SUCCESS)
	{
		HTR_LOG(Warning, "[Audio] initialization failed; continuing without audio: {}", ma_result_description(Result));
		return false;
	}
	Impl = std::move(State);
	SetBGMVolume(BGMVolume);
	SetSFXVolume(SFXVolume);
	return true;
}

void FAudioManager::Shutdown()
{
	Impl.reset();
}

void FAudioManager::Update()
{
	if (!Impl)
		return;

	TArray<uint32> Finished;
	for (const auto& [Id, Effect] : Impl->Effects)
	{
		if (ma_sound_at_end(&Effect->Sound))
			Finished.Add(Id);
	}
	for (uint32 Id : Finished)
		Impl->Effects.Remove(Id);
}

uint32 FAudioManager::Play(const FString& Path, float Volume)
{
	if (!Impl)
		return 0;
	auto Sound = Impl->PlaySound(Path, &Impl->SFXGroup, Volume, false);
	if (!Sound)
		return 0;
	const uint32 Id = NextSoundId++;
	Impl->Effects[Id] = std::move(Sound);
	return Id;
}

bool FAudioManager::IsPlaying(uint32 Id) const
{
	if (!Impl)
		return false;
	const auto* Sound = Impl->Effects.Find(Id);
	return Sound && ma_sound_is_playing(&(*Sound)->Sound);
}

void FAudioManager::Stop(uint32 Id)
{
	if (Impl)
		Impl->Effects.Remove(Id);
}

float FAudioManager::GetPosition(uint32 Id) const
{
	float Seconds = 0.0f;
	if (Impl)
	{
		if (const auto* Sound = Impl->Effects.Find(Id))
			ma_sound_get_cursor_in_seconds(&(*Sound)->Sound, &Seconds);
	}
	return Seconds;
}

bool FAudioManager::PlayBGM(const FString& Path, float Volume)
{
	if (!Impl)
		return false;
	if (Impl->BGM && Impl->BGMPath == Path && ma_sound_is_playing(&Impl->BGM->Sound))
		return true;

	StopBGM();
	Impl->BGM = Impl->PlaySound(Path, &Impl->BGMGroup, Volume, true);
	if (!Impl->BGM)
		return false;
	Impl->BGMPath = Path;
	return true;
}

void FAudioManager::StopBGM()
{
	if (Impl)
	{
		Impl->BGM.reset();
		Impl->BGMPath.clear();
	}
}

void FAudioManager::StopAll()
{
	if (Impl)
		Impl->Effects.Reset();
	StopBGM();
}

void FAudioManager::SetBGMVolume(float Volume)
{
	BGMVolume = std::clamp(Volume, 0.0f, 1.0f);
	if (Impl)
		ma_sound_group_set_volume(&Impl->BGMGroup, BGMVolume);
}

void FAudioManager::SetSFXVolume(float Volume)
{
	SFXVolume = std::clamp(Volume, 0.0f, 1.0f);
	if (Impl)
		ma_sound_group_set_volume(&Impl->SFXGroup, SFXVolume);
}
