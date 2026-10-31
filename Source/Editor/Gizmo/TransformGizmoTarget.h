#pragma once

#include "GizmoTypes.h"

class ITransformGizmoTarget
{
public:
	virtual ~ITransformGizmoTarget() = default;

	virtual bool IsValid() const = 0;
	virtual FMatrix GetWorldMatrix() const = 0;
	virtual void BeginTransformEdit() = 0;
	virtual void ApplyTransformDelta(const FGizmoTransformDelta& Delta) = 0;
	virtual void EndTransformEdit() = 0;
};