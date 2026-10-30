#include "EnginePCH.h"
#include "Component/KnockbackComponent.h"

#include "GameFramework/Actor.h"

UKnockbackComponent::UKnockbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKnockbackComponent::Launch(const FVector& Direction)
{
	if (KnockbackTime <= 0.0f || Direction.SizeSquared() <= KINDA_SMALL_NUMBER)
		return;
	KnockbackDirection = Direction.Normalized();
	RemainingTime = KnockbackTime;
}

void UKnockbackComponent::StopKnockback()
{
	RemainingTime = 0.0f;
}

void UKnockbackComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	AActor* Owner = GetOwner();
	if (!IsKnockedBack() || !Owner || DeltaTime <= 0.0f)
		return;

	// 속도를 시간에 따라 1 → 0으로 줄인다. 그 넓이가 KnockbackDistance가 되도록 처음 속도는 2D/T
	const float Step = FMath::Min(DeltaTime, RemainingTime);
	const float Speed = 2.0f * KnockbackDistance / KnockbackTime * (RemainingTime / KnockbackTime);
	const FVector Move = KnockbackDirection * (Speed * Step);

	// 벽에 닿으면 멈추도록 Sweep한다. 축을 나눠 옮겨 막힌 축만 멈추고 나머지 축으로는 벽을 따라 미끄러진다
	if (Move.X != 0.0f) Owner->AddActorWorldOffset(FVector(Move.X, 0.0f, 0.0f), true);
	if (Move.Y != 0.0f) Owner->AddActorWorldOffset(FVector(0.0f, Move.Y, 0.0f), true);
	if (Move.Z != 0.0f) Owner->AddActorWorldOffset(FVector(0.0f, 0.0f, Move.Z), true);

	RemainingTime = FMath::Max(RemainingTime - DeltaTime, 0.0f);
}
