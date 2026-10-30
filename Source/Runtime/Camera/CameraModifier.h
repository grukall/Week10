#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"

class APlayerCameraManager;
struct FMinimalViewInfo;

class UCameraModifier : public UObject
{
	DECLARE_CLASS(UCameraModifier, UObject)

public:
	virtual ~UCameraModifier() = default;
	void InitializeFor(APlayerCameraManager* Owner);
	APlayerCameraManager* GetCameraOwner() const;
	// True stops subsequent modifiers for this frame.
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) { return false; }
	void EnableModifier() { bDisabled = false; }
	void DisableModifier() { bDisabled = true; }
	bool IsDisabled() const { return bDisabled; }
	uint8 Priority = 127; // Lower values run first. Set before registration.

private:
	TWeakObjectPtr<APlayerCameraManager> CameraOwner;
	bool bDisabled = false;
};
