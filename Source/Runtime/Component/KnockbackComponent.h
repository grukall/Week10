#pragma once

#include "ActorComponent.h"
#include "Math/Vector.h"

// 맞은 액터를 Direction으로 KnockbackTime 동안 KnockbackDistance만큼 밀어낸다.
// UE의 LaunchCharacter(→ CharacterMovementComponent)처럼 "밀어내기"는 엔진 컴포넌트가 하고,
// 언제·어느 방향으로 밀지는 게임 코드(Lua)가 정한다.
// 게임 dt로 움직이므로 히트스톱 중에는 같이 멈춘다.
class UKnockbackComponent : public UActorComponent
{
	DECLARE_CLASS(UKnockbackComponent, UActorComponent)
	REFLECT_START(UKnockbackComponent)
		PROPERTY(KnockbackDistance)
		PROPERTY(KnockbackTime)
		FUNCTION(Launch)
		FUNCTION(StopKnockback)
		FUNCTION(IsKnockedBack)
		FUNCTION(GetElapsedTime)
	REFLECT_END()

public:
	UKnockbackComponent();

	void TickComponent(float DeltaTime) override;

	// Direction(정규화하지 않아도 된다) 쪽으로 밀기 시작한다. 밀리는 중에 다시 부르면 처음부터 다시 민다
	void Launch(const FVector& Direction);
	void StopKnockback();
	bool IsKnockedBack() const { return RemainingTime > 0.0f; }
	// 이번 밀림이 시작된 뒤 지난 시간. 밀리는 중이 아니면 0
	float GetElapsedTime() const { return IsKnockedBack() ? KnockbackTime - RemainingTime : 0.0f; }

	// 밀려나는 총 거리
	float KnockbackDistance = 2.0f;
	// 밀려나는 데 걸리는 시간 (초)
	float KnockbackTime = 0.3f;

private:
	FVector KnockbackDirection = FVector::ZeroVector;
	float RemainingTime = 0.0f;
};
