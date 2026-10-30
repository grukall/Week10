#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Render/LineBatcher.h"
#include "Math/Box.h"

namespace
{
	// Min/Max로 코너 8개를 만든다. BOX_EDGES가 기대하는 순서다.
	void MakeCorners(const FVector& Min, const FVector& Max, FVector* OutCorners)
	{
		OutCorners[0] = { Min.X, Min.Y, Min.Z };
		OutCorners[1] = { Max.X, Min.Y, Min.Z };
		OutCorners[2] = { Max.X, Max.Y, Min.Z };
		OutCorners[3] = { Min.X, Max.Y, Min.Z };
		OutCorners[4] = { Min.X, Min.Y, Max.Z };
		OutCorners[5] = { Max.X, Min.Y, Max.Z };
		OutCorners[6] = { Max.X, Max.Y, Max.Z };
		OutCorners[7] = { Min.X, Max.Y, Max.Z };
	}
}

UDecalComponent::UDecalComponent() : DecalSize(16.f, 32.f, 32.f)
{

}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	LineBatcher->AddOrientedBox(
		FBox({ -DecalSize.X, -DecalSize.Y, -DecalSize.Z }, { DecalSize.X, DecalSize.Y, DecalSize.Z })
		, GetWorldMatrix()
		, { 0, 255, 0, 1 });
}
