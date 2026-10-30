#pragma once

#include "Camera/CameraShakePattern.h"

class UWaveOscillatorCameraShakePattern : public UCameraShakePattern
{
	DECLARE_CLASS(UWaveOscillatorCameraShakePattern, UCameraShakePattern)

public:
	void Configure(float InStrength, float InDuration, float InFrequency);
	void StartShakePattern() override;
	void UpdateShakePattern(float DeltaTime, FCameraShakePatternUpdateResult& OutResult) override;
	void StopShakePattern() override { bStopped = true; }
	bool IsFinished() const override { return bStopped || ElapsedTime >= Duration; }

	// 시작한 지 ElapsedTime초 뒤의 카메라 로컬 흔들림. 시간만으로 정해지므로
	// 에디터 시퀀서가 재생 없이(스크럽만으로) 같은 흔들림을 미리 볼 때도 쓴다. 범위 밖이면 0
	static FVector EvaluateOffset(float Strength, float Duration, float Frequency, float ElapsedTime);

private:
	float Strength = 0.1f;
	float Duration = 0.5f;
	float Frequency = 20.0f;
	float ElapsedTime = 0.0f;
	bool bStopped = true;
};
