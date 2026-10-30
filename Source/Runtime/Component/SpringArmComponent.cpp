#include "EnginePCH.h"
#include "Component/SpringArmComponent.h"

#include "Component/BoxComponent.h"
#include "Component/CapsuleComponent.h"
#include "Component/SphereComponent.h"
#include "Collision/ShapeCollision.h"
#include "Math/Interp.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

USpringArmComponent::USpringArmComponent()
{
	// 대상이 움직인 뒤(PrePhysics: 액터·Lua Tick)에 팔을 계산해야 카메라가 한 프레임 늦지 않는다
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::PostPhysics;
}

void USpringArmComponent::BeginPlay()
{
	Super::BeginPlay();
	PreviousDesiredLoc = GetWorldLocation() + TargetOffset;
	PreviousDesiredRot = GetTargetRotation();
	bHasPreviousDesired = true;
	// 첫 프레임부터 카메라가 팔 끝에 있도록 Lag 없이 한 번 맞춘다
	UpdateDesiredArmLocation(bDoCollisionTest, false, false, 0.0f);
}

void USpringArmComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!bHasPreviousDesired)
	{
		PreviousDesiredLoc = GetWorldLocation() + TargetOffset;
		PreviousDesiredRot = GetTargetRotation();
		bHasPreviousDesired = true;
	}
	UpdateDesiredArmLocation(bDoCollisionTest, bEnableCameraLag, bEnableCameraRotationLag, DeltaTime);
}

FRotator USpringArmComponent::GetTargetRotation() const
{
	FRotator DesiredRot = GetWorldRotation();
	const FRotator& LocalRot = GetRelativeRotation();
	if (!bInheritPitch) DesiredRot.Pitch = LocalRot.Pitch;
	if (!bInheritYaw)   DesiredRot.Yaw = LocalRot.Yaw;
	if (!bInheritRoll)  DesiredRot.Roll = LocalRot.Roll;
	return DesiredRot;
}

void USpringArmComponent::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	// 1. 팔 회전
	FRotator DesiredRot = GetTargetRotation();
	if (bDoRotationLag)
		DesiredRot = FMath::RInterpTo(PreviousDesiredRot, DesiredRot, DeltaTime, CameraRotationLagSpeed);
	PreviousDesiredRot = DesiredRot;

	// 2. 피벗. Lag는 팔 끝이 아니라 피벗에 걸어 팔 길이·방향은 그대로 두고 따라오는 속도만 늦춘다
	const FVector ArmOrigin = GetWorldLocation() + TargetOffset;
	FVector DesiredLoc = ArmOrigin;
	if (bDoLocationLag)
	{
		DesiredLoc = FMath::VInterpTo(PreviousDesiredLoc, ArmOrigin, DeltaTime, CameraLagSpeed);
		const FVector FromOrigin = DesiredLoc - ArmOrigin;
		if (CameraLagMaxDistance > 0.0f && FromOrigin.SizeSquared() > CameraLagMaxDistance * CameraLagMaxDistance)
			DesiredLoc = ArmOrigin + FromOrigin.GetClampedToMaxSize(CameraLagMaxDistance);
	}
	PreviousDesiredLoc = DesiredLoc;

	// 3. 팔을 뒤로 뻗고 소켓 오프셋을 팔 회전 기준으로 더한다
	const FQuat ArmQuat = DesiredRot.Quaternion();
	DesiredLoc -= ArmQuat.RotateVector(ForwardVector) * TargetArmLength;
	DesiredLoc += ArmQuat.RotateVector(SocketOffset);

	// 4. 벽 검사. Lag가 걸린 위치가 아닌 실제 피벗에서 쏴야 피벗이 벽 너머로 넘어간 상태로 검사하지 않는다
	FVector ResultLoc = DesiredLoc;
	bIsCameraFixed = false;
	if (bDoTrace && TargetArmLength != 0.0f)
	{
		const float HitTime = SweepProbe(ArmOrigin, DesiredLoc);
		if (HitTime < 1.0f)
		{
			ResultLoc = ArmOrigin + (DesiredLoc - ArmOrigin) * HitTime;
			bIsCameraFixed = true;
		}
	}

	// 5. 소켓 갱신 후 자식을 팔 끝으로 옮긴다 (소켓 시스템 대신)
	SocketLocation = ResultLoc;
	SocketRotation = DesiredRot;
	for (USceneComponent* Child : GetAttachChildren())
	{
		if (Child)
			Child->SetWorldLocationAndRotation(ResultLoc, DesiredRot, false);
	}
}

