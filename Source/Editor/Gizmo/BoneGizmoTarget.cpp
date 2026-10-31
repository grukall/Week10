#include "EnginePCH.h"
#include "BoneGizmoTarget.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"

namespace
{
	FQuat ExtractRotation(const FMatrix& Matrix)
	{
		FMatrix RotationMatrix = Matrix;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FVector UnitAxis(RotationMatrix[Axis][0], RotationMatrix[Axis][1], RotationMatrix[Axis][2]);
			UnitAxis = UnitAxis.GetSafeNormal();

			RotationMatrix[Axis][0] = UnitAxis.X;
			RotationMatrix[Axis][1] = UnitAxis.Y;
			RotationMatrix[Axis][2] = UnitAxis.Z;
		}
		return MatrixToRotator(RotationMatrix).Quaternion().Normalized();
	}
}

FBoneGizmoTarget::FBoneGizmoTarget(FSkeletalMeshViewerPanel* InViewer)
	: Viewer(InViewer)
{
}

void FBoneGizmoTarget::SetViewer(FSkeletalMeshViewerPanel* InViewer)
{
	Viewer = InViewer;
	EditingBoneIndex = -1;
	ParentBoneIndex = -1;
	bEditing = false;
}

int32 FBoneGizmoTarget::GetTargetBoneIndex() const
{
	if (!Viewer)
	{
		return -1;
	}
	return bEditing 
		? EditingBoneIndex 
		: Viewer->GetSelectedBoneIndex();
}

bool FBoneGizmoTarget::IsValid() const
{
	const int32 BoneIndex = GetTargetBoneIndex();
	return Viewer && Viewer->IsBoneValid(BoneIndex);
}

FMatrix FBoneGizmoTarget::GetWorldMatrix() const
{
	const int32 BoneIndex = GetTargetBoneIndex();
	return Viewer->GetBoneGlobalMatrix(BoneIndex);
}

void FBoneGizmoTarget::BeginTransformEdit()
{
	if (!Viewer)
	{
		return;
	}

	EditingBoneIndex = Viewer->GetSelectedBoneIndex();
	if (!Viewer->IsBoneValid(EditingBoneIndex))
	{
		EditingBoneIndex = -1;
		return;
	}

	StartWorldMatrix = Viewer->GetBoneGlobalMatrix(EditingBoneIndex);
	StartLocalTransform = Viewer->GetBoneLocalTransform(EditingBoneIndex);
	StartWorldRotation = ExtractRotation(StartWorldMatrix);

	ParentBoneIndex = Viewer->GetBoneParentIndex(EditingBoneIndex);

	if (Viewer->IsBoneValid(ParentBoneIndex))
	{
		ParentWorldMatrix = Viewer->GetBoneGlobalMatrix(ParentBoneIndex);
		ParentWorldRotation = ExtractRotation(ParentWorldMatrix);
	}
	else
	{
		ParentBoneIndex = -1;
		ParentWorldMatrix = FMatrix::Identity;
		ParentWorldRotation = FQuat::Identity();
	}
	bEditing = true;
}

void FBoneGizmoTarget::ApplyTransformDelta(const FGizmoTransformDelta& Delta)
{
	if (!Viewer || !bEditing || !Viewer->IsBoneValid(EditingBoneIndex))
	{
		return;
	}

	FTransform NewLocalTransform = StartLocalTransform;
	switch (Delta.Mode)
	{
	case EGizmoMode::Location:
	{
		const FVector StartWorldLocation(StartWorldMatrix[3][0], StartWorldMatrix[3][1], StartWorldMatrix[3][2]);
		const FVector NewWorldLocation = StartWorldLocation + Delta.TranslationDelta;

		NewLocalTransform.Location = ParentBoneIndex >= 0
									? ParentWorldMatrix.Inverse().TransformPosition(NewWorldLocation)
									: NewWorldLocation;
		break;
	}
	case EGizmoMode::Rotation:
	{
		const FQuat NewWorldRotation = (Delta.RotationDelta * StartWorldRotation).Normalized();
		const FQuat NewLocalRotation = ParentBoneIndex >= 0 
									? ParentWorldRotation.Inverse() * NewWorldRotation 
									: NewWorldRotation;
		NewLocalTransform.Rotation = NewLocalRotation.Normalized().ToFRotator();
		break;
	}
	case EGizmoMode::Scale:
	{
		NewLocalTransform.Scale = StartLocalTransform.Scale * Delta.ScaleFactor;
		break;
	}
	}
	Viewer->SetBoneLocalTransform(EditingBoneIndex, NewLocalTransform);
}

void FBoneGizmoTarget::EndTransformEdit()
{
	bEditing = false;
	EditingBoneIndex = -1;
	ParentBoneIndex = -1;
}