#include "EnginePCH.h"
#include "GameFramework/InputComponent.h"

void FInputComponent::BindAxis(FName AxisName, FAxisCallback Callback)
{
	AxisBindings.Add({ AxisName, std::move(Callback) });
}

void FInputComponent::BindAction(FName ActionName, EInputEvent Event, FActionCallback Callback)
{
	ActionBindings.Add({ ActionName, Event, std::move(Callback) });
}

void FInputComponent::BindKey(EKeyCode Key, EInputEvent Event, FActionCallback Callback)
{
	KeyBindings.Add({ Key, Event, std::move(Callback) });
}

void FInputComponent::ClearBindings()
{
	AxisBindings.Empty();
	ActionBindings.Empty();
	KeyBindings.Empty();
}
