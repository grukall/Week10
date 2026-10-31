#pragma once

#include "Editor/Gizmo/TransformGizmoTarget.h"

class FSkeletalMeshViewerPanel;

class FBoneGizmoTarget final : public ITransformGizmoTarget
{
public:
	explicit FBoneGizmoTarget(FSkeletalMeshViewerPanel* InViewer = nullptr);

	void SetViewer(FSkeletalMeshViewerPanel* InViewer);
	bool IsValid() const override;
	FMatrix GetWorldMatrix() const override;

	void BeginTransformEdit() override;
	void ApplyTransformDelta(const FGizmoTransformDelta& Delta) override;
	void EndTransformEdit() override;

private:
	int32 GetTargetBoneIndex() const;

	FSkeletalMeshViewerPanel* Viewer = nullptr;
	int32 EditingBoneIndex = -1;
	int32 ParentBoneIndex = -1;

	FTransform StartLocalTransform = FTransform::Identity;
	FMatrix StartWorldMatrix = FMatrix::Identity;
	FMatrix ParentWorldMatrix = FMatrix::Identity;
	FQuat StartWorldRotation = FQuat::Identity();
	FQuat ParentWorldRotation = FQuat::Identity();

	bool bEditing = false;
};