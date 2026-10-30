#include "EnginePCH.h"
#include "Engine/TimeDilationManager.h"
#include "Math/EngineMath.h"

namespace
{
	// 배율 0을 허용해 히트스톱이 완전히 멈출 수 있게 한다. 위로는 실수로 너무 빨라지지 않게 막는다.
	constexpr float MaxTimeDilation = 10.0f;
	constexpr float MinSlomoDilation = 0.01f;
	constexpr float SlomoBlendInTime = 0.1f;
	constexpr float SlomoBlendOutTime = 0.3f;
}

void FTimeDilationManager::Tick(float RealDeltaTime)
{
	UpdateSlomo(RealDeltaTime);

	if (HitStopRemainingTime > 0.0f)
	{
		HitStopRemainingTime -= RealDeltaTime;
		if (HitStopRemainingTime <= 0.0f)
		{
			HitStopRemainingTime = 0.0f;
			HitStopFactor = 1.0f;
		}
	}
}

float FTimeDilationManager::GetTimeDilation() const
{
	return FMath::Clamp(BaseDilation * SlomoFactor * HitStopFactor, 0.0f, MaxTimeDilation);
}

void FTimeDilationManager::SetBaseTimeDilation(float InDilation)
{
	BaseDilation = FMath::Clamp(InDilation, 0.0f, MaxTimeDilation);
}

void FTimeDilationManager::StartSlomo(float TargetDilation, float Duration)
{
	if (Duration <= 0.0f)
		return;

	SlomoStartFactor = SlomoFactor;
	SlomoTargetFactor = FMath::Clamp(TargetDilation, MinSlomoDilation, MaxTimeDilation);
	SlomoHoldDuration = Duration;
	SlomoElapsedTime = 0.0f;
	SlomoPhase = ESlomoPhase::BlendIn;
}

void FTimeDilationManager::StartHitStop(float Duration, float Dilation)
{
	if (Duration <= 0.0f)
		return;

	HitStopRemainingTime = FMath::Max(HitStopRemainingTime, Duration);
	HitStopFactor = FMath::Clamp(Dilation, 0.0f, 1.0f);
}

void FTimeDilationManager::UpdateSlomo(float RealDeltaTime)
{
	if (SlomoPhase == ESlomoPhase::Inactive)
		return;

	SlomoElapsedTime += RealDeltaTime;

	// 한 프레임이 단계 경계를 넘으면 남은 시간을 다음 단계에 사용한다.
	while (SlomoPhase != ESlomoPhase::Inactive)
	{
		switch (SlomoPhase)
		{
		case ESlomoPhase::BlendIn:
		{
			const float Alpha = FMath::Clamp(SlomoElapsedTime / SlomoBlendInTime, 0.0f, 1.0f);
			SlomoFactor = SlomoStartFactor + (SlomoTargetFactor - SlomoStartFactor) * Alpha;

			if (SlomoElapsedTime < SlomoBlendInTime)
				return;

			SlomoElapsedTime -= SlomoBlendInTime;
			SlomoPhase = ESlomoPhase::Holding;
			break;
		}

		case ESlomoPhase::Holding:
		{
			if (SlomoElapsedTime < SlomoHoldDuration)
				return;

			SlomoElapsedTime -= SlomoHoldDuration;
			SlomoPhase = ESlomoPhase::BlendOut;
			break;
		}

		case ESlomoPhase::BlendOut:
		{
			const float Alpha = FMath::Clamp(SlomoElapsedTime / SlomoBlendOutTime, 0.0f, 1.0f);
			SlomoFactor = SlomoTargetFactor + (1.0f - SlomoTargetFactor) * Alpha;

			if (SlomoElapsedTime < SlomoBlendOutTime)
				return;

			SlomoFactor = 1.0f;
			SlomoElapsedTime = 0.0f;
			SlomoPhase = ESlomoPhase::Inactive;
			return;
		}

		case ESlomoPhase::Inactive:
			return;
		}
	}
}
