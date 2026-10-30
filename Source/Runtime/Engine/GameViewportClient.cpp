#include "EnginePCH.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Input/InputSystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include <cassert>
#include <cmath>

UCameraComponent* FGameViewportClient::GetCamera() const
{
	const ACameraActor* Actor = World->GetMainCamera();
	return Actor ? Actor->GetCameraComponent() : nullptr;
}

// 게임 카메라는 입력과 상관없이 기본 자유 비행(WASD·우클릭·휠)을 쓰지 않는다.
// WASD는 Lua 스크립트(플레이어 등)가 쓰고, 카메라를 움직이려면 스크립트가 위치·회전을 직접 바꾼다.
void FGameViewportClient::SetInputEnabled(const bool bEnabled)
{
	if (UCameraComponent* Camera = GetCamera())
		Camera->SetExternalInputManaged(true);

	// 입력 권한을 잃으면 HUD 입력과 키 상태를 비운다. 다른 화면을 클릭하거나 누른 것이 게임에 남지 않게 한다.
	if (!bEnabled)
	{
		World->GetHUD().ClearInput();
		for (APlayerController* Controller : World->GetPlayerControllers())
			Controller->FlushPressedKeys();
	}
}

FSceneView FGameViewportClient::CreateSceneView(const FVector2 OutputSize) const
{
	const UCameraComponent* Camera = GetCamera();
	assert(Camera);

	// 0번 PC의 카메라 매니저 결과를 쓰고, 아직 없으면 메인 카메라 값으로 대신한다
	FMinimalViewInfo ViewInfo;
	ViewInfo.Location = Camera->GetWorldLocation();
	ViewInfo.Rotation = Camera->GetWorldRotation();
	ViewInfo.Projection = Camera->GetProjectionSettings();

	const APlayerCameraManager* PCM = nullptr;
	if (APlayerController* PC = World->GetPlayerController(0))
		PCM = PC->GetCameraPlayerManager();
	if (PCM)
		ViewInfo = PCM->GetCameraCacheView();

	// PIE는 기존처럼 Roll을 제거한다. 카메라를 변경하지 않고 Forward에서 수평 기저만 만든다.
	// GetSafeNormal은 기존 inverse-length 곱셈 순서도 보존해 극점 포화 경계가 흔들리지 않는다.
	FVector Forward = ViewInfo.Rotation.Quaternion().GetForwardVector().GetSafeNormal();
	FVector Right = FVector(-Forward.Y, Forward.X, 0.0f).Normalized();
	if (Right.SizeSquared() == 0.0f)
		Right = RightVector; // 수직 시선에서 기존 atan2(0,0)의 Yaw=0 정책.
	// 기존 asin(clamp(Z)) 경로는 float Z가 ±1로 반올림되면 정확한 수직 시선이 된다.
	// 이 포화 정책을 보존해야 큰 직교 후퇴 거리에서 수평 위치가 달라지지 않는다.
	const float Z = FMath::Clamp(Forward.Z, -1.0f, 1.0f);
	const float Horizontal = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
	Forward = FVector(Right.Y * Horizontal, -Right.X * Horizontal, Z);
	FMatrix Rotation = FMatrix::Identity;
	Rotation.SetAxis(0, Forward);
	Rotation.SetAxis(1, Right);
	Rotation.SetAxis(2, Forward.Cross(Right).Normalized());
	FCameraProjection Projection = ViewInfo.Projection;
	FSceneView::ConstrainOrthographicWidth(Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
	FSceneView View(ViewInfo.Location, Rotation, Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y}, GetViewRenderMode());
	// 비네트 등 카메라마다 다른 화면 효과
	View.PostProcessSettings = ViewInfo.PostProcessSettings;

	// 화면 Fade
	if (PCM && PCM->IsFading())
	{
		View.OverlayColor = PCM->GetFadeColor();
		View.OverlayColor.A = FMath::Clamp(PCM->GetFadeAmount(), 0.0f, 1.0f);
	}

	// 레터박스: 카메라가 비율을 강제하면 그 비율에 맞춰 위아래를 가린다
	View.LetterBoxAspect = ViewInfo.AspectRatio;
	View.LetterBoxAmount = ViewInfo.bConstrainAspectRatio ? 1.0f : 0.0f;
	return View;
}

void FGameViewportClient::UpdateInput(const FViewportInput& Input, FVector2 OutputSize)
{
	SetInputEnabled(Input.bEnabled);

	// 렌더할 때와 같은 화면 크기로 배율을 맞추고, 화면 기준 마우스(픽셀)를 HUD 기준 단위로 바꾼다
	FHUD& HUD = World->GetHUD();
	HUD.SetScreenSize(OutputSize);
	const float Scale = HUD.GetScale();

	FHUDInput HUDInput;
	HUDInput.bEnabled = Input.bEnabled;
	HUDInput.MousePosition = FVector2(Input.MousePosition.X / Scale, Input.MousePosition.Y / Scale);
	HUDInput.bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);
	HUDInput.bMousePressed = FInputSystem::IsMousePressed(EMouseButton::Left);
	HUD.SetInput(HUDInput);

	// 포커스를 가진 이 뷰포트의 키 상태를 자기 월드의 PlayerController에게만 넘긴다.
	// 한 키보드를 여러 플레이어가 나눠 쓰므로 모두 같은 상태를 받고, 어떤 키를 쓸지는 각자의 매핑이 거른다.
	bool bKeyDown[256];
	for (int32 Key = 0; Key < 256; ++Key)
		bKeyDown[Key] = FInputSystem::IsKeyDown(static_cast<EKeyCode>(Key));
	for (APlayerController* Controller : World->GetPlayerControllers())
		Controller->InputKeyStates(bKeyDown);
}
