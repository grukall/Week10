#pragma once

#include "Camera/CameraShakeBase.h"

// Concrete shake using a wave oscillator as its RootShakePattern.
class UWaveCameraShake : public UCameraShakeBase
{
	DECLARE_CLASS(UWaveCameraShake, UCameraShakeBase)

public:
	UWaveCameraShake();
	void Configure(float Strength, float Duration, float Frequency = 20.0f);
};
