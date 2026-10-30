#pragma once
#include "EnginePCH.h"
#include "Camera/CameraProjection.h"
#include "Math/Rotator.h"
#include "Render/PostProcessSettings.h"

//카메라 시점 하나"를 표현하는 구조체.
// 복사 가능한 값이다. Modifier는 매 프레임 새로 만든 기본 시점을 고친다.
struct FMinimalViewInfo
{
public:
	FMinimalViewInfo() = default;
	~FMinimalViewInfo() = default;

		//카메라 위치
		FVector Location;
		//카메라 방향
		FRotator Rotation;
		//투영 설정 (원근/직교, 시야각, 직교 폭, Near/Far)
		FCameraProjection Projection;
		//화면비
		float AspectRatio = 1.333f;
		//true면 AspectRatio를 강제하고 남는 영역은 검은 띠로 채움 -> letter box
		bool bConstrainAspectRatio = false;
		//화면 효과. 카메라 컴포넌트가 채우고 뷰포트가 SceneView로 넘긴다. 기본은 꺼짐
		FPostProcessSettings PostProcessSettings = { 0.0f };

	// 이 시점에서 Other 쪽으로 Alpha(0~1)만큼 섞는다
	void BlendViewInfo(const FMinimalViewInfo& Other, float Alpha)
	{
		Location = FMath::Lerp(Location, Other.Location, Alpha);
		Rotation = FQuat::Slerp(Rotation.Quaternion(), Other.Rotation.Quaternion(), Alpha).ToFRotator();
		Projection.FovDegrees = FMath::Lerp(Projection.FovDegrees, Other.Projection.FovDegrees, Alpha);
		Projection.OrthoWidth = FMath::Lerp(Projection.OrthoWidth, Other.Projection.OrthoWidth, Alpha);
		Projection.NearClip = FMath::Lerp(Projection.NearClip, Other.Projection.NearClip, Alpha);
		Projection.FarClip = FMath::Lerp(Projection.FarClip, Other.Projection.FarClip, Alpha);
		AspectRatio = FMath::Lerp(AspectRatio, Other.AspectRatio, Alpha);
		PostProcessSettings.VignetteIntensity = FMath::Lerp(PostProcessSettings.VignetteIntensity, Other.PostProcessSettings.VignetteIntensity, Alpha);

		// 원근/직교는 중간값이 없어서 절반을 넘으면 도착 쪽 값으로 바꾼다
		if (Alpha >= 0.5f)
			Projection.Mode = Other.Projection.Mode;

		// 한쪽이라도 레터박스면 블렌드 내내 켜 두고,
		// 위에서 Lerp되는 AspectRatio에 따라 띠가 서서히 두꺼워지거나 얇아진다
		bConstrainAspectRatio |= Other.bConstrainAspectRatio;
	}

	// 게임 화면과 같은 기저 (Roll 제거, 수평 Right). 카메라 쉐이크가 로컬 오프셋을 월드로 돌릴 때 쓴다
	FMatrix GetCameraRotationMatrix() const;
};
