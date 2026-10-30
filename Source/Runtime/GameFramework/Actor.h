#pragma once

#include "UObject/Object.h"
#include "Component/PrimitiveComponent.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"

class UWorld;
class ULevel;
class FLuaScriptInstance;
class FInputComponent;
class APlayerController;
struct FMinimalViewInfo;

class AActor : public UObject
{
	DECLARE_CLASS(AActor, UObject)
	REFLECT_START(ClassName)
		PROPERTY_FLAGS(ScriptPath, CPF_EditAnywhere | CPF_LuaScriptPath)
		PROPERTY_FLAGS(BindingTag, CPF_EditAnywhere)
		FUNCTION(GetRootComponent)
		FUNCTION(FindComponentByName)
		FUNCTION(GetActorLocation)
		FUNCTION(GetActorRotation)
		FUNCTION(GetActorScale3D)
		FUNCTION(GetActorTransform)
		FUNCTION(HasActorBegunPlay)
		FUNCTION(Destroy)
		FUNCTION(DestroyComponent)
		FUNCTION(AddActorWorldOffset)
		REFLECT_END()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	virtual void Tick(float DeltaTime) {}
	virtual void EndPlay();
	void TickActor(float DeltaTime);

	// World가 호출하는 진입점. BeginPlay를 받은 액터만 EndPlay를 받도록 짝을 맞춘다.
	void DispatchBeginPlay();
	void DispatchEndPlay();
	bool HasActorBegunPlay() const { return bActorHasBegunPlay; }

	// 비어 있으면 스크립트 없음. 씬 저장과 PIE 복제는 리플렉션(PROPERTY)이 처리한다.
	FString ScriptPath;
	FLuaScriptInstance* GetScriptInstance() const { return ScriptInstance.get(); }

	// 레벨 시퀀스가 이 액터를 찾을 때 쓰는 이름표 (UE의 Binding Tag). 비어 있으면 어떤 시퀀스에도 빌려 주지 않는다.
	// 액터 이름(AActor_3 등)은 실행마다 바뀌어 저장할 수 없으므로 디자이너가 직접 붙인다.
	FString BindingTag;

	// UE AActor::EnableInput. 이 액터의 InputComponent를 PlayerController 입력 스택에 올린다.
	// 처음 부를 때 InputComponent를 만든다. 다시 켜도 바인딩은 그대로 남아 있다.
	FInputComponent* EnableInput(APlayerController* PlayerController);
	// 스택에서 내린다. EndPlay에서도 부른다.
	void DisableInput();
	FInputComponent* GetInputComponent() const { return InputComponent.get(); }

	UWorld* GetWorld() const { return World; }
	ULevel* GetLevel() const { return Level; }

	const TArray<UActorComponent*>& GetComponents() const { return Components; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	void SetRootComponent(USceneComponent* SceneComponent) { RootComponent = SceneComponent; }

	void RemoveOwnedComponent(UActorComponent* Component);
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	FVector GetActorScale3D() const;
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();

	// 루트를 Delta만큼 옮긴다 (UE AddActorWorldOffset). bSweep이면 이 액터의 막는 Shape(bBlockComponent)들로
	// 가는 길을 검사해 가장 먼저 막히는 곳에서 멈추고 Hit을 알린다. 막혔으면 true.
	// UE는 루트 Shape만 Sweep하지만, 씬 파일에서 루트를 바꿀 수 없어 자식 Shape도 검사한다.
	bool AddActorWorldOffset(const FVector& Delta, bool bSweep);

	// 두 액터 사이의 첫 컴포넌트 겹침이 시작될 때 / 마지막 겹침이 끝날 때 불린다 (UE와 같은 이름).
	// OnActorBeginOverlap / OnActorEndOverlap을 Broadcast한다. 스크립트 함수는 그 Delegate의 구독자로 불린다.
	virtual void NotifyActorBeginOverlap(AActor* OtherActor);
	virtual void NotifyActorEndOverlap(AActor* OtherActor);
	bool IsOverlappingActor(const AActor* Other) const;

	// (자신, 상대 액터)
	TMulticastDelegate<AActor*, AActor*> OnActorBeginOverlap;
	TMulticastDelegate<AActor*, AActor*> OnActorEndOverlap;

	// 내 컴포넌트가 Sweep으로 막히거나, 남이 Sweep하다 내 컴포넌트에 막혔을 때 불린다 (UE와 같은 이름).
	// Hit은 항상 내 입장 기준: HitComponent는 상대, Normal은 상대에서 나를 향한다.
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit);
	// (자신, 상대 액터, 충돌 정보)
	TMulticastDelegate<AActor*, AActor*, const FHitResult&> OnActorHit;

	//xx삭제 예정
	friend class UWorld;

	UActorComponent* AddComponent(UClass* ComponentClass, FName Name);
	void DestroyComponent(UActorComponent* Component);

	// bCanEverTick이 켜진 액터·컴포넌트의 Tick 함수만 World의 FTickTaskManager에 등록하거나 해제한다.
	void RegisterAllActorTickFunctions(bool bRegister);

	// UE와 같이 기본값은 bCanEverTick = false. 생성자에서 Target = this
	FActorTickFunction PrimaryActorTick;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	UActorComponent* FindComponentByName(FName Name) const;

	// 이 액터가 ViewTarget일 때 이번 프레임의 POV를 계산한다.
	// 카메라 컴포넌트가 있으면 그 시점, 없으면 액터 위치와 회전을 쓴다. 자식 클래스에서 오버라이드 가능.
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV);
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) override;

	//TSet<TObjectPtr<UActorComponent>> OwnedComponents;
	TArray<UActorComponent*> Components;
	USceneComponent* RootComponent = nullptr;

	UWorld* World = nullptr;
	ULevel* Level = nullptr;
private:
	bool bActorHasBegunPlay = false;

	// 플레이 중에만 존재한다. DispatchBeginPlay에서 만들고 Actor가 delete 될 때 지워진다.
	// EndPlay에서 지우지 않는 이유는 Script 안에서 Destroy가 호출되었을 수 있기 때문.
	TUniquePtr<FLuaScriptInstance> ScriptInstance;

	// 스크립트에 이벤트 함수가 있으면 해당 Delegate에 구독시킨다 (UE의 BeginPlay에서 AddDynamic 하는 것과 같다).
	//   OnOverlap(Other)                         → OnActorBeginOverlap
	//   OnEndOverlap(Other)                      → OnActorEndOverlap
	//   OnHit(Other)                             → OnActorHit
	//   OnComponentOverlap(MyComp, Other, OtherComp) → 내 PrimitiveComponent마다 OnComponentBeginOverlap
	// 한 액터의 여러 컴포넌트(풀링한 낙하물 등)가 각각 겹치는 것은 OnComponentOverlap으로 구분한다.
	void BindScriptEvents();
	// 위에서 구독한 것을 모두 끊는다 (Owner = this). EndPlay에서 부른다.
	void UnbindScriptEvents();

	// 바인딩이 Lua 함수를 붙잡고 있을 수 있어 ScriptInstance처럼 Actor가 delete 될 때 함께 지운다.
	// 선언의 역순으로 파괴되므로 ScriptInstance보다 먼저 지워진다.
	TUniquePtr<FInputComponent> InputComponent;


};