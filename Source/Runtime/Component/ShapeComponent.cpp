#include "EnginePCH.h"
#include "ShapeComponent.h"

#include "Render/LineBatcher.h"
#include "Engine/World.h"

void UShapeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->GetWorld())
		GetOwner()->GetWorld()->AddShape(this);
}

void UShapeComponent::GetWorldAxes(FVector& OutX, FVector& OutY, FVector& OutZ) const
{
	// 행벡터 규약이라 행렬의 0·1·2행이 각각 스케일이 곱해진 X·Y·Z축이다.
	const FMatrix M = GetWorldMatrix();
	OutX = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).GetSafeNormal();
	OutY = FVector(M.M[1][0], M.M[1][1], M.M[1][2]).GetSafeNormal();
	OutZ = FVector(M.M[2][0], M.M[2][1], M.M[2][2]).GetSafeNormal();
}

FVector4 UShapeComponent::GetLineColor() const
{
	// 디버그 라인은 오버레이라 sRGB 값을 감마 변환 없이 그대로 쓴다
	const FLinearColor C = ShapeColor.ReinterpretAsLinear();
	return FVector4(C.R, C.G, C.B, 1.0f);
}

void UShapeComponent::AddWireArc(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, float StartAngle, float EndAngle, int32 Segments, const FVector4& Color)
{
	const float Step = (EndAngle - StartAngle) / Segments;
	FVector Prev = Center + (AxisA * std::cos(StartAngle) + AxisB * std::sin(StartAngle)) * Radius;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = StartAngle + Step * i;
		const FVector Point = Center + (AxisA * std::cos(Angle) + AxisB * std::sin(Angle)) * Radius;
		LineBatcher->AddLine(Prev, Point, Color);
		Prev = Point;
	}
}

void UShapeComponent::AddWireCircle(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, const FVector4& Color)
{
	AddWireArc(LineBatcher, Center, AxisA, AxisB, Radius, 0.0f, 2.0f * PI, 32, Color);
}
