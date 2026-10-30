#pragma once
#include "PrimitiveComponent.h"

class FLineBatcher;

// 충돌용 단순 도형(Box/Sphere/Capsule)의 공통 부모. 메시 없이 디버그 라인으로만 보인다.
class UShapeComponent : public UPrimitiveComponent
{
	DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)
	REFLECT_START(UShapeComponent)
		PROPERTY(ShapeColor)
		PROPERTY(bDrawOnlyIfSelected)
	REFLECT_END()

public:
	// 소유 월드의 Shape 목록에 등록한다. 넓은 단계가 이 목록만 훑는다.
	virtual void BeginPlay() override;

	virtual void DrawDebug(FLineBatcher* LineBatcher) const = 0;

	// 선택 여부는 에디터만 알기 때문에 그릴지 말지는 에디터가 정한다.
	bool ShouldDrawOnlyIfSelected() const { return bDrawOnlyIfSelected; }

	// 월드 행렬의 각 축(정규화). 디버그 라인과 판정 함수가 같이 쓴다.
	void GetWorldAxes(FVector& OutX, FVector& OutY, FVector& OutZ) const;

protected:
	FVector4 GetLineColor() const;

	// Center에서 AxisA·AxisB 평면 위에 StartAngle~EndAngle(라디안) 호를 그린다.
	static void AddWireArc(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, float StartAngle, float EndAngle, int32 Segments, const FVector4& Color);
	static void AddWireCircle(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, const FVector4& Color);

	FColor ShapeColor = FColor(255, 0, 0);
	bool bDrawOnlyIfSelected = false;

private:
	// 마지막 Refit 뒤에 움직였으면 BVH 경계가 낡았다. 월드는 이 Shape를 BVH 대신 MovedShapes에서 검사한다.
	friend class UWorld;
	bool bMovedSinceShapeRefit = false;
};
