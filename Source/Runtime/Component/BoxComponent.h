#pragma once
#include "ShapeComponent.h"

class UBoxComponent : public UShapeComponent
{
	DECLARE_CLASS(UBoxComponent, UShapeComponent)
	REFLECT_START(UBoxComponent)
		PROPERTY(BoxExtent)
		FUNCTION(SetBoxExtent)
	REFLECT_END()

public:
	virtual FBox CalcLocalBounds() const override { return FBox{ -BoxExtent, BoxExtent }; }
	virtual void DrawDebug(FLineBatcher* LineBatcher) const override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 판정 코드는 이 값을 쓴다 (월드 스케일 반영).
	FVector GetScaledBoxExtent() const { return BoxExtent * GetWorldScale3D().GetAbs(); }

	// 크기가 바뀌면 움직이지 않아도 겹침이 달라지므로 다음 Tick 갱신을 예약한다.
	void SetBoxExtent(const FVector& InExtent);

private:
	// 반 크기. 실제 박스는 각 축으로 -Extent ~ +Extent
	FVector BoxExtent = FVector(0.5f, 0.5f, 0.5f);
};
