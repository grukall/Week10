#include "EnginePCH.h"
#include "ShapeCollision.h"

#include "Math/EngineMath.h"
#include "UObject/Casts.h"
#include "Component/BoxComponent.h"
#include "Component/CapsuleComponent.h"
#include "Component/SphereComponent.h"

namespace
{
	// 중심이 완전히 겹쳐 방향을 정할 수 없을 때 쓰는 법선
	const FVector FallbackNormal = FVector(0.0f, 0.0f, 1.0f);

	FVector ClosestPointOnSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float LenSq = AB.Dot(AB);
		if (LenSq <= SMALL_NUMBER)
			return A;

		const float T = FMath::Clamp((P - A).Dot(AB) / LenSq, 0.0f, 1.0f);
		return A + AB * T;
	}

	// 두 선분 P1–Q1, P2–Q2 위에서 서로 가장 가까운 점 (Real-Time Collision Detection 5.1.9)
	void ClosestPointsSegmentSegment(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2,
		FVector& OutC1, FVector& OutC2)
	{
		const FVector D1 = Q1 - P1;
		const FVector D2 = Q2 - P2;
		const FVector R = P1 - P2;
		const float A = D1.Dot(D1);
		const float E = D2.Dot(D2);
		const float F = D2.Dot(R);

		float S = 0.0f;
		float T = 0.0f;

		if (A <= SMALL_NUMBER && E <= SMALL_NUMBER)
		{
			// 둘 다 점
		}
		else if (A <= SMALL_NUMBER)
		{
			T = FMath::Clamp(F / E, 0.0f, 1.0f);
		}
		else
		{
			const float C = D1.Dot(R);
			if (E <= SMALL_NUMBER)
			{
				S = FMath::Clamp(-C / A, 0.0f, 1.0f);
			}
			else
			{
				const float B = D1.Dot(D2);
				const float Denom = A * E - B * B;

				// 평행이면 Denom이 0이라 아무 S에서 시작해도 된다
				S = (Denom > SMALL_NUMBER) ? FMath::Clamp((B * F - C * E) / Denom, 0.0f, 1.0f) : 0.0f;
				T = (B * S + F) / E;

				// T가 선분 밖이면 끝점으로 붙이고 S를 다시 구한다
				if (T < 0.0f)
				{
					T = 0.0f;
					S = FMath::Clamp(-C / A, 0.0f, 1.0f);
				}
				else if (T > 1.0f)
				{
					T = 1.0f;
					S = FMath::Clamp((B - C) / A, 0.0f, 1.0f);
				}
			}
		}

		OutC1 = P1 + D1 * S;
		OutC2 = P2 + D2 * T;
	}

	// 박스 로컬 축으로 투영해 각 축 범위로 자르면 OBB 위의 최근접점이 된다
	FVector ClosestPointOnBox(const FVector& P, const FCollisionBox& Box)
	{
		const FVector D = P - Box.Center;
		FVector Result = Box.Center;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Dist = FMath::Clamp(D.Dot(Box.Axis[i]), -Box.Extent.V[i], Box.Extent.V[i]);
			Result += Box.Axis[i] * Dist;
		}
		return Result;
	}

	// 밖이면 표면까지 거리, 안이면 가장 가까운 면까지 거리의 음수.
	// 안쪽을 0으로 두면 관통한 구간이 평평해져 가장 깊은 점을 고를 수 없다
	float SignedDistanceToBox(const FVector& P, const FCollisionBox& Box)
	{
		const FVector Closest = ClosestPointOnBox(P, Box);
		const float OutsideSq = (P - Closest).Dot(P - Closest);
		if (OutsideSq > SMALL_NUMBER)
			return std::sqrt(OutsideSq);

		const FVector D = P - Box.Center;
		float MinFaceDist = FLT_MAX;
		for (int32 i = 0; i < 3; ++i)
			MinFaceDist = std::min(MinFaceDist, Box.Extent.V[i] - std::abs(D.Dot(Box.Axis[i])));
		return -MinFaceDist;
	}

	// 박스를 축 L에 투영했을 때의 반 길이
	float ProjectBoxRadius(const FCollisionBox& Box, const FVector& L)
	{
		return Box.Extent.X * std::abs(Box.Axis[0].Dot(L))
			+ Box.Extent.Y * std::abs(Box.Axis[1].Dot(L))
			+ Box.Extent.Z * std::abs(Box.Axis[2].Dot(L));
	}

	enum class EShapeKind : uint8 { Box, Capsule, Sphere, None };

	EShapeKind GetShapeKind(UShapeComponent* Shape)
	{
		if (Cast<UBoxComponent>(Shape)) return EShapeKind::Box;
		if (Cast<UCapsuleComponent>(Shape)) return EShapeKind::Capsule;
		if (Cast<USphereComponent>(Shape)) return EShapeKind::Sphere;
		return EShapeKind::None;
	}
}

