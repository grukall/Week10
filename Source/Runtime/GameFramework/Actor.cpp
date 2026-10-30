#include "EnginePCH.h"
#include "Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Component/SceneComponent.h"
#include "Component/ParticleSubUVComponent.h"
#include "Component/ExponentialHeightFogComponent.h"
#include "Component/LightComponentBase.h"
#include "Component/ShapeComponent.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"
#include "Script/LuaScriptInstance.h"
#include "Script/LuaReflection.h"
#include "Component/ShapeComponent.h"
#include "GameFramework/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "CameraTypes.h"

enum class EAttachmentRule;


AActor::AActor()
{
    PrimaryActorTick.Target = this;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot"));
}

AActor::~AActor()
{
    TArray<UActorComponent*> ToDelete = Components;
    Components.Reset();
    RootComponent = nullptr;

    for (UActorComponent* Component : ToDelete)
    {
        delete Component;
    }
}

void AActor::BeginPlay()
{
	//if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(RootComponent))
	//{
	//	World->AddPrimitive(Cast<UPrimitiveComponent>(RootComponent));
	//}

    for (UActorComponent* Component : Components)
    {
        Component->InitializeComponent();
    }

	for (UActorComponent* Component : Components)
	{
		Component->DispatchBeginPlay();
	}

	RegisterAllActorTickFunctions(true);
}

void AActor::EndPlay()
{
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Component->DispatchEndPlay();
	}
}

void AActor::DispatchBeginPlay()
{
	if (bActorHasBegunPlay)
		return;

	bActorHasBegunPlay = true;

	ScriptInstance = FLuaScriptInstance::CreateAndLoad(this, ScriptPath);
    
    // 스크립트에 Tick이 있을 때만 켠다. 원래 Tick하는 클래스라면 이미 켜져 있으므로 끄지는 않는다.
	// BeginPlay가 Tick 함수를 등록하기 전에 켜야 한다.
	if (ScriptInstance && ScriptInstance->HasFunction("Tick"))
		PrimaryActorTick.bCanEverTick = true;

	// Coroutine은 World가 매 프레임 진행함
	if (ScriptInstance && World)
        World->RegisterScriptInstance(ScriptInstance.get());

	BeginPlay();

	// 스크립트 BeginPlay보다 먼저 구독해서, BeginPlay 안에서 생긴 겹침도 받는다
	BindScriptEvents();

	if (ScriptInstance)
		ScriptInstance->Call("BeginPlay");
}

void AActor::TickActor(float DeltaTime)
{
	Tick(DeltaTime);

	if (ScriptInstance)
		ScriptInstance->Call("Tick", DeltaTime);
}

FInputComponent* AActor::EnableInput(APlayerController* PlayerController)
{
	if (!PlayerController)
		return nullptr;

	if (!InputComponent)
		InputComponent = MakeUnique<FInputComponent>(this);

	PlayerController->PushInputComponent(InputComponent.get());
	return InputComponent.get();
}

void AActor::DisableInput()
{
	if (!InputComponent)
		return;

	if (APlayerController* Controller = InputComponent->GetController())
		Controller->PopInputComponent(InputComponent.get());
}

void AActor::DispatchEndPlay()
{
	if (!bActorHasBegunPlay)
		return;

	// EndPlay 안에서 다시 Destroy가 불려도 두 번 들어오지 않도록 먼저 내린다.
	bActorHasBegunPlay = false;

	if (ScriptInstance)
		ScriptInstance->Call("EndPlay");

	EndPlay();

	DisableInput();
	// Broadcast 도중(겹침 콜백 안에서 Destroy)이어도 Delegate가 해제를 미뤄 처리한다
	UnbindScriptEvents();

    // Instance의 Lua 함수 (Tick, Coroutine) 에서 Destroy가 불렸을 수도 있으므로
    // Script Instance 는 여기서 지우지 않는다. 어차피 Actor Member 여서 Frame 끝에서 delete 될 때 함께 delete 됨.

    // 남은 코루틴을 멈추고 World 진행 목록에서 지우기
	if (ScriptInstance)
	{
		ScriptInstance->StopCoroutines();
		if (World) World->UnregisterScriptInstance(ScriptInstance.get());
	}

}

void AActor::RegisterAllActorTickFunctions(bool bRegister)
{
	if (bRegister && !World)
		return;

	// bCanEverTick이 꺼진 함수는 등록하지 않으므로 정적 메시 액터는 매 프레임 순회 대상에서 빠진다.
	auto Apply = [&](FTickFunction& Function)
	{
		if (bRegister)
			Function.RegisterTickFunction(World->GetTickTaskManager());
		else
			Function.UnRegisterTickFunction();
	};

	Apply(PrimaryActorTick);
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Apply(Component->PrimaryComponentTick);
	}
}

