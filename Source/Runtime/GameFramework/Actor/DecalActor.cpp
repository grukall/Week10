#include "EnginePCH.h"
#include "DecalActor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"
#include "Core/ResourcePaths.h"

ADecalActor::ADecalActor()
{
	// 언리얼처럼 데칼 컴포넌트가 루트다. 액터를 움직이면 투영 박스가 그대로 따라간다.
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("DecalComponent");
	SetRootComponent(DecalComponent);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetupAttachment(DecalComponent);
	BillboardComponent->SetHiddenInDetails(false);
	BillboardComponent->SetTexture(ResourcePaths::DecalActorIcon);
}