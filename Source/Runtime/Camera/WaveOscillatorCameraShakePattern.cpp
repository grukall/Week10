#include "EnginePCH.h"
#include "Camera/WaveOscillatorCameraShakePattern.h"

void UWaveOscillatorCameraShakePattern::Configure(float InStrength, float InDuration, float InFrequency)
{
	Strength = FMath::Max(InStrength, 0.0f);
	Duration = FMath::Max(InDuration, 0.0f);
	Frequency = FMath::Max(InFrequency, 0.0f);
}

void UWaveOscillatorCameraShakePattern::StartShakePattern()
{
	ElapsedTime = 0.0f;
	bStopped = false;
}

void UWaveOscillatorCameraShakePattern::UpdateShakePattern(
	float DeltaTime, FCameraShakePatternUpdateResult& OutResult)
{
	OutResult = {};
	if (IsFinished())
		return;
	ElapsedTime += DeltaTime;
	if (IsFinished())
		return;
	OutResult.Location = EvaluateOffset(Strength, Duration, Frequency, ElapsedTime);
}

FVector UWaveOscillatorCameraShakePattern::EvaluateOffset(float Strength, float Duration, float Frequency, float ElapsedTime)
{
	if (Duration <= 0.0f || ElapsedTime < 0.0f || ElapsedTime >= Duration)
		return FVector(0.0f, 0.0f, 0.0f);

	const float Weight = 1.0f - ElapsedTime / Duration;
	const float Amplitude = FMath::Max(Strength, 0.0f) * Weight;
	const float Phase = 2.0f * PI * FMath::Max(Frequency, 0.0f) * ElapsedTime;
	return FVector(0.0f,
		std::sin(Phase) * Amplitude,
		std::sin(Phase * 1.3f) * Amplitude * 0.5f);
}
