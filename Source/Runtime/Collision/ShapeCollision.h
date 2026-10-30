#pragma once

#include "Math/Vector.h"

class UShapeComponent;
class UBoxComponent;
class UCapsuleComponent;
class USphereComponent;

// 판정 함수는 컴포넌트 대신 월드 기준 순수 도형만 받는다.
struct FCollisionSphere
{
	FVector Center;
	float Radius;
};

// 선분 A–B에서 Radius 이내인 점들의 집합
struct FCollisionCapsule
{
	FVector A;
	FVector B;
	float Radius;
};

// OBB. Axis는 정규화된 월드 축, Extent는 각 축의 반 크기
struct FCollisionBox
{
	FVector Center;
	FVector Axis[3];
	FVector Extent;
};

// 겹쳤을 때의 접촉 정보. Normal은 A에서 B로 향하고, B를 Normal * Depth만큼 옮기면 떨어진다.
struct FContact
{
	FVector Normal;
	float Depth = 0.0f;
	FVector Point;
};

namespace ShapeCollision
{
	// Offset: 컴포넌트를 실제로 옮기지 않고 그만큼 옮긴 위치의 도형을 만든다 (Sweep용)
	FCollisionSphere MakeSphere(const USphereComponent& Sphere, const FVector& Offset = FVector::ZeroVector);
	FCollisionCapsule MakeCapsule(const UCapsuleComponent& Capsule, const FVector& Offset = FVector::ZeroVector);
	FCollisionBox MakeBox(const UBoxComponent& Box, const FVector& Offset = FVector::ZeroVector);

	// 인자 순서는 Box → Capsule → Sphere. OverlapShapes가 이 순서로 맞춰 부른다.
	bool BoxBox(const FCollisionBox& A, const FCollisionBox& B, FContact* OutContact);
	// 캡슐을 선분 위 구 여러 개로 근사한다.
	bool BoxCapsule(const FCollisionBox& A, const FCollisionCapsule& B, FContact* OutContact);
	bool BoxSphere(const FCollisionBox& A, const FCollisionSphere& B, FContact* OutContact);
	bool CapsuleCapsule(const FCollisionCapsule& A, const FCollisionCapsule& B, FContact* OutContact);
	bool CapsuleSphere(const FCollisionCapsule& A, const FCollisionSphere& B, FContact* OutContact);
	bool SphereSphere(const FCollisionSphere& A, const FCollisionSphere& B, FContact* OutContact);

	// 두 Shape 컴포넌트의 종류를 보고 위 함수 중 하나로 보낸다. Normal은 A에서 B로 향한다.
	// OffsetA: A를 그만큼 옮겼다고 치고 판정한다. Sweep이 컴포넌트를 실제로 옮기지 않고 여러 위치를 시험할 때 쓴다.
	bool OverlapShapes(UShapeComponent* A, UShapeComponent* B, FContact* OutContact = nullptr, const FVector& OffsetA = FVector::ZeroVector);
}
