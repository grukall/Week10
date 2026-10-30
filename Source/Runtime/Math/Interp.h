#pragma once
#include <cmath>
#include "Math/EngineMath.h"
#include "Math/MathUtility.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

// Lerp는 EngineMath.h에 있다
namespace FMath
{
	// ============================================================ 
    // InterpTo: 매 프레임 목표를 따라감 (정해진 시간이 없음. Ex. Spring Arm)

	// 남은 거리에 비례. 처음엔 빠르고 목표에 가까울수록 느려짐.
	static inline float FInterpTo(float Current, float Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.f)
			return Target;
		const float Dist = Target - Current;
		if (Dist * Dist < SMALL_NUMBER)
			return Target;
		return Current + Dist * Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);
	}

	// 초당 InterpSpeed 만큼 일정한 속도로 움직인다 (등속이동)
	static inline float FInterpConstantTo(float Current, float Target, float DeltaTime, float InterpSpeed)
	{
        if (InterpSpeed <= 0.f)
			return Target;
		const float Dist = Target - Current;
		if (Dist * Dist < SMALL_NUMBER)
			return Target;
		const float Step = InterpSpeed * DeltaTime;
		return Current + Clamp(Dist, -Step, Step);
	}

    // 비율 이동 ( 남은 거리가 멀면 빠르게 가까우면 천천히 이동 (Ease-Out) )
	static inline FVector VInterpTo(const FVector& Current, const FVector& Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.f)
			return Target;
		const FVector Dist = Target - Current;
		if (Dist.SizeSquared() < KINDA_SMALL_NUMBER)
			return Target;
		return Current + Dist * Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);
	}

	// 각도를 -180 ~ 180 으로 정규화. 각도가 작은 쪽으로 돎.
	static inline float NormalizeAxis(float Angle)
	{
		Angle = std::fmod(Angle, 360.f);
		if (Angle > 180.f) Angle -= 360.f;
		else if (Angle <= -180.f) Angle += 360.f;
		return Angle;
	}

    // 회전 Interp
	static inline FRotator RInterpTo(const FRotator& Current, const FRotator& Target, float DeltaTime, float InterpSpeed)
	{
		if (DeltaTime == 0.f)
			return Current;
		if (InterpSpeed <= 0.f)
			return Target;

		const float DP = NormalizeAxis(Target.Pitch - Current.Pitch);
		const float DY = NormalizeAxis(Target.Yaw - Current.Yaw);
		const float DR = NormalizeAxis(Target.Roll - Current.Roll);
		if (std::abs(DP) < KINDA_SMALL_NUMBER && std::abs(DY) < KINDA_SMALL_NUMBER && std::abs(DR) < KINDA_SMALL_NUMBER)
			return Target;

		const float Alpha = Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);
		return FRotator(NormalizeAxis(Current.Pitch + DP * Alpha),
		                NormalizeAxis(Current.Yaw + DY * Alpha),
		                NormalizeAxis(Current.Roll + DR * Alpha));
	}
	// ============================================================ 

    // ============================================================ 
	// Ease: A -> B 를 Alpha(0 ~ 1) 로 (정해진 시간이 있음 Ex. Fade, LetterBox, Transition)
    
    // 천천히 출발
	template <typename T>
	static inline T InterpEaseIn(const T& A, const T& B, float Alpha, float Exp)
	{
		return Lerp(A, B, std::pow(Alpha, Exp));
	}
    // 천천히 도착
	template <typename T>
	static inline T InterpEaseOut(const T& A, const T& B, float Alpha, float Exp)
	{
		return Lerp(A, B, 1.f - std::pow(1.f - Alpha, Exp));
	}

	// 앞 절반은 EaseIn, 뒤 절반은 EaseOut ( 출발, 도착 감속 )
	template <typename T>
	static inline T InterpEaseInOut(const T& A, const T& B, float Alpha, float Exp)
	{
		const float Eased = (Alpha < 0.5f)
			? InterpEaseIn(0.f, 1.f, Alpha * 2.f, Exp) * 0.5f
			: InterpEaseOut(0.f, 1.f, Alpha * 2.f - 1.f, Exp) * 0.5f + 0.5f;
		return Lerp(A, B, Eased);
	}

    // ============================================================ 
    // 커브 보간
	// Hermite: 위치 두 개 (P0, P1) + 기울기 두 개
	template <typename T>
	static inline T CubicInterp(const T& P0, const T& T0, const T& P1, const T& T1, float A)
	{
		const float A2 = A * A;
		const float A3 = A2 * A;
		return P0 * (2.f * A3 - 3.f * A2 + 1.f) + T0 * (A3 - 2.f * A2 + A) + T1 * (A3 - A2) + P1 * (-2.f * A3 + 3.f * A2);
	}

	// Bezier: 위치 네 개 ( 지나가는 점은 P0, P3 )
	// 언리얼 UE::Curves::BezierInterp (CurveEvaluation.h). Lerp를 반복하는 de Casteljau 방식
	template <typename T>
	static inline T BezierInterp(const T& P0, const T& P1, const T& P2, const T& P3, float Alpha)
	{
		const T P01 = Lerp(P0, P1, Alpha);
		const T P12 = Lerp(P1, P2, Alpha);
		const T P23 = Lerp(P2, P3, Alpha);
		const T P012 = Lerp(P01, P12, Alpha);
		const T P123 = Lerp(P12, P23, Alpha);
		return Lerp(P012, P123, Alpha);
	}
}