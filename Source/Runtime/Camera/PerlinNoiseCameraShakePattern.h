#pragma once

#include "Camera/CameraShakePattern.h"

// 부드러운 랜덤(펄린 노이즈)으로 흔든다. 사인파처럼 규칙적이지 않아 손떨림·지진 느낌이 난다.
class UPerlinNoiseCameraShakePattern : public UCameraShakePattern
{
	DECLARE_CLASS(UPerlinNoiseCameraShakePattern, UCameraShakePattern)

public:
	void Configure(float InStrength, float InDuration, float InFrequency);
	void StartShakePattern() override;
	void UpdateShakePattern(float DeltaTime, FCameraShakePatternUpdateResult& OutResult) override;
	void StopShakePattern() override { bStopped = true; }
	bool IsFinished() const override { return bStopped || ElapsedTime >= Duration; }

private:
	float Strength = 0.1f;
	float Duration = 0.5f;
	float Frequency = 10.0f;   // 초당 노이즈 격자를 몇 칸 지나가는지
	float ElapsedTime = 0.0f;
	// 축마다 노이즈의 다른 구간을 읽어야 Y·Z가 같은 모양으로 움직이지 않는다
	float OffsetY = 0.0f;
	float OffsetZ = 0.0f;
	bool bStopped = true;
};
