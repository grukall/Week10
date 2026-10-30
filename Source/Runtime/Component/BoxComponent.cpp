#include "EnginePCH.h"
#include "BoxComponent.h"

#include "Render/LineBatcher.h"

void UBoxComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher) return;

	// 로컬 박스와 월드 행렬을 넘기면 회전·스케일·부모 변환이 모두 반영된 OBB로 그려진다.
	LineBatcher->AddOrientedBox(CalcLocalBounds(), GetWorldMatrix(), GetLineColor());
}

void UBoxComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "BoxExtent")
		SetBoxExtent(BoxExtent);
}

void UBoxComponent::SetBoxExtent(const FVector& InExtent)
{
	// 음수 크기는 Min/Max가 뒤집혀 판정이 깨진다.
	BoxExtent = InExtent.GetAbs();
	MarkOverlapsDirty();
}
