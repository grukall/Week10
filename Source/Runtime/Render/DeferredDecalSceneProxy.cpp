#include "EnginePCH.h"
#include "DeferredDecalSceneProxy.h"
#include "Component/DecalComponent.h"
#include "Asset/AssetManager.h"
#include "Material.h"

namespace
{
	FVector Down(const FVector4 v4)
	{
		return FVector(v4.X, v4.Y, v4.Z);
	}
}

void FDeferredDecalSceneProxy::UpdateFromComponent()
{
	FMatrix DecalSizeMat = FMatrix::Identity;
	DecalSizeMat = DecalSizeMat.ApplyScale(Component->DecalSize);
	DecalToWorld = DecalSizeMat * Component->GetWorldMatrix();
	WorldToDecal = DecalToWorld.Inverse();

	Material = Component->GetDecalMaterial();
	if (!Material)
	{
		Material = UAssetManager::Get().GetAssetByPath<UMaterial>("DecalMaterial");
	}

	DecalColor = Component->DecalColor;

	//TODO : bVisible?
	bVisible = true;

	// 이동이 4행에 있는 행벡터 규칙이라 0~2행이 단위 박스의 월드 축이다. 행 길이가 반크기, 방향이 축이다.
	const FVector Row[3] = {
		Down(DecalToWorld.GetRow(0)),
		Down(DecalToWorld.GetRow(1)),
		Down(DecalToWorld.GetRow(2)),
	};

	//OBB
	OBB.Center = Down(DecalToWorld.GetOrigin());
	OBB.Extent = FVector(Row[0].Size(), Row[1].Size(), Row[2].Size());
	OBB.Axis[0] = Row[0].GetSafeNormal();
	OBB.Axis[1] = Row[1].GetSafeNormal();
	OBB.Axis[2] = Row[2].GetSafeNormal();

	//AABB: 회전된 박스를 감싸려면 월드 축마다 세 축 벡터 성분의 절댓값을 더한다
	Bounds.Center = OBB.Center;
	Bounds.Extent = FVector(
		std::abs(Row[0].X) + std::abs(Row[1].X) + std::abs(Row[2].X),
		std::abs(Row[0].Y) + std::abs(Row[1].Y) + std::abs(Row[2].Y),
		std::abs(Row[0].Z) + std::abs(Row[1].Z) + std::abs(Row[2].Z));
}
