#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Core/NameTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include "Core/EngineString.h"
#include "UObject/WeakObjectPtr.h"
#include "Component/ParticleSubUVComponent.h"

class UWorld;
class AActor;

// SpawnEffect가 돌려주는 핸들. 슬롯이 반환·재사용되면 Generation이 달라져 옛 핸들은 무시된다.
// 이펙트 프리셋 값 하나를 읽는 쪽 (Lua 테이블, 시퀀스 JSON). 타입이 맞지 않으면 false
struct IEffectValueReader
{
	virtual ~IEffectValueReader() = default;
	virtual bool ReadNumber(float& Out) const = 0;
	virtual bool ReadBool(bool& Out) const = 0;
	virtual bool ReadString(FString& Out) const = 0;
	virtual bool ReadVector(FVector& Out) const = 0;
};

// 프리셋 키 하나를 FEffectDesc 필드에 넣는다. 키 이름은 C++ 필드 이름과 같다 (Lua RegisterEffect와 시퀀스 Effects가 같이 쓴다).
// 실패하면 false: 모르는 키면 bOutUnknownKey, 값이 틀리면 OutError에 "{키}는 숫자여야 합니다" 같은 이유를 남긴다.
bool ApplyEffectDescField(FEffectDesc& Desc, const FString& Key, const IEffectValueReader& Value, FString& OutError, bool& bOutUnknownKey);

struct FEffectHandle
{
	int32 Index = -1;
	uint32 Generation = 0;

	bool IsValid() const { return Index >= 0; }

	// Lua에 숫자 하나로 넘긴다. LuaJIT는 숫자를 double로 들고 있어 2^53까지만 정확하므로
	// Generation을 32비트 위로 올리지 않고 Index(MaxPoolSize 미만)를 아래 16비트에 넣는다. 무효면 -1
	int64 Pack() const { return IsValid() ? static_cast<int64>(Generation) * 65536 + Index : -1; }
	static FEffectHandle Unpack(int64 Packed)
	{
		if (Packed < 0)
			return FEffectHandle();
		return FEffectHandle{ static_cast<int32>(Packed % 65536), static_cast<uint32>(Packed / 65536) };
	}
};

// 월드의 이펙트를 파티클 컴포넌트 풀에서 꺼내 재생하고, 끝나면 돌려받는다.
// UWorld가 소유한다. 게임 월드의 NotifyBeginPlay 시작에서 Init, EndPlay·ClearWorld에서 Shutdown 한다.
// 시뮬레이션은 각 컴포넌트의 TickComponent가 하고, 매니저 Tick은 수명만 관리한다.
class FEffectManager
{
public:
	// 기본 프리셋은 여기서 한 번만 등록한다. 프리셋 표는 Init/Shutdown과 상관없이 유지된다.
	FEffectManager();

	void Init(UWorld* InWorld);
	void Shutdown();
	// UWorld::Tick이 액터 Tick·겹침 이벤트·코루틴 뒤, FlushPendingKill 전에 부른다.
	void Tick(float DeltaTime);

	// 같은 이름이 있으면 덮어쓴다. Init 전에도 등록할 수 있다.
	void RegisterEffect(FName Name, const FEffectDesc& Desc);
	bool HasEffect(FName Name) const;
	// 등록된 프리셋. 없으면 nullptr. 기존 프리셋을 바탕으로 새 프리셋을 만들 때 쓴다.
	const FEffectDesc* FindEffect(FName Name) const;

	// Duration > 0: 그 시간 뒤 방출을 멈춘다.
	// Duration <= 0: 루프면 StopEffect까지 계속, 루프가 아니면 한 번 방출하고 끝난다.
	// 방출이 멈춘 뒤 남은 입자가 모두 사라지면 슬롯을 돌려받는다.
	FEffectHandle SpawnEffect(FName Effect, const FVector& Location, const FRotator& Rotation = FRotator(),
		bool bLoop = false, float Duration = 0.0f);

