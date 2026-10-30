#pragma once

#include "GameFramework/Actor.h"

class UDecalComponent;


class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)
	REFLECT_START(ADecalActor)
	REFLECT_END()

public:
	ADecalActor();

	UDecalComponent* DecalComponent;
	//virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	//virtual void Tick(float DeltaTime) {}
	//virtual void EndPlay();
};