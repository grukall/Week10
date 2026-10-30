#pragma once
#include "ShapeComponent.h"

// 로컬 Z축으로 선 캡슐. HalfHeight는 반구 끝까지 포함한 높이의 절반이라 항상 Radius 이상이다.
class UCapsuleComponent : public UShapeComponent
{
	DECLARE_CLASS(UCapsuleComponent, UShapeComponent)
	REFLECT_START(UCapsuleComponent)
		PROPERTY(CapsuleHalfHeight)
		PROPERTY(CapsuleRadius)
	REFLECT_END()

public:
	virtual FBox CalcLocalBounds() const override
	{
		return FBox{ FVector(-CapsuleRadius, -CapsuleRadius, -CapsuleHalfHeight),
		             FVector( CapsuleRadius,  CapsuleRadius,  CapsuleHalfHeight) };
	}
	virtual void DrawDebug(FLineBatcher* LineBatcher) const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	float GetScaledCapsuleRadius() const;
	float GetScaledCapsuleHalfHeight() const;
	// 양 끝 반구 중심 사이 선분의 절반. 판정은 이 선분 + 반지름으로 한다.
	float GetScaledCapsuleHalfHeightWithoutHemisphere() const
	{
		return GetScaledCapsuleHalfHeight() - GetScaledCapsuleRadius();
	}

private:
	float CapsuleHalfHeight = 1.0f;
	float CapsuleRadius = 0.5f;
};
