#include "EnginePCH.h"
#include "DecalActor.h"
#include "Component/DecalComponent.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("DecalComponent");
	DecalComponent->SetupAttachment(RootComponent);
}