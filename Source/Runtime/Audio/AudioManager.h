#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include <memory>

// 월드와 독립적인 2D 오디오. 씬 전환에는 유지하고 PIE 종료에는 StopAll한다.
class FAudioManager
{
public:
	static FAudioManager& Get();
	bool Initialize();
	void Shutdown();
	void Update();
	void StopAll();

	uint32 Play(const FString& Path, float Volume = 1.0f);
	bool IsPlaying(uint32 Id) const;
	float GetPosition(uint32 Id) const;
	void Stop(uint32 Id);
	bool PlayBGM(const FString& Path, float Volume = 1.0f);
	void StopBGM();

	void SetBGMVolume(float Volume);
	float GetBGMVolume() const { return BGMVolume; }
	void SetSFXVolume(float Volume);
	float GetSFXVolume() const { return SFXVolume; }

private:
	FAudioManager();
	~FAudioManager();
	FAudioManager(const FAudioManager&) = delete;
	FAudioManager& operator=(const FAudioManager&) = delete;

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	uint32 NextSoundId = 1;
	float BGMVolume = 0.6f;
	float SFXVolume = 1.0f;
};
