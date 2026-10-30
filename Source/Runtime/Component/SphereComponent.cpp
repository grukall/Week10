#include "EnginePCH.h"
#include "SphereComponent.h"

#include "Render/LineBatcher.h"

void USphereComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher) return;

	FVector X, Y, Z;
	GetWorldAxes(X, Y, Z);

	const FVector Center = GetWorldLocation();
	const float Radius = GetScaledSphereRadius();
	const FVector4 Color = GetLineColor();

	AddWireCircle(LineBatcher, Center, X, Y, Radius, Color);
	AddWireCircle(LineBatcher, Center, X, Z, Radius, Color);
	AddWireCircle(LineBatcher, Center, Y, Z, Radius, Color);
}

void USphereComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "SphereRadius")
		SphereRadius = std::max(0.0f, SphereRadius);
}

float USphereComponent::GetScaledSphereRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs();
	return SphereRadius * std::min({ S.X, S.Y, S.Z });
}
