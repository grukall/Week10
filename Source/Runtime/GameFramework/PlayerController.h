#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/InputComponent.h"
#include "PlayerCameraManager.h"

// 키 매핑 하나. 이 키를 누르고 있으면 Axis에 Scale만큼 더한다.
struct FInputAxisKeyMapping
{
	FName AxisName;
	EKeyCode Key = EKeyCode::LButton;
	float Scale = 1.0f;
};

struct FInputActionKeyMapping
{
	FName ActionName;
	EKeyCode Key = EKeyCode::LButton;
};

// 플레이어 하나의 입력을 받는 액터 (UE APlayerController 축소판). GameMode가 플레이 월드마다 스폰한다.
// Pawn은 없다. 입력을 받고 싶은 액터가 EnableInput으로 자기 InputComponent를 입력 스택에 올린다.
//
// 입력 흐름: FViewport(포커스 검사) → FGameViewportClient → APlayerController::InputKeyStates (키 상태만 저장)
//          → UWorld::Tick이 액터 Tick 전에 ProcessPlayerInput → 스택 위부터 InputComponent 바인딩 호출
class APlayerController : public AActor
{
	DECLARE_CLASS(APlayerController, AActor)
	REFLECT_START(ClassName)
		FUNCTION(AddAxisMapping)
		FUNCTION(AddActionMapping)
		FUNCTION(ClearMappings)
		FUNCTION(GetPlayerIndex)
	REFLECT_END()
public:
	APlayerController() = default;
	virtual ~APlayerController() = default;

	virtual void EndPlay() override;

	int32 GetPlayerIndex() const { return PlayerIndex; }
	void SetPlayerIndex(int32 InIndex) { PlayerIndex = InIndex; }

	// Key는 "W", "Space", "Up"처럼 이름으로 받는다. 모르는 이름이면 로그를 남기고 false.
	// 같은 매핑이 이미 있으면 다시 넣지 않는다 (같은 컨트롤러를 쓰는 스크립트가 여럿이어도 두 번 더해지지 않게).
	bool AddAxisMapping(FName AxisName, const FString& Key, float Scale);
	bool AddActionMapping(FName ActionName, const FString& Key);
	void ClearMappings();

	// 스택 맨 위(마지막에 넣은 것)가 먼저 입력을 받는다.
	void PushInputComponent(FInputComponent* Component);
	void PopInputComponent(FInputComponent* Component);

	// 뷰포트 클라이언트가 포커스를 가진 프레임마다 이번 프레임의 키 상태를 넘긴다.
	void InputKeyStates(const bool (&InKeyDown)[256]);
	// 포커스를 잃으면 모든 키를 뗀 것으로 본다. 누르고 있던 키는 다음 처리 때 Released가 나간다.
	void FlushPressedKeys();

	// UWorld::Tick이 액터 Tick 전에 부른다. 이번 프레임 키 상태로 바인딩을 호출한다.
	void ProcessPlayerInput();

	//PlayerCameraManager 생성
	void SpawnPlayerCameraManager();
	//PCM 업데이트
	void UpdatePlayerCameraManager(float DeltaTime);

	//FViewTargetTransitionParams의 값들
	void SetViewTargetWithBlend(AActor* NewViewTarget, float BlendTime,
		EViewTargetBlendFunction BlendFunc, float BlendExp, bool bLockOutgoing);
	
	APlayerCameraManager* GetCameraPlayerManager() const { return PlayerCameraManager.Get(); }
private:
	bool IsKeyDown(EKeyCode Key) const { return bKeyDown[static_cast<uint8>(Key)]; }
	bool WasKeyPressed(EKeyCode Key) const { return IsKeyDown(Key) && !bPrevKeyDown[static_cast<uint8>(Key)]; }
	bool WasKeyReleased(EKeyCode Key) const { return !IsKeyDown(Key) && bPrevKeyDown[static_cast<uint8>(Key)]; }
	bool WasKeyEvent(EKeyCode Key, EInputEvent Event) const;

	int32 PlayerIndex = 0;

	TArray<FInputAxisKeyMapping> AxisMappings;
	TArray<FInputActionKeyMapping> ActionMappings;

	// 소유하지 않는다. 각 액터가 자기 InputComponent를 가진다.
	TArray<FInputComponent*> InputStack;

	//플레이어 컨트롤러당 플레이어 카메라 매니저 하나를 가지고있음
	TWeakObjectPtr<APlayerCameraManager> PlayerCameraManager;
	UClass* PlayerCameraManagerClass = nullptr;


	// 뷰포트가 넘긴 이번 프레임 상태와, 지난 ProcessPlayerInput 때의 상태
	bool bKeyDown[256] = {};
	bool bPrevKeyDown[256] = {};
};
