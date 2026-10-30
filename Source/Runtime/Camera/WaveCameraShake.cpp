#include "EnginePCH.h"
#include "Camera/WaveCameraShake.h"
#include "Camera/WaveOscillatorCameraShakePattern.h"

UWaveCameraShake::UWaveCameraShake()
{
	SetRootShakePattern(NewObject<UWaveOscillatorCameraShakePattern>(this));
}

void UWaveCameraShake::Configure(float Strength, float Duration, float Frequency)
{
	if (auto* Pattern = Cast<UWaveOscillatorCameraShakePattern>(GetRootShakePattern()))
		Pattern->Configure(Strength, Duration, Frequency);
}
