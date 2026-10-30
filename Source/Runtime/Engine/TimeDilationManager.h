#pragma once

#include "Core/Types.h"

// 월드의 시간 배율을 요인별로 따로 들고 곱해서 최종 배율을 만든다.
// 최종 배율 = Base(직접 설정) × Slomo × HitStop. 요인끼리 서로 덮어쓰지 않는다.
// UWorld가 소유한다. 슬로모·히트스톱 타이머는 배율이 0이어도 풀려야 하므로 실제 시간으로 잰다.
class FTimeDilationManager
{
public:
	// UWorld::Tick 입구에서 배율을 곱하기 전에 실제 프레임 시간으로 부른다.
	void Tick(float RealDeltaTime);

	float GetTimeDilation() const;

	void SetBaseTimeDilation(float InDilation);
	float GetBaseTimeDilation() const { return BaseDilation; }

	// 0.1초에 걸쳐 TargetDilation으로 내려가 Duration 동안 유지한 뒤 0.3초에 걸쳐 돌아온다.
	// 진행 중에 다시 부르면 현재 배율에서 새 목표로 이어서 블렌드한다.
	void StartSlomo(float TargetDilation, float Duration);

	// Duration 동안 배율을 Dilation으로 즉시 바꾼다. 블렌드 없이 끊어야 타격이 멈춘 듯 보인다.
	// 진행 중에 다시 부르면 남은 시간이 더 긴 쪽을 따르고 배율은 새 값으로 바꾼다.
	void StartHitStop(float Duration, float Dilation = 0.0f);
	bool IsHitStopActive() const { return HitStopRemainingTime > 0.0f; }

private:
	void UpdateSlomo(float RealDeltaTime);

	float BaseDilation = 1.0f;

	enum class ESlomoPhase : uint8
	{
		Inactive,
		BlendIn,
		Holding,
		BlendOut
	};
	ESlomoPhase SlomoPhase = ESlomoPhase::Inactive;

	float SlomoFactor = 1.0f; // 현재 슬로모 요인
	float SlomoElapsedTime = 0.0f; // 현재 단계의 경과 시간
	float SlomoHoldDuration = 0.0f; // 목표 배율 유지 시간
	float SlomoStartFactor = 1.0f; // BlendIn 시작 시의 요인
	float SlomoTargetFactor = 1.0f; // 목표 요인

	float HitStopRemainingTime = 0.0f;
	float HitStopFactor = 1.0f; // 히트스톱 중 요인. 끝나면 1
};
