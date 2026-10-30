#pragma once
#include "Math/Color.h"
#include "Math/Vector2.h"
#include "Core/NameTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "GameFramework/Actor.h"
#include "CameraTypes.h"
#include "Camera/CameraShakeBase.h"

class APlayerController;
class UCameraModifier;
class UCameraModifier_CameraShake;

enum class EViewTargetBlendFunction : uint8
{
	Linear,
	Cubic,
	EaseIn,
	EaseOut,
	EaseInOut,
};

// "Linear", "Cubic", "EaseIn", "EaseOut", "EaseInOut" → enum. 모르는 이름이면 false (Lua SetViewTarget, 레벨 시퀀스가 같이 쓴다)
bool TryParseViewTargetBlendFunction(const FString& Name, EViewTargetBlendFunction& OutFunction);

struct FViewTarget
{
	FViewTarget() = default;
	~FViewTarget() = default;

	FViewTarget(const FViewTarget&) = delete;

	TWeakObjectPtr<AActor> Target;
	
	FMinimalViewInfo POV;


};

struct FViewTargetTransitionParams
{
	//전환에 걸리는 시간. 0이면 컷 전환
	float BlendTime = 0.0f;
	//진행률 t를 어떤 곡선으로 바꿀지
	EViewTargetBlendFunction BlendFunction = EViewTargetBlendFunction::Cubic;
	//Ease(EaseIn, EaseOut, EaseInOut) 계열 곡선의 강도
	float BlendExp = 2.0f;
	//떠나는 쪽 POV를 전환 시작 순간 값으로 고정
	//만약 플레이어가 보스몬스터한테 달려가면서 카메라가 보스한테 이동하는 연출을 한다 생각했을때
	//플레이어의 POV 와 보스몬스터의 POV를 이용해서 천천히 이동해야하는데, 플레이어가 이동중이라 매 프레임 변하는 플레이어의 POV와 보스몬스터의 POV을 사용하게되면
	//이상해지므로, 전환 시작 순간의 플레이어 POV와 보스몬스터의 POV만을 가지고 계산할 수 있게해주는 변수
	bool  bLockOutgoing  = false;


};

class APlayerCameraManager : public AActor
{
	DECLARE_CLASS(APlayerCameraManager, AActor)

public:
	APlayerCameraManager() = default;
	~APlayerCameraManager() override;
	void EndPlay() override;

	APlayerCameraManager(const APlayerCameraManager&) = delete;
	APlayerCameraManager& operator=(APlayerCameraManager&) = delete;

	void InitializeFor(APlayerController* PC);

	//BlendTime > 0이면 PendingViewTarget에 두고 블렌드, 0이면 ViewTarget을 바로 교체
	void SetViewTarget(AActor* NewTarget, const FViewTargetTransitionParams& TransitionParams = FViewTargetTransitionParams());
	void UpdateCamera(float deltaTime);

	FMinimalViewInfo GetCameraCacheView() const { return CameraCachePOV; };

	// Modifier: 블렌드까지 끝난 최종 POV를 매 프레임 고친다. Priority가 낮은 것부터 실행한다
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);
	void RemoveCameraModifier(UCameraModifier* Modifier);
	void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);

	// Camera Shake: 하나의 Shake Modifier가 여러 Shake 인스턴스를 관리한다
	UCameraShakeBase* StartCameraShake(UClass* ShakeClass, float Scale = 1.0f,
		ECameraShakePlaySpace Space = ECameraShakePlaySpace::CameraLocal);
	UCameraShakeBase* StartCameraShake(float Strength, float Duration, float Frequency = 20.0f);
	// 사인파 대신 펄린 노이즈로 흔든다 (불규칙한 손떨림·지진 느낌)
	UCameraShakeBase* StartPerlinNoiseCameraShake(float Strength, float Duration, float Frequency = 10.0f);
	void StopCameraShake(UCameraShakeBase* Shake);
	void StopAllCameraShakes();

	// Fade: 렌더러는 IsFading일 때 FadeColor를 FadeAmount만큼 덮는다
	// bInHoldWhenFinished가 false면 끝나는 순간 Fade가 꺼진다
	void StartCameraFade(float FromAlpha, float ToAlpha, float InFadeTime, FLinearColor InFadeColor, bool bInHoldWhenFinished = false);
	void StopCameraFade();
	// 시간 보간 없이 Fade 양을 직접 정한다
	void SetManualCameraFade(float InFadeAmount, FLinearColor InFadeColor);
	bool IsFading() const { return bEnableFading; }
	FLinearColor GetFadeColor() const { return FadeColor; }
	float GetFadeAmount() const { return FadeAmount; }


	// 현재 카메라 시점을 제공하는 대상. 블렌드 중이면 떠나는 쪽이다
	AActor* GetViewTarget() const { return ViewTarget.Target.Get(); }
	// PendingViewTarget으로 블렌드하는 중인지
	bool IsBlending() const { return PendingViewTarget.Target.Get() != nullptr; }

