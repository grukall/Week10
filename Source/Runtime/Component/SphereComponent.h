#pragma once
#include "ShapeComponent.h"

class USphereComponent : public UShapeComponent
{
	DECLARE_CLASS(USphereComponent, UShapeComponent)
	REFLECT_START(USphereComponent)
		PROPERTY(SphereRadius)
	REFLECT_END()

public:
	virtual FBox CalcLocalBounds() const override
	{
		return FBox{ FVector(-SphereRadius), FVector(SphereRadius) };
	}
	virtual void DrawDebug(FLineBatcher* LineBatcher) const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 비균등 스케일이어도 구로 유지한다 (가장 작은 축 기준).
	float GetScaledSphereRadius() const;

private:
	float SphereRadius = 0.5f;
};
