#include "EnginePCH.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/CameraShakePattern.h"
#include "CameraTypes.h"

UCameraShakeBase::~UCameraShakeBase()
{
	delete RootShakePattern.Get();
}

void UCameraShakeBase::SetRootShakePattern(UCameraShakePattern* Pattern)
{
	if (RootShakePattern.Get() == Pattern)
		return;
	StopShake();
	delete RootShakePattern.Get();
	RootShakePattern = Pattern;
}

UCameraShakePattern* UCameraShakeBase::GetRootShakePattern() const
{
	return RootShakePattern.Get();
}

void UCameraShakeBase::StartShake(float Scale, ECameraShakePlaySpace Space)
{
	ShakeScale = FMath::Max(Scale, 0.0f);
	PlaySpace = Space;
	bActive = true;
	if (UCameraShakePattern* Pattern = RootShakePattern.Get())
		Pattern->StartShakePattern();
}

void UCameraShakeBase::UpdateAndApplyCameraShake(float DeltaTime, float Alpha, FMinimalViewInfo& InOutPOV)
{
	if (IsFinished())
		return;
	FCameraShakePatternUpdateResult Result;
	RootShakePattern->UpdateShakePattern(DeltaTime, Result);
	FVector Offset = Result.Location * (ShakeScale * Alpha);
	if (PlaySpace == ECameraShakePlaySpace::CameraLocal)
		Offset = InOutPOV.GetCameraRotationMatrix().TransformVector(Offset);
	InOutPOV.Location += Offset;
}

void UCameraShakeBase::StopShake()
{
	bActive = false;
	if (UCameraShakePattern* Pattern = RootShakePattern.Get())
		Pattern->StopShakePattern();
}

bool UCameraShakeBase::IsFinished() const
{
	const UCameraShakePattern* Pattern = RootShakePattern.Get();
	return !bActive || !Pattern || Pattern->IsFinished();
}