private:
	void UpdateViewTarget(FViewTarget& VT, float DeltaTime);
	void FillCameraCache(const FMinimalViewInfo& InCameraCachePov);
	// 블렌드 진행률 T(0~1)를 BlendParams.BlendFunction 곡선에 넣어 실제로 섞을 비율로 바꾼다
	float GetBlendAlpha(float T) const;
	UCameraModifier_CameraShake* GetCameraShakeModifier();
	void ClearCameraModifiers();

private:
	APlayerController* PCOwner = nullptr;

	// Modifier는 PCM이 직접 delete한다. 약한 참조라 전역 UObject 정리 순서에서도 두 번 지우지 않는다
	TArray<TWeakObjectPtr<UCameraModifier>> ModifierList;
	TWeakObjectPtr<UCameraModifier_CameraShake> CachedCameraShakeMod;

	// 이번 프레임 UpdateCamera가 만든 최종 카메라 시점. PCM이 밖으로 내보내는 유일한 출력이다
	// 렌더러는 이 값으로 SceneView를 만들고, 시점이 필요한 다른 시스템(조준, 사운드 리스너 등)도 이 값을 읽는다
	// ViewTarget.POV를 바로 쓰지 않고 따로 두는 이유
	// Pending이 없을 때: 최종 POV = ViewTarget.POV
	// Pending이 있을 때(블렌드 중): 최종 POV = Blend(ViewTarget.POV, PendingViewTarget.POV, Alpha)
	// 이 값은 ViewTarget.POV도 PendingViewTarget.POV도 아닌 제3의 값이라 담아둘 자리가 따로 필요하다
	FMinimalViewInfo CameraCachePOV;

	//현재 카메라 시점을 제공하는 대상과,그 대상으로부터 이번 프레임에 계산한 POV
	FViewTarget ViewTarget;

	//블렌드 전환 중 일 때 도착할 대상과, 그 대상으로부터 이번 프레임에 계산한 POV
	FViewTarget PendingViewTarget;

	//화면을 덮을 색
	FLinearColor FadeColor = FLinearColor(0, 0, 0, 1);

	//현재 프레임의 페이드 정도
	float FadeAmount = 0.0f;

	//시작값,끝값
	FVector2 FadeAlpha = FVector2(0.0f, 0.0f);

	//페이드에 걸리는 전체 시간
	float FadeTime = 0.0f;

	//true인 동안 렌더러가 화면을 FadeColor로 덮는다
	bool bEnableFading = false;
	//true면 UpdateCamera가 FadeAlpha.X -> Y로 FadeAmount를 보간한다. SetManualCameraFade는 false
	bool bAutoAnimateFade = false;
	//true면 끝난 뒤에도 마지막 값을 유지한다
	bool bHoldFadeWhenFinished = false;
	//페이드 남은 시간
	float FadeTimeRemaining = 0.f;

	//ViewTarget이 카메라 액터가 아닐 때, 그 대상에서 POV를 어떤 방식으로 뽑을지 고르는 스위치
	// PlayerController::SetCameraMode로 바꾼다
	// Default     -> Target->CalcCamera. 대상에 붙은 카메라 컴포넌트 값을 쓴다
	// Fixed       -> 이전 프레임 POV를 그대로 유지한다 (카메라 정지)
	// ThirdPerson -> 대상 위치에서 FreeCamDistance만큼 뒤로 뺀 위치. 벽에 막히면 앞으로 당긴다
	// FreeCam     -> ThirdPerson과 같고, 회전만 컨트롤러 회전(마우스)을 쓴다
	// FirstPerson -> 대상의 눈 위치와 방향 (GetActorEyesViewPoint)
	// Default 외에는 디버그용 간이 카메라라 Modifier(쉐이크 등)를 적용하지 않는다
	FName CameraStyle;

	//블렌드 파라미터
	FViewTargetTransitionParams BlendParams;

	//블렌드 남은 시간
	float BlendTimeToGo = 0.0f;

	//아무도 정하지 않았을 때 쓰는 값
	float DefaultFOV = 90.0f;
	float DefaultAspectRatio = 1.333f;
	bool bDefaultConstrainAspectRatio = false;
	
};
