#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Audio/AudioManager.h"
#include <sol/sol.hpp>

void RegisterLuaAudioBindings(sol::state& Lua)
{
	sol::table Audio = Lua.create_named_table("Audio");
	Audio.set_function("Play", [](const FString& Path, sol::optional<float> Volume)
		{ return FAudioManager::Get().Play(Path, Volume.value_or(1.0f)); });
	Audio.set_function("IsPlaying", [](uint32 Id) { return FAudioManager::Get().IsPlaying(Id); });
	Audio.set_function("GetPosition", [](uint32 Id) { return FAudioManager::Get().GetPosition(Id); });
	Audio.set_function("Stop", [](uint32 Id) { FAudioManager::Get().Stop(Id); });
	Audio.set_function("PlayBGM", [](const FString& Path, sol::optional<float> Volume)
		{ return FAudioManager::Get().PlayBGM(Path, Volume.value_or(1.0f)); });
	Audio.set_function("StopBGM", []() { FAudioManager::Get().StopBGM(); });
	Audio.set_function("SetBGMVolume", [](float Volume) { FAudioManager::Get().SetBGMVolume(Volume); });
	Audio.set_function("GetBGMVolume", []() { return FAudioManager::Get().GetBGMVolume(); });
	Audio.set_function("SetSFXVolume", [](float Volume) { FAudioManager::Get().SetSFXVolume(Volume); });
	Audio.set_function("GetSFXVolume", []() { return FAudioManager::Get().GetSFXVolume(); });
}
