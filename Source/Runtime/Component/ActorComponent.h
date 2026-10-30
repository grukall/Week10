#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Engine/EngineBaseTypes.h"

class AActor;
class FLuaScriptInstance;

class UActorComponent : public UObject
{
	DECLARE_CLASS(UActorComponent, UObject)

	REFLECT_START(ClassName)
		PROPERTY_FLAGS(ScriptPath, CPF_EditAnywhere | CPF_LuaScriptPath)
		// GetOwner는 반환 타입 AActor가 여기서는 전방 선언뿐이라 등록할 수 없다(Actor.h와 순환 include).
		// 컴포넌트 스크립트에서는 owner로 소유 액터를 쓴다.
		FUNCTION(SetActive)
		FUNCTION(IsActive)
		FUNCTION(SetHiddenInGame)
		FUNCTION(IsHiddenInGame)
	REFLECT_END()

public:
	// TUniquePtr<FLuaScriptInstance> 멤버를 지우려면 완전한 타입이 필요하므로 생성자·소멸자는 cpp에 둔다
	UActorComponent();
	virtual ~UActorComponent() override;

	virtual void InitializeComponent() {};
	virtual void BeginPlay() {};
	// 소유 액터의 EndPlay나 DestroyComponent에서 호출된다. BeginPlay를 받은 컴포넌트만 받는다.
	virtual void EndPlay() {};
	virtual void TickComponent(float DeltaTime)
	{
		//HTR_LOG(Info, "TickComponent");
	};

	// 소유 액터와 FActorComponentTickFunction이 호출하는 진입점. 서브클래스가 Super를 어디서 부르든
	// C++ 처리가 끝난 뒤에 스크립트를 부른다. 스크립트는 소유 액터가 플레이 중일 때만 로드한다.
	void DispatchBeginPlay();
	void DispatchTickComponent(float DeltaTime);
	void DispatchEndPlay();

	// 비어 있으면 스크립트 없음. 씬 저장과 PIE 복제는 리플렉션(PROPERTY)이 처리한다.
	FString ScriptPath;
	FLuaScriptInstance* GetScriptInstance() const { return ScriptInstance.get(); }

	void SetOwner(AActor* InOwner) { Owner = InOwner; }
    AActor* GetOwner() const { return Owner; }

	// 디테일 패널에서 이 컴포넌트를 통째로 숨긴다 (에디터 시각화용 빌보드 등)
	void SetHiddenInDetails(bool bInHidden) { bHiddenInDetails = bInHidden; }
	bool IsHiddenInDetails() const { return bHiddenInDetails; }

	// 게임 플레이(PIE) 중에 이 컴포넌트를 숨길지 여부
	void SetHiddenInGame(bool bInHidden) { bHiddenInGame = bInHidden; }
	bool IsHiddenInGame() const { return bHiddenInGame; }

	// UE와 같이 기본값은 bCanEverTick = false. Tick이 필요한 컴포넌트만 생성자에서 켠다.
	FActorComponentTickFunction PrimaryComponentTick;

	virtual void SetActive(bool bNewActive);
	bool IsActive() const { return bIsActive; }

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record);

private:
	AActor* Owner = nullptr;
	bool bHiddenInDetails = false;
	bool bHiddenInGame = false;
	bool bIsActive = true;

	// 플레이 중에만 존재한다. DispatchBeginPlay에서 만들고 Component가 delete 될 때 지워진다.
	// EndPlay에서 지우지 않는 이유는 Script 안에서 Destroy가 호출되었을 수 있기 때문.
	TUniquePtr<FLuaScriptInstance> ScriptInstance;
};