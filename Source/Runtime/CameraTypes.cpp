#include "EnginePCH.h"
#include "CameraTypes.h"
#include <cmath>

FMatrix FMinimalViewInfo::GetCameraRotationMatrix() const
{
	FVector Forward = Rotation.Quaternion().GetForwardVector().GetSafeNormal();
	FVector Right = FVector(-Forward.Y, Forward.X, 0.0f).Normalized();
	if (Right.SizeSquared() == 0.0f)
		Right = RightVector;
	const float Z = FMath::Clamp(Forward.Z, -1.0f, 1.0f);
	const float Horizontal = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
	Forward = FVector(Right.Y * Horizontal, -Right.X * Horizontal, Z);
	FMatrix Result = FMatrix::Identity;
	Result.SetAxis(0, Forward);
	Result.SetAxis(1, Right);
	Result.SetAxis(2, Forward.Cross(Right).Normalized());
	return Result;
}