void AActor::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);   // "Properties"

    FArchive& Ar = Record.GetUnderlyingArchive();

    if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
    {
        int32 Num = Components.Num();
        FStructuredArchive::FArray ActorArray = Record.EnterArray("Components", Num);
        if (Ar.IsLoading())
            Components.SetNum(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            UObject* Obj = Components[i];
            ActorArray.EnterElement() << Obj;
            Components[i] = static_cast<UActorComponent*>(Obj);
        }

        UObject* WorldObj = World;
        UObject* LevelObj = Level;
        UObject* RootObj = RootComponent;
        Record << SA_VALUE("OwningWorld", WorldObj);
        Record << SA_VALUE("OwningLevel", LevelObj);
        Record << SA_VALUE("RootComponent", RootObj);
        World = static_cast<UWorld*>(WorldObj);
        Level = static_cast<ULevel*>(LevelObj);
        RootComponent = static_cast<USceneComponent*>(RootObj);
        return;
    }

    TArray<UActorComponent*> SavedComponents;
    if (Ar.IsSaving())
    {
        for (UActorComponent* Component : Components)
        {
            if (Component)
            {
                SavedComponents.Add(Component);
            }

        }
    }

	struct FPendingAttachment
	{
		USceneComponent* Child;
		FString ParentName;
	};
	TArray<FPendingAttachment> PendingAttachments;

    int32 Num = SavedComponents.Num();
    FStructuredArchive::FArray ComponentArray = Record.EnterArray("Components", Num);
    for (int32 i = 0; i < Num; ++i)
    {
        FStructuredArchive::FRecord ComponentRecord = ComponentArray.EnterElement().EnterRecord();
        FString ParentName;

        if (Ar.IsSaving())
        {
            UActorComponent* Component = SavedComponents[i];
            FString Name = Component->GetName();
            FString ClassName = Component->GetClass()->Name;
            if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
            {
                ParentName = SceneComponent->GetAttachParent() ? SceneComponent->GetAttachParent()->GetName() : "";
            }
            ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);
            Component->Serialize(ComponentRecord);
            continue;
        }

        FString Name, ClassName;
        ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);

        UActorComponent* Component = FindComponentByName(FName(Name));
        if (!Component)
        {
            UClass* ComponentClass = FindClass(ClassName);

            if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
            {
                HTR_LOG(Warning, "Load: unknown component class '{}'", ClassName);
                continue;
            }

            Component = AddComponent(ComponentClass, FName(Name));
        }

        if (!Component || Component->GetClass()->Name != ClassName)
        {
            HTR_LOG(Warning, "Load: component {} ({}) failed", Name, ClassName);
            continue;
        }

        Component->Serialize(ComponentRecord);
		if(USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			if (!ParentName.empty())
			{
				PendingAttachments.Add({ SceneComponent, ParentName });
			}
		}
    }

	for (const FPendingAttachment& Attachment : PendingAttachments)
	{
		USceneComponent* ParentComponent = Cast<USceneComponent>(FindComponentByName(FName(Attachment.ParentName)));
		if (ParentComponent)
		{
			Attachment.Child->SetupAttachment(ParentComponent);
		}
	}
}


void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
    for (uint32 i = 0; i < Components.Num(); ++i)
    {
        if (Components[i] == Component)
        {
            Components.RemoveAt(i, 1);
            break;
        }
    }

    if (RootComponent == Component)
    {
        RootComponent = nullptr;
    }
}

FVector AActor::GetActorLocation() const
{
    if (RootComponent)
    {
        return RootComponent->GetWorldLocation();
    }
    return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
    if (RootComponent)
    {
        return RootComponent->GetWorldRotation();
    }
    return FRotator(0, 0, 0);
}

FVector AActor::GetActorScale3D() const
{
    if (RootComponent)
    {
        return RootComponent->GetWorldScale3D();
    }
    return FVector::OneVector;
}

//FQuat AActor::GetActorQuat() const
//{
//    if (RootComponent)
//    {
//        return FQuat(RootComponent->GetWorldRotation());
//    }
//    return FQuat::Identity;
//}

FTransform AActor::GetActorTransform() const
{
    if (RootComponent)
    {
        return FTransform(
            RootComponent->GetWorldRotation(),
            RootComponent->GetWorldLocation(),
            RootComponent->GetWorldScale3D()
        );

        // return FTransform(RootComponent->GetWorldMatrix());
    }
    return FTransform::Identity;
}

bool AActor::Destroy()
{
    if (!World)
        return false;

    return World->DestroyActor(this);
}

namespace
{
    // 스크립트에는 원시 포인터 대신 약한 참조 핸들로 넘긴다 (LuaReflection::MakeObject와 같다)
    FLuaObjectRef ToLua(UObject* Object)
    {
        return FLuaObjectRef{ FWeakObjectPtr(Object) };
    }
}

