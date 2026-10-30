#include "EnginePCH.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Script/LuaScriptInstance.h"

UActorComponent::UActorComponent()
{
	PrimaryComponentTick.Target = this;
}

UActorComponent::~UActorComponent()
{
    if (Owner)
    {
        Owner->RemoveOwnedComponent(this);
    }
}

void UActorComponent::DispatchBeginPlay()
{
	// 에디터에서 컴포넌트를 추가할 때도 BeginPlay가 불리므로(AActor::AddComponent),
	// 소유 액터가 플레이 중일 때만 스크립트를 로드한다.
	if (!ScriptInstance && Owner && Owner->HasActorBegunPlay())
	{
		ScriptInstance = FLuaScriptInstance::CreateAndLoad(this, ScriptPath);

		// 스크립트에 Tick이 있을 때만 켠다. 액터가 Tick 함수를 등록하기 전에 켜야 한다.
		if (ScriptInstance && ScriptInstance->HasFunction("Tick"))
			PrimaryComponentTick.bCanEverTick = true;

		// Coroutine은 World가 매 프레임 진행함
		if (ScriptInstance && Owner->GetWorld())
			Owner->GetWorld()->RegisterScriptInstance(ScriptInstance.get());
	}

	BeginPlay();

	if (ScriptInstance)
		ScriptInstance->Call("BeginPlay");
}

void UActorComponent::DispatchTickComponent(float DeltaTime)
{
	TickComponent(DeltaTime);

	if (ScriptInstance)
		ScriptInstance->Call("Tick", DeltaTime);
}

void UActorComponent::DispatchEndPlay()
{
	// 컴포넌트가 아직 살아 있을 때 스크립트가 정리할 기회를 준다.
	if (ScriptInstance)
		ScriptInstance->Call("EndPlay");

	EndPlay();

	// Script Instance 여기서 지우지 않는다. Actor::DispatchEndPlay() 수정과 같은 이유

	if (ScriptInstance)
	{
		ScriptInstance->StopCoroutines();
		if (Owner && Owner->GetWorld()) Owner->GetWorld()->UnregisterScriptInstance(ScriptInstance.get());
	}
}

void UActorComponent::SetActive(bool bNewActive)
{
    bIsActive = bNewActive;
}

void UActorComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* OwnerObj = Owner;
		Record << SA_VALUE("Owner", OwnerObj);
		Owner = static_cast<AActor*>(OwnerObj);
	}
}
