#pragma once
#include <sol/forward.hpp>

class UObject;
class UWorld;

// 스크립트 주인(액터, 또는 컴포넌트의 소유 액터)의 게임 월드. 게임 월드가 아니면(에디터) nullptr.
// 분야별 바인딩이 같이 쓴다.
UWorld* FindGameWorldForObject(UObject* Object);

// C++ 타입을 Lua VM에 등록한다. VM을 만든 직후 한 번만 호출한다.
// 각 액터의 environment는 전역을 fallback으로 보므로 모든 스크립트가 같은 바인딩을 쓴다.
void RegisterLuaBindings(sol::state& Lua);

// 분야별 등록 함수. 타입 의존 순서대로 RegisterLuaBindings가 호출한다.
void RegisterLuaMathBindings(sol::state& Lua);
void RegisterLuaObjectBindings(sol::state& Lua);
void RegisterLuaInputBindings(sol::state& Lua);
void RegisterLuaGameBindings(sol::state& Lua);
void RegisterLuaAudioBindings(sol::state& Lua);

// HUD 그리기 함수 등록 (DrawHUDRect, DrawHUDText, GetHUDSize)
void RegisterLuaHUDAPI(sol::environment& Env, UObject* Owner);

// 입력 함수 등록 (EnableInput, DisableInput, GetPlayerController)
void RegisterLuaInputAPI(sol::environment& Env, UObject* Owner);

// 이펙트 함수 등록 (SpawnEffect, SpawnEffectAttached, SpawnTextEffect, StopEffect, IsEffectActive, RegisterEffect)
void RegisterLuaEffectAPI(sol::environment& Env, UObject* Owner);

// 시간 배율 함수 등록 (HitStop, Slomo, SetTimeDilation, GetTimeDilation)
void RegisterLuaTimeAPI(sol::environment& Env, UObject* Owner);

// 카메라 함수 등록 (CameraShake, NoiseShake, StopCameraShakes, GetMainCamera, SetViewTarget, GetViewTarget, IsViewTargetBlending,
// GetCameraLocation, GetCameraRotation, SpawnCamera, SpawnMeshActor, FindLookAtRotation, GetActorBounds, GetComponentBounds)
void RegisterLuaCameraAPI(sol::environment& Env, UObject* Owner);

// 레벨 시퀀스 함수 등록 (PlaySequence)
void RegisterLuaSequenceAPI(sol::environment& Env, UObject* Owner);
