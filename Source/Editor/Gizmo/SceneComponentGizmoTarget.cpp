#include "EnginePCH.h"
#include "SceneComponentGizmoTarget.h"
#include "Component/SceneComponent.h"

FSceneComponentGizmoTarget::FSceneComponentGizmoTarget(USceneComponent* InComponent)
	: Component(InComponent)
{
}

void FSceneComponentGizmoTarget::SetComponent(USceneComponent* InComponent)
{
	Component = InComponent;
	bEditing = false;
}

bool FSceneComponentGizmoTarget::IsValid() const
{
	return Component != nullptr;
}

FMatrix FSceneComponentGizmoTarget::GetWorldMatrix() const
{
	return Component ? Component->GetWorldMatrix() : FMatrix::Identity;
}

void FSceneComponentGizmoTarget::BeginTransformEdit()
{
	if (!Component)
	{
		return;
	}

	StartLocalTransform = Component->GetTransform();
	StartWorldLocation = Component->GetWorldLocation();
	StartWorldRotation = Component->GetWorldRotation().Quaternion();
	bEditing = true;
}

void FSceneComponentGizmoTarget::ApplyTransformDelta(const FGizmoTransformDelta& Delta)
{
	if (!Component || !bEditing)
	{
		return;
	}

	FQuat NewWorldRotation = FQuat::Identity();

	switch (Delta.Mode)
	{
	case EGizmoMode::Location:
		Component->SetWorldLocation(StartWorldLocation + Delta.TranslationDelta, false);
		break;
	case EGizmoMode::Rotation:
		NewWorldRotation = (Delta.RotationDelta * StartWorldRotation).Normalized();
		Component->SetWorldRotation(NewWorldRotation.ToFRotator(), false);
		break;
	case EGizmoMode::Scale:
		Component->SetRelativeScale3D(StartLocalTransform.Scale * Delta.ScaleFactor);
		break;
	}
}
void FSceneComponentGizmoTarget::EndTransformEdit()
{
	bEditing = false;
}