bool AActor::AddActorWorldOffset(const FVector& Delta, bool bSweep)
{
    if (!RootComponent)
        return false;

    FHitResult BestHit;
    BestHit.Init(RootComponent->GetWorldLocation(), RootComponent->GetWorldLocation() + Delta);
    UPrimitiveComponent* BestComponent = nullptr;

    if (bSweep && World && Delta.Size() > KINDA_SMALL_NUMBER)
    {
        // 막는 Shape마다 같은 Delta로 검사해 가장 먼저 막히는 것을 고른다
        for (UActorComponent* Component : Components)
        {
            UShapeComponent* Shape = Cast<UShapeComponent>(Component);
            if (!Shape || !Shape->bHasBegunPlay || !Shape->IsBlockComponent())
                continue;

            FHitResult Hit;
            Hit.Init(Shape->GetWorldLocation(), Shape->GetWorldLocation() + Delta);
            if (Shape->SweepBlocking(Shape, World, Delta, Hit) && Hit.Time < BestHit.Time)
            {
                BestHit = Hit;
                BestComponent = Shape;
            }
        }

        // 멈출 곳이 정해졌으니 거기까지 가는 길에 스친 겹침 상대를 Shape마다 모은다. 옮기기 전에 해야 한다.
        for (UActorComponent* Component : Components)
        {
            UShapeComponent* Shape = Cast<UShapeComponent>(Component);
            if (Shape && Shape->bHasBegunPlay && Shape->bGenerateOverlapEvents)
                Shape->GatherSweepOverlaps(Shape, World, Delta * BestHit.Time);
        }
    }

    RootComponent->SetWorldLocation(RootComponent->GetWorldLocation() + Delta * BestHit.Time, false);

    if (BestComponent)
        BestComponent->DispatchBlockingHit(BestHit);
    return BestComponent != nullptr;
}

void AActor::NotifyActorBeginOverlap(AActor* OtherActor)
{
    HTR_LOG(Info, "ActorBeginOverlap: {} <-> {}", GetName(), OtherActor->GetName());
    OnActorBeginOverlap.Broadcast(this, OtherActor);
}

void AActor::NotifyActorEndOverlap(AActor* OtherActor)
{
    HTR_LOG(Info, "ActorEndOverlap: {} <-> {}", GetName(), OtherActor->GetName());
    OnActorEndOverlap.Broadcast(this, OtherActor);
}

void AActor::NotifyHit(UPrimitiveComponent* MyComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit)
{
    HTR_LOG(Info, "Hit: {} -> {} (Time {:.3f}, StartPenetrating {})", MyComp->GetName(), OtherComp->GetName(), Hit.Time, Hit.bStartPentrating);
    OnActorHit.Broadcast(this, OtherActor, Hit);
}

void AActor::BindScriptEvents()
{
    if (!ScriptInstance)
        return;

    // 핸들러는 부를 때마다 ScriptInstance를 다시 확인한다. 등록은 Owner = this라 UnbindScriptEvents가 한 번에 끊는다.
    if (ScriptInstance->HasFunction("OnOverlap"))
    {
        OnActorBeginOverlap.Add([this](AActor*, AActor* OtherActor)
            {
                if (ScriptInstance)
                    ScriptInstance->Call("OnOverlap", ToLua(OtherActor));
            }, this);
    }

    if (ScriptInstance->HasFunction("OnEndOverlap"))
    {
        OnActorEndOverlap.Add([this](AActor*, AActor* OtherActor)
            {
                if (ScriptInstance)
                    ScriptInstance->Call("OnEndOverlap", ToLua(OtherActor));
            }, this);
    }

    if (ScriptInstance->HasFunction("OnHit"))
    {
        OnActorHit.Add([this](AActor*, AActor* OtherActor, const FHitResult&)
            {
                if (ScriptInstance)
                    ScriptInstance->Call("OnHit", ToLua(OtherActor));
            }, this);
    }

    // 컴포넌트 겹침은 컴포넌트마다 명단이 따로 있어서, 내 PrimitiveComponent 전부에 구독한다
    if (ScriptInstance->HasFunction("OnComponentOverlap"))
    {
        for (UActorComponent* Component : Components)
        {
            UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
            if (!Primitive)
                continue;

            Primitive->OnComponentBeginOverlap.Add([this](UPrimitiveComponent* MyComp, AActor* OtherActor, UPrimitiveComponent* OtherComp)
                {
                    if (ScriptInstance)
                        ScriptInstance->Call("OnComponentOverlap", ToLua(MyComp), ToLua(OtherActor), ToLua(OtherComp));
                }, this);
        }
    }
}

