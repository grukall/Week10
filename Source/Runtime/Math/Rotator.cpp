#include "EnginePCH.h"
#include "Rotator.h"
#include "Math/EngineMath.h"

FRotator::FRotator()
{
	Pitch = 0; Yaw = 0; Roll = 0;
}

FRotator::FRotator(float P, float Y, float R)
{
	Pitch = P; Yaw = Y; Roll = R;
}

FRotator::FRotator(FVector V)
{
	Pitch = V.X; Yaw = V.Y; Roll = V.Z;
}


FQuat FRotator::Quaternion() const
{
	// Degree를 Radian으로 변환
	float PRad = Pitch * (PI / 180.0f);
	float YRad = Yaw * (PI / 180.0f);
	float RRad = Roll * (PI / 180.0f);
	
	return FQuat::MakeFromEuler(RRad, PRad, YRad);
}

FRotator FRotator::Identitiy = FRotator(0, 0, 0);

FRotator operator+(FRotator Rot, const FVector& Vec)
{
	Rot.Roll += Vec.X;
	Rot.Pitch += Vec.Y;
	Rot.Yaw += Vec.Z;

	return Rot;
}

// From에서 To를 바라보는 회전. Pitch·Yaw 부호 규칙을 가정하지 않고, 실제 Forward가 방향과 가장 잘 맞는 조합을 고른다
FRotator FRotator::FindLookAtRotation(const FVector& From, const FVector& To)
{
	const FVector Dir = (To - From).Normalized();
	const float Yaw = FMath::RadiansToDegrees(std::atan2(Dir.Y, Dir.X));
	const float Pitch = FMath::RadiansToDegrees(std::asin(FMath::Clamp(Dir.Z, -1.0f, 1.0f)));

	FRotator Best(Pitch, Yaw, 0.0f);
	float BestDot = -2.0f;
	for (const float PitchSign : { 1.0f, -1.0f })
	{
		for (const float YawSign : { 1.0f, -1.0f })
		{
			const FRotator Candidate(Pitch * PitchSign, Yaw * YawSign, 0.0f);
			const float Dot = FVector::Dot(Candidate.Quaternion().GetForwardVector(), Dir);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				Best = Candidate;
			}
		}
	}
	return Best;
}