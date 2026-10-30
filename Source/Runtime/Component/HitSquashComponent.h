#pragma once

#include "ActorComponent.h"
#include "Math/Vector.h"
#include "UObject/WeakObjectPtr.h"

class USceneComponent;

// 맞은 순간 메시를 납작하고 넓게 찌그러뜨렸다가 출렁이며 원래 크기로 되돌린다 (Squash & Stretch).
// 충돌 Shape는 그대로 두고 보이는 메시(첫 StaticMeshComponent)만 바꾼다.
// 게임 dt로 움직이므로 히트스톱 중에는 찌그러진 채로 멈춰 있다가 풀리면서 튕겨 돌아온다.
class UHitSquashComponent : public UActorComponent
{
	DECLARE_CLASS(UHitSquashComponent, UActorComponent)
	REFLECT_START(UHitSquashComponent)
		PROPERTY(SquashAmount)
		PROPERTY(SquashTime)
		FUNCTION(Squash)
		FUNCTION(IsSquashing)
	REFLECT_END()

public:
	UHitSquashComponent();

	void TickComponent(float DeltaTime) override;
	void EndPlay() override;

	// 찌그러뜨리기 시작한다. 찌그러진 중에 다시 부르면 원래 크기를 기준으로 처음부터 다시 한다
	void Squash();
	bool IsSquashing() const { return bSquashing; }

	// 처음 찌그러지는 비율. 0.3이면 세로 0.7배, 가로 1.3배
	float SquashAmount = 0.3f;
	// 원래 크기로 돌아오는 데 걸리는 시간 (초)
	float SquashTime = 0.25f;

private:
	void ApplyScale();
	void Restore();

	TWeakObjectPtr<USceneComponent> Target;
	FVector BaseScale = FVector(1.0f, 1.0f, 1.0f);
	float ElapsedTime = 0.0f;
	bool bSquashing = false;
};
