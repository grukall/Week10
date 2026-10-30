#include "EnginePCH.h"
#include "CapsuleComponent.h"

#include "Render/LineBatcher.h"

void UCapsuleComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher) return;

	FVector X, Y, Z;
	GetWorldAxes(X, Y, Z);

	const FVector Center = GetWorldLocation();
	const float Radius = GetScaledCapsuleRadius();
	const FVector Top = Center + Z * GetScaledCapsuleHalfHeightWithoutHemisphere();
	const FVector Bottom = Center - Z * GetScaledCapsuleHalfHeightWithoutHemisphere();
	const FVector4 Color = GetLineColor();

	// 원통 부분: 위·아래 원과 옆선 4개
	AddWireCircle(LineBatcher, Top, X, Y, Radius, Color);
	AddWireCircle(LineBatcher, Bottom, X, Y, Radius, Color);
	LineBatcher->AddLine(Top + X * Radius, Bottom + X * Radius, Color);
	LineBatcher->AddLine(Top - X * Radius, Bottom - X * Radius, Color);
	LineBatcher->AddLine(Top + Y * Radius, Bottom + Y * Radius, Color);
	LineBatcher->AddLine(Top - Y * Radius, Bottom - Y * Radius, Color);

	// 반구: XZ·YZ 평면의 반원. 0~PI가 +Z쪽, PI~2PI가 -Z쪽
	AddWireArc(LineBatcher, Top, X, Z, Radius, 0.0f, PI, 16, Color);
	AddWireArc(LineBatcher, Top, Y, Z, Radius, 0.0f, PI, 16, Color);
	AddWireArc(LineBatcher, Bottom, X, Z, Radius, PI, 2.0f * PI, 16, Color);
	AddWireArc(LineBatcher, Bottom, Y, Z, Radius, PI, 2.0f * PI, 16, Color);
}

void UCapsuleComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "CapsuleRadius" || Property.Name == "CapsuleHalfHeight")
	{
		CapsuleRadius = std::max(0.0f, CapsuleRadius);
		// 높이가 반지름보다 작으면 선분 길이가 음수가 된다.
		CapsuleHalfHeight = std::max(CapsuleRadius, CapsuleHalfHeight);
	}
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs();
	return CapsuleRadius * std::min(S.X, S.Y);
}

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	const FVector S = GetWorldScale3D().GetAbs();
	// 스케일 뒤에도 반구가 몸통보다 커지지 않게 반지름 이상으로 맞춘다.
	return std::max(CapsuleHalfHeight * S.Z, GetScaledCapsuleRadius());
}
