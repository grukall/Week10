#pragma once

#include "Editor/Gizmo/TransformGizmoTarget.h"

class USceneComponent;

class FSceneComponentGizmoTarget final : public ITransformGizmoTarget
{
public:
	explicit FSceneComponentGizmoTarget(USceneComponent* InComponent = nullptr);

	void SetComponent(USceneComponent* InComponent);
	USceneComponent* GetComponent() const { return Component; }

	bool IsValid() const override;
	FMatrix GetWorldMatrix() const override;
	void BeginTransformEdit() override;
	void ApplyTransformDelta(const FGizmoTransformDelta& Delta) override;
	void EndTransformEdit() override;

private:
	USceneComponent* Component = nullptr;

	FTransform StartLocalTransform = FTransform::Identity;
	FVector StartWorldLocation = FVector::ZeroVector;
	FQuat StartWorldRotation = FQuat::Identity();

	bool bEditing = false;
};