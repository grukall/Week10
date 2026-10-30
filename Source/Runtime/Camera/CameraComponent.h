#pragma once

#include "UObject/Class.h"
#include "Camera/CameraProjection.h"
#include "../Component/SceneComponent.h"
#include "../Math/EngineMath.h"
#include "../Math/Rotator.h"

struct FMinimalViewInfo;

class UCameraComponent : public USceneComponent
{
	DECLARE_CLASS(UCameraComponent, USceneComponent)
	// 행렬·FRay를 쓰는 함수와 에디터 입력 처리(Update*)는 노출하지 않는다
	REFLECT_START(ClassName)
		// 레벨 시퀀스 Float 트랙이 "CameraComponent.FieldOfView"로 줌을 움직일 수 있게 프로퍼티로도 노출한다
		PROPERTY_FLAGS(FieldOfView, CPF_EditAnywhere)
		// 화면 효과도 같은 이유로 프로퍼티다. 시퀀스가 시네 카메라의 값을 키로 움직인다
		PROPERTY_FLAGS(VignetteIntensity, CPF_EditAnywhere)
		PROPERTY_FLAGS(AspectRatio, CPF_EditAnywhere)
		PROPERTY_FLAGS(bConstrainAspectRatio, CPF_EditAnywhere)
		FUNCTION(GetFieldOfView)
		FUNCTION(SetFieldOfView)
		FUNCTION(GetAspectRatio)
		FUNCTION(SetAspectRatio)
		FUNCTION(GetConstrainAspectRatio)
		FUNCTION(SetConstrainAspectRatio)
		FUNCTION(GetNearZ)
		FUNCTION(SetNearZ)
		FUNCTION(GetFarZ)
		FUNCTION(SetFarZ)
		FUNCTION(GetIsOrthogonal)
		FUNCTION(SetIsOrthogonal)
		FUNCTION(GetOrthoWidth)
		FUNCTION(SetOrthoWidth)
		FUNCTION(GetMoveSpeed)
		FUNCTION(SetMoveSpeed)
		FUNCTION(GetMouseSensitivity)
		FUNCTION(SetMouseSensitivity)
		FUNCTION(GetWheelSpeed)
		FUNCTION(SetWheelSpeed)
		FUNCTION(SetExternalInputManaged)
		FUNCTION(IsExternalInputManaged)
	REFLECT_END()

public:
	UCameraComponent() { PrimaryComponentTick.bCanEverTick = true; }   // 이동·회전 입력을 매 프레임 처리한다

	// 매 프레임 카메라 입력/이동 처리
	virtual void TickComponent(float DeltaTime) override;

	// Camera 행렬 계산
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FCameraProjection GetProjectionSettings() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetProjectionMatrix(float ViewAspectRatio) const;
	// CameraComponent와 SceneView가 공유하는 역투영. 원근은 논리 위치, 직교는 렌더 위치를 전달한다.
	static FRay DeprojectView(const FMatrix& View, const FMatrix& Projection, const FVector& Origin, FVector2 MousePos, FVector2 ViewSize, bool bOrthographic);
	FMatrix GetViewProjectionMatrix() const;

	// Picking
	FRay DeProjection(FVector2 MousePos, float ScreenW, float ScreenH);

	// Get & Set
	float GetFieldOfView() const { return FieldOfView; }
	void SetFieldOfView(float InFieldOfView) { FieldOfView = InFieldOfView; }

	float GetAspectRatio() const { return AspectRatio; }
	void SetAspectRatio(float InAspectRatio) { AspectRatio = InAspectRatio; }

	// true면 화면 비율이 AspectRatio와 다를 때 남는 영역을 검은 띠로 채운다
	bool GetConstrainAspectRatio() const { return bConstrainAspectRatio; }
	void SetConstrainAspectRatio(bool bInConstrainAspectRatio) { bConstrainAspectRatio = bInConstrainAspectRatio; }

	float GetNearZ() const { return NearClipPlane; }
	void SetNearZ(float InNearZ) { NearClipPlane = InNearZ; }

	float GetFarZ() const { return FarClipPlane; }
	void SetFarZ(float InFarZ) { FarClipPlane = InFarZ; }

	bool GetIsOrthogonal() const { return bIsOrthogonal; }
	void SetIsOrthogonal(bool bInIsOrthographic) { bIsOrthogonal = bInIsOrthographic; }

	float GetOrthoWidth() const { return OrthoWidth; }
	void SetOrthoWidth(float InOrthoWidth) { OrthoWidth = InOrthoWidth; }

	// 마우스 조작 설정
	float GetMoveSpeed() const { return MoveSpeed; }
	void SetMoveSpeed(float InMoveSpeed) { MoveSpeed = InMoveSpeed; }

	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetMouseSensitivity(float InMouseSensitivity) { MouseSensitivity = InMouseSensitivity; }

	float GetWheelSpeed() const { return WheelSpeed; }
	void SetWheelSpeed(float InWheelSpeed) { WheelSpeed = InWheelSpeed; }
	// 뷰포트 클라이언트가 View별 입력을 처리할 때 기본 카메라의 중복 입력 갱신을 끈다.
	void SetExternalInputManaged(bool bValue) { bExternalInputManaged = bValue; }
	bool IsExternalInputManaged() const { return bExternalInputManaged; }

	// 마우스 조작 업데이트
	void UpdateMovement(float DeltaTime); // 마우스 이동
	void UpdateRotation(float DeltaTime); // 마우스 우클릭 회전
	void UpdateZoom(float DeltaTime);     // 마우스 휠 줌

	void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV);

	// 화면 모서리를 어둡게 하는 정도. 0이면 끔
	float VignetteIntensity = 0.0f;
protected:
	float FieldOfView = 60.0f;
	float AspectRatio = 16.0f / 9.0f;
	bool bConstrainAspectRatio = false;
	float NearClipPlane = 0.1f;
	float FarClipPlane = 10000.0f;

	bool bIsOrthogonal = false;
	float OrthoWidth = 16.0f;
	
	float MoveSpeed = 10.0f;
	float MouseSensitivity = 0.05f;
	float WheelSpeed = 0.1f;
	bool bExternalInputManaged = false;

};

