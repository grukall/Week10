#pragma once

#include "GameFramework/Actor.h"
#include "Component/BillboardComponent.h"
#include "Component/DirectionalLightComponent.h"

class ADirectionalLightActor final : public AActor
{
	DECLARE_CLASS(ADirectionalLightActor, AActor)
	REFLECT_START(ClassName)
		FUNCTION(GetLightComponent)
	REFLECT_END()

public:
	ADirectionalLightActor();
	virtual ~ADirectionalLightActor() override = default;

	UDirectionalLightComponent* GetLightComponent() const { return LightComponent; }

private:
	UDirectionalLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
