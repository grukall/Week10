#pragma once

#include "Math/Transform.h"

enum class EGizmoMode
{
	Location, 
	Rotation, 
	Scale,
};

enum class EGizmoSpace
{
	Local,
	World
};

struct FGizmoTransformDelta
{
	EGizmoMode Mode = EGizmoMode::Location;
	EGizmoSpace Space = EGizmoSpace::Local;
	FVector TranslationDelta = FVector::ZeroVector;
	FQuat RotationDelta = FQuat::Identity();
	FVector ScaleFactor = FVector::OneVector;
};