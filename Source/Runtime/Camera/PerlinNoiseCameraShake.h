#pragma once

#include "Camera/CameraShakeBase.h"

// Concrete shake using a Perlin noise pattern as its RootShakePattern.
class UPerlinNoiseCameraShake : public UCameraShakeBase
{
	DECLARE_CLASS(UPerlinNoiseCameraShake, UCameraShakeBase)

public:
	UPerlinNoiseCameraShake();
	void Configure(float Strength, float Duration, float Frequency = 10.0f);
};