	// Target의 루트 컴포넌트에 붙여 따라다니게 한다. Relative의 Location·Rotation만 쓴다.
	// 방출 지점과 방향만 따라가고, 이미 나온 입자는 월드에 남는다 (발밑 연기의 꼬리).
	// Target이 파괴되면 그 자리에 떼어 두고 방출을 멈춘다.
	FEffectHandle SpawnEffectAttached(FName Effect, AActor* Target, const FTransform& Relative = FTransform(),
		bool bLoop = false, float Duration = 0.0f);

	// Location에서 떠오르며 사라지는 텍스트 (데미지 숫자 등). 매 프레임 HUD 월드 텍스트로 그린다.
	void SpawnTextEffect(const FString& Text, const FVector& Location,
		const FLinearColor& Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), float Duration = 1.0f);

	// bImmediate가 false면 방출만 멈추고 남은 입자가 사라진 뒤 반환한다.
	void StopEffect(FEffectHandle Handle, bool bImmediate = false);
	bool IsEffectActive(FEffectHandle Handle) const;

	bool IsInitialized() const { return World != nullptr; }
	int32 GetPoolSize() const { return Slots.Num(); }
	int32 GetActiveCount() const;
	int32 GetTextEffectCount() const { return TextEffects.Num(); }

private:
	enum class ESlotState : uint8
	{
		Free,       // 풀에서 쉬는 중
		Active,     // 방출 중
		Stopping,   // 방출을 멈추고 남은 입자가 사라지길 기다리는 중
	};

	struct FSlot
	{
		UParticleSubUVComponent* Component = nullptr;
		ESlotState State = ESlotState::Free;
		uint32 Generation = 0;
		float RemainingTime = -1.0f;    // 남은 방출 시간. 음수면 제한 없음
		bool bAttached = false;
		// UUID로 찾으므로 대상이 Destroy 예정이거나 지워졌으면 IsValid()가 false다.
		TWeakObjectPtr<AActor> AttachTarget;
	};

	struct FTextEffect
	{
		FString Text;
		FVector Location;
		FLinearColor Color;
		float Age = 0.0f;
		float Duration = 1.0f;
	};

	void TickTextEffects(float DeltaTime);
	void RegisterDefaultEffects();
	bool AddPooledComponent();
	int32 AcquireSlot();
	// 트랜스폼을 정한 슬롯의 컴포넌트에 Desc를 넣고 방출을 시작한다.
	FEffectHandle StartSlot(int32 Index, const FEffectDesc& Desc, bool bLoop, float Duration);
	void ReleaseSlot(int32 Index);
	// 붙어 있던 컴포넌트를 풀 액터 루트로 되돌린다. 월드 위치는 유지한다.
	void DetachToPool(FSlot& Slot);
	const FSlot* FindSlot(FEffectHandle Handle) const;

	// 동시에 보일 이펙트 수 정도. 모자라면 MaxPoolSize까지 늘린다.
	static constexpr int32 InitialPoolSize = 16;
	static constexpr int32 MaxPoolSize = 64;
	static_assert(MaxPoolSize <= 65536, "FEffectHandle::Pack는 Index를 16비트에 넣는다");

	// 텍스트 이펙트 모양. 크기는 HUD 기준 단위(1920x1080)
	static constexpr float TextFontSize = 56.0f;
	static constexpr float TextRiseSpeed = 0.8f;        // 월드 단위/초
	static constexpr float TextFadeStart = 0.6f;        // 수명 비율이 이 값을 넘으면 페이드아웃
	static constexpr float TextPopTime = 0.15f;         // 처음 이만큼은 크게 시작해 원래 크기로 줄어든다
	static constexpr float TextPopScale = 1.4f;
	static constexpr float TextShadowOffset = 3.0f;

	UWorld* World = nullptr;
	// 풀 컴포넌트를 소유하는 숨은 액터. 컴포넌트는 Owner를 통해 월드와 Scene·Tick에 등록된다.
	AActor* PoolOwner = nullptr;

	TArray<FSlot> Slots;
	TArray<int32> FreeSlots;
	TArray<FTextEffect> TextEffects;
	TMap<FName, FEffectDesc, FNameHash> Descs;

	// Init마다 올려 새 슬롯의 시작 Generation으로 쓴다. 이전 플레이의 핸들이 새 슬롯과 우연히 맞지 않게 한다.
	uint32 SessionGeneration = 0;
};
