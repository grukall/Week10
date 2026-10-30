#pragma once

#include "Camera/CameraModifier.h"
#include "Camera/CameraShakeBase.h"

class UCameraModifier_CameraShake : public UCameraModifier
{
	DECLARE_CLASS(UCameraModifier_CameraShake, UCameraModifier)

public:
	UCameraModifier_CameraShake() { Priority = 255; }
	~UCameraModifier_CameraShake() override;
	bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	UCameraShakeBase* AddCameraShake(UClass* ShakeClass, float Scale = 1.0f,
		ECameraShakePlaySpace Space = ECameraShakePlaySpace::CameraLocal);
	// Takes ownership of a configured instance.
	UCameraShakeBase* AddCameraShake(UCameraShakeBase* Shake, float Scale = 1.0f,
		ECameraShakePlaySpace Space = ECameraShakePlaySpace::CameraLocal);
	void RemoveCameraShake(UCameraShakeBase* Shake);
	void RemoveAllCameraShakes();
	int32 GetActiveShakeCount() const { return ActiveShakes.Num(); }

private:
	// Explicitly deleted on finish/removal; weak handles tolerate global teardown order.
	TArray<TWeakObjectPtr<UCameraShakeBase>> ActiveShakes;
};
