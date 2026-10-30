#pragma once

#include "GameFramework/Actor.h"

class UDecalComponent;
class UBillboardComponent;


class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)
	REFLECT_START(ADecalActor)
	REFLECT_END()

public:
	ADecalActor();

	UDecalComponent* DecalComponent = nullptr;
	// 데칼은 Primitive가 아니라 뷰포트에서 클릭할 수 없다. 라이트 액터처럼 아이콘으로 선택한다.
	UBillboardComponent* BillboardComponent = nullptr;
	//virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	//virtual void Tick(float DeltaTime) {}
	//virtual void EndPlay();
};