namespace ShapeCollision
{
	FCollisionSphere MakeSphere(const USphereComponent& Sphere, const FVector& Offset)
	{
		return { Sphere.GetWorldLocation() + Offset, Sphere.GetScaledSphereRadius() };
	}

	FCollisionCapsule MakeCapsule(const UCapsuleComponent& Capsule, const FVector& Offset)
	{
		FVector X, Y, Z;
		Capsule.GetWorldAxes(X, Y, Z);

		const FVector Center = Capsule.GetWorldLocation() + Offset;
		const FVector HalfSegment = Z * Capsule.GetScaledCapsuleHalfHeightWithoutHemisphere();
		return { Center - HalfSegment, Center + HalfSegment, Capsule.GetScaledCapsuleRadius() };
	}

	FCollisionBox MakeBox(const UBoxComponent& Box, const FVector& Offset)
	{
		FCollisionBox Result;
		Result.Center = Box.GetWorldLocation() + Offset;
		Box.GetWorldAxes(Result.Axis[0], Result.Axis[1], Result.Axis[2]);
		Result.Extent = Box.GetScaledBoxExtent();
		return Result;
	}

	bool BoxBox(const FCollisionBox& A, const FCollisionBox& B, FContact* OutContact)
	{
		// 분리축 정리(SAT): 15개 후보 축 중 하나라도 투영이 안 겹치면 떨어져 있다.
		// 모두 겹치면 가장 적게 겹친 축이 밀어낼 방향이다.
		const FVector T = B.Center - A.Center;
		float BestDepth = FLT_MAX;
		float BestScore = FLT_MAX;
		FVector BestAxis = FallbackNormal;

		auto TestAxis = [&](const FVector& Axis, bool bEdgeAxis) -> bool
		{
			const float LenSq = Axis.Dot(Axis);
			// 평행한 모서리끼리의 외적은 0이라 축이 될 수 없다
			if (LenSq < KINDA_SMALL_NUMBER)
				return true;

			const FVector L = Axis * (1.0f / std::sqrt(LenSq));
			const float Dist = T.Dot(L);
			const float Overlap = ProjectBoxRadius(A, L) + ProjectBoxRadius(B, L) - std::abs(Dist);
			if (Overlap < 0.0f)
				return false;

			// 모서리 축은 수치 오차로 면 축과 거의 같은 값이 나오기 쉬워 조금 불리하게 비교한다
			const float Score = bEdgeAxis ? Overlap * 1.05f : Overlap;
			if (Score < BestScore)
			{
				BestScore = Score;
				BestDepth = Overlap;
				BestAxis = (Dist < 0.0f) ? -L : L;
			}
			return true;
		};

		for (int32 i = 0; i < 3; ++i)
		{
			if (!TestAxis(A.Axis[i], false)) return false;
			if (!TestAxis(B.Axis[i], false)) return false;
		}
		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				if (!TestAxis(A.Axis[i].Cross(B.Axis[j]), true)) return false;
			}
		}

		if (OutContact)
		{
			OutContact->Normal = BestAxis;
			OutContact->Depth = BestDepth;
			// 정확한 접촉 다각형 대신 서로의 최근접점 중간으로 근사한다
			OutContact->Point = (ClosestPointOnBox(B.Center, A) + ClosestPointOnBox(A.Center, B)) * 0.5f;
		}
		return true;
	}

	bool BoxCapsule(const FCollisionBox& A, const FCollisionCapsule& B, FContact* OutContact)
	{
		// 선분 위 점에서 박스까지의 부호 있는 거리는 t에 대해 볼록이라 삼분 탐색으로 최소점을 찾는다.
		// 그 점에 캡슐 반지름의 구를 놓고 구-박스 판정
		const FVector Segment = B.B - B.A;
		const float Length = Segment.Size();

		float Lo = 0.0f;
		float Hi = 1.0f;
		// 구간이 2/3씩 줄어 64번이면 float 정밀도를 넘는다
		for (int32 Iter = 0; Iter < 64 && (Hi - Lo) * Length > KINDA_SMALL_NUMBER; ++Iter)
		{
			const float M1 = Lo + (Hi - Lo) / 3.0f;
			const float M2 = Hi - (Hi - Lo) / 3.0f;
			if (SignedDistanceToBox(B.A + Segment * M1, A) < SignedDistanceToBox(B.A + Segment * M2, A))
				Hi = M2;
			else
				Lo = M1;
		}

		const float T = (Lo + Hi) * 0.5f;
		return BoxSphere(A, { B.A + Segment * T, B.Radius }, OutContact);
	}

	bool BoxSphere(const FCollisionBox& A, const FCollisionSphere& B, FContact* OutContact)
	{
		const FVector Closest = ClosestPointOnBox(B.Center, A);
		const FVector Delta = B.Center - Closest;
		const float DistSq = Delta.Dot(Delta);
		if (DistSq > B.Radius * B.Radius)
			return false;

		if (!OutContact)
			return true;

		if (DistSq > SMALL_NUMBER)
		{
			const float Dist = std::sqrt(DistSq);
			OutContact->Normal = Delta * (1.0f / Dist);
			OutContact->Depth = B.Radius - Dist;
			OutContact->Point = Closest;
			return true;
		}

		// 구 중심이 박스 안: 가장 가까운 면 쪽으로 밀어낸다
		const FVector D = B.Center - A.Center;
		int32 BestAxis = 0;
		float BestFaceDist = FLT_MAX;
		float BestSign = 1.0f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Local = D.Dot(A.Axis[i]);
			const float FaceDist = A.Extent.V[i] - std::abs(Local);
			if (FaceDist < BestFaceDist)
			{
				BestFaceDist = FaceDist;
				BestAxis = i;
				BestSign = (Local < 0.0f) ? -1.0f : 1.0f;
			}
		}

		OutContact->Normal = A.Axis[BestAxis] * BestSign;
		OutContact->Depth = B.Radius + BestFaceDist;
		OutContact->Point = B.Center + OutContact->Normal * BestFaceDist;
		return true;
	}

	bool CapsuleCapsule(const FCollisionCapsule& A, const FCollisionCapsule& B, FContact* OutContact)
	{
		// 두 선분의 최근접점에 놓인 구 두 개의 판정과 같다
		FVector OnA, OnB;
		ClosestPointsSegmentSegment(A.A, A.B, B.A, B.B, OnA, OnB);
		return SphereSphere({ OnA, A.Radius }, { OnB, B.Radius }, OutContact);
	}

	bool CapsuleSphere(const FCollisionCapsule& A, const FCollisionSphere& B, FContact* OutContact)
	{
		// 구 중심에 가장 가까운 선분 위 점에 캡슐 반지름의 구를 놓고 구-구 판정
		const FVector OnA = ClosestPointOnSegment(B.Center, A.A, A.B);
		return SphereSphere({ OnA, A.Radius }, B, OutContact);
	}

	bool SphereSphere(const FCollisionSphere& A, const FCollisionSphere& B, FContact* OutContact)
	{
		const FVector Delta = B.Center - A.Center;
		const float DistSq = Delta.Dot(Delta);
		const float RadiusSum = A.Radius + B.Radius;
		if (DistSq > RadiusSum * RadiusSum)
			return false;

		if (OutContact)
		{
			const float Dist = std::sqrt(DistSq);
			OutContact->Normal = (Dist > SMALL_NUMBER) ? Delta * (1.0f / Dist) : FallbackNormal;
			OutContact->Depth = RadiusSum - Dist;
			// 겹친 구간의 가운데
			OutContact->Point = A.Center + OutContact->Normal * (A.Radius - OutContact->Depth * 0.5f);
		}
		return true;
	}

	bool OverlapShapes(UShapeComponent* A, UShapeComponent* B, FContact* OutContact, const FVector& OffsetA)
	{
		if (!A || !B || A == B)
			return false;

		const EShapeKind KindA = GetShapeKind(A);
		const EShapeKind KindB = GetShapeKind(B);
		if (KindA == EShapeKind::None || KindB == EShapeKind::None)
			return false;

		// 함수는 Box → Capsule → Sphere 순서로만 있으니, 뒤집혀 들어오면 바꿔 부르고 법선을 되돌린다
		if (KindA > KindB)
		{
			// A를 +OffsetA 옮기는 것과 B를 -OffsetA 옮기는 것은 상대 위치가 같다.
			// 다만 전체가 -OffsetA만큼 밀린 좌표에서 계산되므로 접촉점은 되돌려 준다.
			const bool bHit = OverlapShapes(B, A, OutContact, -OffsetA);
			if (bHit && OutContact)
			{
				OutContact->Normal = -OutContact->Normal;
				OutContact->Point = OutContact->Point + OffsetA;
			}
			return bHit;
		}

		switch (KindA)
		{
		case EShapeKind::Box:
		{
			const FCollisionBox BoxA = MakeBox(*Cast<UBoxComponent>(A), OffsetA);
			if (KindB == EShapeKind::Box)     return BoxBox(BoxA, MakeBox(*Cast<UBoxComponent>(B)), OutContact);
			if (KindB == EShapeKind::Capsule) return BoxCapsule(BoxA, MakeCapsule(*Cast<UCapsuleComponent>(B)), OutContact);
			return BoxSphere(BoxA, MakeSphere(*Cast<USphereComponent>(B)), OutContact);
		}
		case EShapeKind::Capsule:
		{
			const FCollisionCapsule CapsuleA = MakeCapsule(*Cast<UCapsuleComponent>(A), OffsetA);
			if (KindB == EShapeKind::Capsule) return CapsuleCapsule(CapsuleA, MakeCapsule(*Cast<UCapsuleComponent>(B)), OutContact);
			return CapsuleSphere(CapsuleA, MakeSphere(*Cast<USphereComponent>(B)), OutContact);
		}
		default:
			return SphereSphere(MakeSphere(*Cast<USphereComponent>(A), OffsetA), MakeSphere(*Cast<USphereComponent>(B)), OutContact);
		}
	}
}