bool USpringArmComponent::ProbeOverlaps(const FVector& Center, const TArray<UShapeComponent*>& Candidates) const
{
	const FCollisionSphere Probe{ Center, ProbeSize };
	for (UShapeComponent* Other : Candidates)
	{
		if (UBoxComponent* Box = Cast<UBoxComponent>(Other))
		{
			if (ShapeCollision::BoxSphere(ShapeCollision::MakeBox(*Box), Probe, nullptr))
				return true;
		}
		else if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Other))
		{
			if (ShapeCollision::CapsuleSphere(ShapeCollision::MakeCapsule(*Capsule), Probe, nullptr))
				return true;
		}
		else if (USphereComponent* Sphere = Cast<USphereComponent>(Other))
		{
			if (ShapeCollision::SphereSphere(ShapeCollision::MakeSphere(*Sphere), Probe, nullptr))
				return true;
		}
	}
	return false;
}

float USpringArmComponent::SweepProbe(const FVector& Start, const FVector& End) const
{
	// PrimitiveComponent::SweepBlocking과 같은 순서: 넓은 단계 → 칸 단위 전진 → 이분 탐색
	constexpr int32 MaxSweepSteps = 64;
	constexpr int32 BinarySearchIterations = 12;

	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	const FVector Delta = End - Start;
	const float Distance = Delta.Size();
	if (!World || ProbeSize <= 0.0f || Distance <= KINDA_SMALL_NUMBER)
		return 1.0f;

	// 1. 넓은 단계: 구가 지나가는 범위의 AABB에 걸치는 Shape 중 막는 것만. 자기 액터(캐릭터 몸)는 뺀다
	const FVector Radius(ProbeSize, ProbeSize, ProbeSize);
	const FBox SweptBounds{
		FVector(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y), FMath::Min(Start.Z, End.Z)) - Radius,
		FVector(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y), FMath::Max(Start.Z, End.Z)) + Radius };
	TArray<UShapeComponent*> Shapes;
	World->GatherShapesInBounds(SweptBounds, nullptr, Shapes);

	TArray<UShapeComponent*> Candidates;
	for (UShapeComponent* Other : Shapes)
	{
		if (Other && Other->IsBlockComponent() && Other->GetOwner() != GetOwner())
			Candidates.Add(Other);
	}
	if (Candidates.IsEmpty())
		return 1.0f;

	// 2. 피벗부터 이미 박혀 있으면 팔을 0으로 (UE도 시작 겹침이면 피벗 위치로 당긴다)
	if (ProbeOverlaps(Start, Candidates))
		return 0.0f;

	// 3. 구 반지름 간격으로 전진한다. 간격이 반지름 이하라 얇은 벽도 건너뛰지 않는다 (64칸 상한 안에서)
	const int32 NumSteps = FMath::Clamp((int32)std::ceil(Distance / ProbeSize), 1, MaxSweepSteps);
	float SafeTime = 0.0f;
	float HitTime = -1.0f;
	for (int32 Step = 1; Step <= NumSteps; ++Step)
	{
		const float Time = (float)Step / NumSteps;
		if (ProbeOverlaps(Start + Delta * Time, Candidates))
		{
			HitTime = Time;
			break;
		}
		SafeTime = Time;
	}
	if (HitTime < 0.0f)
		return 1.0f;

	// 4. 이분 탐색으로 닿기 직전(안 겹치는 쪽)을 찾는다
	for (int32 Iter = 0; Iter < BinarySearchIterations; ++Iter)
	{
		const float Mid = (SafeTime + HitTime) * 0.5f;
		if (ProbeOverlaps(Start + Delta * Mid, Candidates))
			HitTime = Mid;
		else
			SafeTime = Mid;
	}
	return SafeTime;
}
