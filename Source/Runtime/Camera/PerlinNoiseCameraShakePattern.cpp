#include "EnginePCH.h"
#include "Camera/PerlinNoiseCameraShakePattern.h"

#include <cmath>

namespace
{
	// 정수 격자점마다 고정된 기울기(-1 ~ 1)
	float LatticeGradient(int32 X)
	{
		uint32 H = static_cast<uint32>(X) * 0x27d4eb2du;
		H ^= H >> 15;
		H *= 0x85ebca6bu;
		H ^= H >> 13;
		return static_cast<float>(H & 0xffffu) / 32767.5f - 1.0f;
	}

	// 1D 펄린 노이즈: 양쪽 격자점의 기울기로 만든 직선 두 개를 부드러운 곡선(fade)으로 섞는다. 대략 -1 ~ 1
	float PerlinNoise1D(float X)
	{
		const float Floor = std::floor(X);
		const int32 I = static_cast<int32>(Floor);
		const float F = X - Floor;
		const float Left = LatticeGradient(I) * F;
		const float Right = LatticeGradient(I + 1) * (F - 1.0f);
		const float U = F * F * F * (F * (F * 6.0f - 15.0f) + 10.0f);
		return (Left + (Right - Left) * U) * 2.0f;
	}
}

void UPerlinNoiseCameraShakePattern::Configure(float InStrength, float InDuration, float InFrequency)
{
	Strength = FMath::Max(InStrength, 0.0f);
	Duration = FMath::Max(InDuration, 0.0f);
	Frequency = FMath::Max(InFrequency, 0.0f);
}

void UPerlinNoiseCameraShakePattern::StartShakePattern()
{
	// 흔들 때마다 노이즈의 다른 구간을 읽어 매번 다른 모양이 나오게 한다
	static int32 ShakeCount = 0;
	++ShakeCount;
	OffsetY = (LatticeGradient(ShakeCount * 2) + 1.0f) * 500.0f;
	OffsetZ = (LatticeGradient(ShakeCount * 2 + 1) + 1.0f) * 500.0f + 1000.0f;
	ElapsedTime = 0.0f;
	bStopped = false;
}

void UPerlinNoiseCameraShakePattern::UpdateShakePattern(
	float DeltaTime, FCameraShakePatternUpdateResult& OutResult)
{
	OutResult = {};
	if (IsFinished())
		return;
	ElapsedTime += DeltaTime;
	if (IsFinished())
		return;
	const float Weight = 1.0f - ElapsedTime / Duration;
	const float Amplitude = Strength * Weight;
	const float T = ElapsedTime * Frequency;
	OutResult.Location = FVector(0.0f,
		PerlinNoise1D(T + OffsetY) * Amplitude,
		PerlinNoise1D(T + OffsetZ) * Amplitude);
}
