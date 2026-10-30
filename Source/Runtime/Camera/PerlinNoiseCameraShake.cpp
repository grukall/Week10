#include "EnginePCH.h"
#include "Camera/PerlinNoiseCameraShake.h"
#include "Camera/PerlinNoiseCameraShakePattern.h"

UPerlinNoiseCameraShake::UPerlinNoiseCameraShake()
{
	SetRootShakePattern(NewObject<UPerlinNoiseCameraShakePattern>(this));
}

void UPerlinNoiseCameraShake::Configure(float Strength, float Duration, float Frequency)
{
	if (auto* Pattern = Cast<UPerlinNoiseCameraShakePattern>(GetRootShakePattern()))
		Pattern->Configure(Strength, Duration, Frequency);
}