void AActor::UnbindScriptEvents()
{
    OnActorBeginOverlap.RemoveAll(this);
    OnActorEndOverlap.RemoveAll(this);
    OnActorHit.RemoveAll(this);

    for (UActorComponent* Component : Components)
    {
        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            Primitive->OnComponentBeginOverlap.RemoveAll(this);
    }
}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
    for (UActorComponent* OwnedComp : Components)
    {
        if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(OwnedComp))
        {
            if (PrimComp->GetOverlapInfos().Num() > 0 && PrimComp->IsOverlappingActor(Other))
                return true;
        }
    }
    return false;
}

UActorComponent* AActor::AddComponent(UClass* ComponentClass, FName Name)
{
    if (!ComponentClass 
        || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return nullptr;
    }


    if (FName(Name) == FName("None"))
    {
        Name = MakeUniqueObjectName(this, ComponentClass);
    }

    UActorComponent* NewComponent = NewObject<UActorComponent>(this, ComponentClass, Name);
    if (!NewComponent)
    {
        return nullptr;
    }

	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(NewComponent))
	{
        World->GetScene().AddPrimitive(Primitive);
	}
    
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent))
	{
		SceneComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);        
	}

    NewComponent->SetOwner(this);    
    Components.Add(NewComponent);
    NewComponent->InitializeComponent();

    //if (UParticleSubUVComponent* ParticleSubUV = Cast<UParticleSubUVComponent>(NewComponent))
    //{
    //    ParticleSubUV->BeginPlay();
    //    RegisterAllActorTickFunctions(true);
    //}

    if (World)
    {
        NewComponent->DispatchBeginPlay();
        RegisterAllActorTickFunctions(true);
    }

	return NewComponent;
}

void AActor::DestroyComponent(UActorComponent* Component)
{ 
    if (!Component || Component->IsPendingKill())
        return;

    if (Component == RootComponent)
    {
        HTR_LOG(Warning, "DefaultSceneRoot cannot be deleted.");
        return;
    }

    if (bActorHasBegunPlay)
    {
        Component->DispatchEndPlay();
    }

    if (USceneComponent* Parent = Cast<USceneComponent>(Component))
    {
        TArray<USceneComponent*> Children = Parent->GetAttachChildren();
        for (USceneComponent* Candidate : Children)
        {
            if (!Candidate) { continue; }

            Candidate->DetachFromParent(EAttachmentRule::KeepWorld);
            Candidate->SetupAttachment(Parent->GetAttachParent(), EAttachmentRule::KeepWorld);
        }
    }

    if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
    {
        Primitive->ClearComponentOverlaps();
        if (UShapeComponent* Shape = Cast<UShapeComponent>(Primitive))
            World->RemoveShape(Shape);
        World->GetScene().RemovePrimitive(Primitive);
    }
    else if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
    {
        World->GetScene().RemoveFogInfo(Fog->GetUUID());
    }
    else if (ULightComponentBase* Light = Cast<ULightComponentBase>(Component))
    {
        World->GetScene().RemoveLight(Light);
    }
    RemoveOwnedComponent(Component);

    // delete Component;
    // 바로 delete 하지 않고, 소멸자가 하던 Tick 등록 해제 지금 하기
    Component->PrimaryComponentTick.UnRegisterTickFunction();

	if (World) World->DeferDelete(Component);
	else delete Component;
    
    return;
}

void AActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV)
{
    //루프를 돌면서 카메라 컴포넌트가 존재하는지
    for (UActorComponent* Component : Components)
    {
        UCameraComponent* Camera = Cast<UCameraComponent>(Component);
        if (!Camera)
            continue;

        //DeltaTime에 맞춰서 Location이나 Rotation을 조종하고싶을때가 있을수도 있어서 DeltaTime을 넘김
        Camera->GetCameraView(DeltaTime, OutPOV);
        return;                                 
    }

    //없으면 액터의 위치 회전을 씀
    OutPOV.Location = GetActorLocation();
    OutPOV.Rotation = GetActorRotation();

}

void AActor::OnDefaultSubobjectCreated(UObject* Subobject)
{
    if (UActorComponent* Component = Cast<UActorComponent>(Subobject))
    {
        Component->SetOwner(this);
        Components.Add(Component);
    }
}

UActorComponent* AActor::FindComponentByName(FName Name) const
{
    for (UActorComponent* Component : Components)
        if (Component && Component->GetFName() == Name)
            return Component;
    return nullptr;
}

bool AActor::TryGetActorBounds(FBox& OutBounds) const
{
    bool bHasBounds = false;

    for (UActorComponent* Component : Components)
    {
        UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
        if (!Primitive)
            continue;

        FBox PrimitiveBounds = Primitive->CalcBounds();
        if (bHasBounds)
        {
            OutBounds.Expand(PrimitiveBounds);
        }
        else
        {
            OutBounds = PrimitiveBounds;
            bHasBounds = true;
        }
    }

    return bHasBounds;
}
