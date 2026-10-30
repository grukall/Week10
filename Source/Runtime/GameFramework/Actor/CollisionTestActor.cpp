#include "EnginePCH.h"
#include "CollisionTestActor.h"

#include "Component/BoxComponent.h"
#include "Component/ProjectileMovementComponent.h"

ACollisionTestActor::ACollisionTestActor()
{
	CollisionBox = CreateDefaultSubobject<UBoxComponent>("CollisionBox");
	SetRootComponent(CollisionBox);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovement");
}
