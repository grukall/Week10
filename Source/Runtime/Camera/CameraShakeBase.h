#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"

class UCameraShakePattern;
struct FMinimalViewInfo;

enum class ECameraShakePlaySpace : uint8 { CameraLocal, World };

class UCameraShakeBase : public UObject
{
	DECLARE_CLASS(UCameraShakeBase, UObject)

public:
	~UCameraShakeBase() override;
	void SetRootShakePattern(UCameraShakePattern* Pattern);
	UCameraShakePattern* GetRootShakePattern() const;
	void StartShake(float Scale = 1.0f, ECameraShakePlaySpace Space = ECameraShakePlaySpace::CameraLocal);
	void UpdateAndApplyCameraShake(float DeltaTime, float Alpha, FMinimalViewInfo& InOutPOV);
	void StopShake();
	bool IsFinished() const;

private:
	// Explicit ownership, with a weak handle for global UObject teardown.
	TWeakObjectPtr<UCameraShakePattern> RootShakePattern;
	float ShakeScale = 1.0f;
	ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal;
	bool bActive = false;
};
