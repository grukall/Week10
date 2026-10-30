#pragma once

#include "GameFramework/Actor.h"
#include "Component/StaticMeshComponent.h"

class AStaticMeshActor :public AActor
{
	DECLARE_CLASS(AStaticMeshActor, AActor)
	// SetPrimitiveType은 enum 매개변수라 등록할 수 없다
	REFLECT_START(ClassName)
		FUNCTION(GetStaticMeshComponent)
	REFLECT_END()
public:
	AStaticMeshActor();
	virtual ~AStaticMeshActor() = default;

	virtual void BeginPlay() override;

	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	void SetPrimitiveType(EPrimitiveType Type);

private:
	UStaticMeshComponent* StaticMeshComponent;
};

