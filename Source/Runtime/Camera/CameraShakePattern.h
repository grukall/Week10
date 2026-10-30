#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Math/Vector.h"

// Delta in the shake's play space. This first pattern changes position.
struct FCameraShakePatternUpdateResult
{
	FVector Location = FVector::ZeroVector;
};

class UCameraShakePattern : public UObject
{
	DECLARE_CLASS(UCameraShakePattern, UObject)

public:
	virtual void StartShakePattern() = 0;
	virtual void UpdateShakePattern(float DeltaTime, FCameraShakePatternUpdateResult& OutResult) = 0;
	virtual void StopShakePattern() = 0;
	virtual bool IsFinished() const = 0;
};
