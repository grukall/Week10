#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Containers/Array.h"
#include "Input/KeyCode.h"
#include <functional>

class AActor;
class APlayerController;

enum class EInputEvent : uint8 { Pressed, Released };

// 액터가 받을 입력과 그때 부를 함수를 담는다 (UE UInputComponent 축소판).
// AActor::EnableInput으로 PlayerController의 입력 스택에 올라가야 입력을 받는다.
// 이름(Axis·Action)을 어떤 키로 낼지는 PlayerController의 매핑이 정한다.
class FInputComponent
{
public:
	using FAxisCallback = std::function<void(float)>;
	using FActionCallback = std::function<void()>;

	explicit FInputComponent(AActor* InOwner) : Owner(InOwner) {}
	FInputComponent(const FInputComponent&) = delete;
	FInputComponent& operator=(const FInputComponent&) = delete;

	// 매 프레임 매핑된 키 값의 합으로 불린다. 아무 키도 안 누르면 0.
	void BindAxis(FName AxisName, FAxisCallback Callback);
	// 매핑된 키 중 하나가 눌린 프레임 / 뗀 프레임에 불린다.
	void BindAction(FName ActionName, EInputEvent Event, FActionCallback Callback);
	// 매핑 없이 키 하나에 바로 묶는다.
	void BindKey(EKeyCode Key, EInputEvent Event, FActionCallback Callback);
	void ClearBindings();

	AActor* GetOwner() const { return Owner; }
	APlayerController* GetController() const { return Controller; }

private:
	friend class APlayerController;

	struct FAxisBinding
	{
		FName AxisName;
		FAxisCallback Callback;
	};

	struct FActionBinding
	{
		FName ActionName;
		EInputEvent Event = EInputEvent::Pressed;
		FActionCallback Callback;
	};

	struct FKeyBinding
	{
		EKeyCode Key = EKeyCode::LButton;
		EInputEvent Event = EInputEvent::Pressed;
		FActionCallback Callback;
	};

	AActor* Owner = nullptr;
	// 입력 스택에 올라가 있는 동안만 값이 있다. PlayerController가 넣고 뺀다.
	APlayerController* Controller = nullptr;

	TArray<FAxisBinding> AxisBindings;
	TArray<FActionBinding> ActionBindings;
	TArray<FKeyBinding> KeyBindings;
};
