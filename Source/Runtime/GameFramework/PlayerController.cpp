#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Input/InputSystem.h"
#include <cstring>

void APlayerController::EndPlay()
{
    if (APlayerCameraManager* Manager = PlayerCameraManager.Get())
        Manager->Destroy();
    PlayerCameraManager.Reset();
	// 아직 스택에 남은 InputComponent가 사라진 컨트롤러를 가리키지 않게 끊는다
	for (FInputComponent* Component : InputStack)
		Component->Controller = nullptr;
	InputStack.Empty();

	if (World)
		World->RemovePlayerController(this);

	Super::EndPlay();
}

bool APlayerController::AddAxisMapping(FName AxisName, const FString& Key, float Scale)
{
	EKeyCode KeyCode;
	if (!FInputSystem::FindKeyByName(Key, KeyCode))
	{
		HTR_LOG(Error, "[PlayerController] AddAxisMapping : 알 수 없는 키 이름 '{}'", Key);
		return false;
	}

	for (const FInputAxisKeyMapping& Mapping : AxisMappings)
	{
		if (Mapping.AxisName == AxisName && Mapping.Key == KeyCode && Mapping.Scale == Scale)
			return true;
	}
	AxisMappings.Add({ AxisName, KeyCode, Scale });
	return true;
}

bool APlayerController::AddActionMapping(FName ActionName, const FString& Key)
{
	EKeyCode KeyCode;
	if (!FInputSystem::FindKeyByName(Key, KeyCode))
	{
		HTR_LOG(Error, "[PlayerController] AddActionMapping : 알 수 없는 키 이름 '{}'", Key);
		return false;
	}

	for (const FInputActionKeyMapping& Mapping : ActionMappings)
	{
		if (Mapping.ActionName == ActionName && Mapping.Key == KeyCode)
			return true;
	}
	ActionMappings.Add({ ActionName, KeyCode });
	return true;
}

void APlayerController::ClearMappings()
{
	AxisMappings.Empty();
	ActionMappings.Empty();
}

void APlayerController::PushInputComponent(FInputComponent* Component)
{
	if (!Component || Component->Controller == this)
		return;

	// 다른 컨트롤러에 올라가 있었으면 거기서 먼저 내린다
	if (Component->Controller)
		Component->Controller->PopInputComponent(Component);

	InputStack.Add(Component);
	Component->Controller = this;
}

void APlayerController::PopInputComponent(FInputComponent* Component)
{
	const int32 Index = InputStack.Find(Component);
	if (Index == INDEX_NONE)
		return;

	InputStack.RemoveAt(static_cast<uint32>(Index));
	Component->Controller = nullptr;
}

void APlayerController::InputKeyStates(const bool (&InKeyDown)[256])
{
	std::memcpy(bKeyDown, InKeyDown, sizeof(bKeyDown));
}

void APlayerController::FlushPressedKeys()
{
	std::memset(bKeyDown, 0, sizeof(bKeyDown));
}

bool APlayerController::WasKeyEvent(EKeyCode Key, EInputEvent Event) const
{
	return Event == EInputEvent::Pressed ? WasKeyPressed(Key) : WasKeyReleased(Key);
}

void APlayerController::ProcessPlayerInput()
{
	// 바인딩 안에서 EnableInput·DisableInput·Destroy가 불려 스택이 바뀔 수 있어 사본을 돈다.
	// 내려간 컴포넌트는 Controller가 비므로 건너뛴다. 컴포넌트는 액터와 함께 Frame 끝에서 지워지므로 이번 순회 동안은 살아 있다.
	const TArray<FInputComponent*> Stack = InputStack;
	for (int32 StackIndex = Stack.Num() - 1; StackIndex >= 0; --StackIndex)
	{
		FInputComponent* Component = Stack[StackIndex];

		// 바인딩 안에서 새 바인딩을 추가하면 배열이 다시 할당될 수 있으므로 매번 크기를 다시 읽고, 함수는 복사해서 부른다
		for (int32 i = 0; i < Component->KeyBindings.Num() && Component->Controller == this; ++i)
		{
			if (!WasKeyEvent(Component->KeyBindings[i].Key, Component->KeyBindings[i].Event))
				continue;
			const FInputComponent::FActionCallback Callback = Component->KeyBindings[i].Callback;
			Callback();
		}

		for (int32 i = 0; i < Component->ActionBindings.Num() && Component->Controller == this; ++i)
		{
			const FName ActionName = Component->ActionBindings[i].ActionName;
			const EInputEvent Event = Component->ActionBindings[i].Event;

			// 같은 Action에 키가 여럿 매핑돼 있어도 한 프레임에 한 번만 부른다
			bool bTriggered = false;
			for (const FInputActionKeyMapping& Mapping : ActionMappings)
			{
				if (Mapping.ActionName == ActionName && WasKeyEvent(Mapping.Key, Event))
				{
					bTriggered = true;
					break;
				}
			}
			if (!bTriggered)
				continue;

			const FInputComponent::FActionCallback Callback = Component->ActionBindings[i].Callback;
			Callback();
		}

		// Axis는 키를 안 눌러도 매 프레임 0으로 불러서, 스크립트가 값을 따로 지우지 않아도 되게 한다
		for (int32 i = 0; i < Component->AxisBindings.Num() && Component->Controller == this; ++i)
		{
			const FName AxisName = Component->AxisBindings[i].AxisName;

			float Value = 0.0f;
			for (const FInputAxisKeyMapping& Mapping : AxisMappings)
			{
				if (Mapping.AxisName == AxisName && IsKeyDown(Mapping.Key))
					Value += Mapping.Scale;
			}

			const FInputComponent::FAxisCallback Callback = Component->AxisBindings[i].Callback;
			Callback(Value);
		}
	}

	std::memcpy(bPrevKeyDown, bKeyDown, sizeof(bPrevKeyDown));
}

void APlayerController::SpawnPlayerCameraManager()
{
    UWorld* World = GetWorld();
    if (!World || PlayerCameraManager.IsValid())
        return;
    UClass* Class = PlayerCameraManagerClass ? PlayerCameraManagerClass : APlayerCameraManager::StaticClass();
    APlayerCameraManager* Manager = Cast<APlayerCameraManager>(World->SpawnActor(Class));
    if (!Manager)
        return;
    PlayerCameraManager = Manager;
    Manager->SetFlags(EObjectFlags::RF_Transient);
    Manager->InitializeFor(this);
}

void APlayerController::UpdatePlayerCameraManager(float DeltaTime)
{
    if (APlayerCameraManager* Manager = PlayerCameraManager.Get())
        Manager->UpdateCamera(DeltaTime);
}

void APlayerController::SetViewTargetWithBlend(AActor* NewViewTarget, float BlendTime, EViewTargetBlendFunction BlendFunc, float BlendExp, bool bLockOutgoing)
{
	if (!PlayerCameraManager)
		return;

	FViewTargetTransitionParams TransitionParams;
	TransitionParams.BlendTime = BlendTime;
	TransitionParams.BlendFunction = BlendFunc;
	TransitionParams.BlendExp = BlendExp;
	TransitionParams.bLockOutgoing = bLockOutgoing;
	PlayerCameraManager->SetViewTarget(NewViewTarget, TransitionParams);
}
