#pragma once

#include "SceneComponent.h"

class UShapeComponent;

// UE USpringArmComponent 축소판. 부모(피벗)에서 뒤로 TargetArmLength만큼 팔을 뻗고 끝에 자식(카메라)을 둔다.
// 팔과 카메라 사이에 벽이 있으면 팔을 줄이고, Lag를 켜면 피벗을 늦게 따라간다.
// Hitori에는 소켓이 없으므로 팔 끝(소켓)을 계산한 뒤 붙어 있는 자식 전부를 그 위치·회전으로 옮긴다.
class USpringArmComponent : public USceneComponent
{
	DECLARE_CLASS(USpringArmComponent, USceneComponent)
	REFLECT_START(USpringArmComponent)
		PROPERTY(TargetArmLength)
		PROPERTY(SocketOffset)
		PROPERTY(TargetOffset)
		PROPERTY(ProbeSize)
		PROPERTY(bDoCollisionTest)
		PROPERTY(bInheritPitch)
		PROPERTY(bInheritYaw)
		PROPERTY(bInheritRoll)
		PROPERTY(bEnableCameraLag)
		PROPERTY(bEnableCameraRotationLag)
		PROPERTY(CameraLagSpeed)
		PROPERTY(CameraRotationLagSpeed)
		PROPERTY(CameraLagMaxDistance)
	REFLECT_END()

public:
	USpringArmComponent();

	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	// 팔 끝(카메라가 놓일 곳)의 월드 위치·회전. 이번 Tick 결과다
	FVector GetSocketLocation() const { return SocketLocation; }
	FRotator GetSocketRotation() const { return SocketRotation; }
	// 이번 Tick에 벽 때문에 팔이 줄었는지
	bool IsCollisionFixApplied() const { return bIsCameraFixed; }

	// 피벗에서 팔 끝까지의 길이 (충돌 전)
	float TargetArmLength = 3.0f;
	// 팔 끝 기준 오프셋 (팔 회전을 따른다). 어깨 너머 시점 등
	FVector SocketOffset = FVector::ZeroVector;
	// 피벗 기준 월드 오프셋 (팔 회전을 따르지 않는다). 머리 위를 피벗으로 삼을 때 등
	FVector TargetOffset = FVector::ZeroVector;

	// 벽 검사에 쓰는 구의 반지름. 카메라 Near 평면이 벽을 뚫지 않을 만큼 둔다
	float ProbeSize = 0.12f;
	bool bDoCollisionTest = true;

	// 끄면 그 축은 부모 회전을 따르지 않고 이 컴포넌트의 Relative 회전값을 월드값으로 쓴다
	bool bInheritPitch = true;
	bool bInheritYaw = true;
	bool bInheritRoll = true;

	// 피벗 위치·팔 회전을 목표로 바로 옮기지 않고 매 프레임 Speed 비율만큼 다가간다
	bool bEnableCameraLag = false;
	bool bEnableCameraRotationLag = false;
	float CameraLagSpeed = 10.0f;
	float CameraRotationLagSpeed = 10.0f;
	// 0보다 크면 Lag로 뒤처지는 거리를 이 값 이하로 묶는다 (빠르게 움직여도 화면 밖으로 놓치지 않게)
	float CameraLagMaxDistance = 0.0f;

private:
	// UE UpdateDesiredArmLocation: 회전·Lag·충돌을 거쳐 팔 끝을 정하고 자식을 옮긴다
	void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime);
	FRotator GetTargetRotation() const;
	// Start에서 End까지 ProbeSize 구를 밀어 처음 막히는 비율(0~1)을 돌려준다. 안 막히면 1
	float SweepProbe(const FVector& Start, const FVector& End) const;
	bool ProbeOverlaps(const FVector& Center, const TArray<UShapeComponent*>& Candidates) const;

	FVector SocketLocation = FVector::ZeroVector;
	FRotator SocketRotation;
	bool bIsCameraFixed = false;

	// Lag의 출발점. 첫 Tick(또는 BeginPlay)에는 목표값으로 맞춰 처음부터 미끄러지지 않게 한다
	FVector PreviousDesiredLoc = FVector::ZeroVector;
	FRotator PreviousDesiredRot;
	bool bHasPreviousDesired = false;
};
