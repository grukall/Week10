#pragma once

#include "GameFramework/Actor.h"

class UBoxComponent;
class UProjectileMovementComponent;

// Sweep/Hit 테스트용 투사체. MovementComponent는 루트만 Sweep하므로 Box를 루트로 둔다 (UE의 투사체 액터와 같은 구성).
// 속도·중력·bBlockComponent 같은 값은 씬 파일이나 Details에서 정한다.
class ACollisionTestActor : public AActor
{
	DECLARE_CLASS(ACollisionTestActor, AActor)
public:
	ACollisionTestActor();

private:
	UBoxComponent* CollisionBox = nullptr;
	UProjectileMovementComponent* ProjectileMovement = nullptr;
};
