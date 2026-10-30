#pragma once

#include "../GameFramework/Actor.h"
#include "UObject/Class.h"

#include "CameraComponent.h"

//class UCameraComponent;

class ACameraActor : public AActor
{
	DECLARE_CLASS(ACameraActor, AActor)
	REFLECT_START(ClassName)
		FUNCTION(GetCameraComponent)
		FUNCTION(StartCameraShake)
	REFLECT_END()

public:
	// 카메라 컴포넌트를 기본 서브오브젝트로 생성해 Actor 루트에 연결한다.
	ACameraActor();

	UCameraComponent* GetCameraComponent() const { return CameraComponent; };

	// 지금 이 카메라로 화면을 보고 있는 플레이어들의 카메라를 흔든다 (Lua CameraShake와 같은 인자).
	// 레벨 시퀀스 Event 트랙이 리플렉션으로 부를 수 있게 카메라 쪽에 둔다 (UE의 Camera Shake 트랙과 같은 역할).
	// Strength는 카메라가 밀리는 거리, Frequency는 초당 흔들림 횟수
	void StartCameraShake(float Strength, float Duration, float Frequency);

private:
	UCameraComponent* CameraComponent;
};
