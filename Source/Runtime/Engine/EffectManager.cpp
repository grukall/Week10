#include "EnginePCH.h"
#include "Engine/EffectManager.h"

bool ApplyEffectDescField(FEffectDesc& Desc, const FString& Key, const IEffectValueReader& Value, FString& OutError, bool& bOutUnknownKey)
{
	bOutUnknownKey = false;

	const auto Number = [&](float& Out)
	{
		if (Value.ReadNumber(Out))
			return true;
		OutError = std::format("{}는 숫자여야 합니다", Key);
		return false;
	};
	const auto Count = [&](uint32 Min, uint32& Out)
	{
		float N = 0.0f;
		if (!Number(N))
			return false;
		if (N < static_cast<float>(Min))
		{
			OutError = std::format("{}는 {} 이상이어야 합니다", Key, Min);
			return false;
		}
		Out = static_cast<uint32>(N);
		return true;
	};
	const auto Bool = [&](bool& Out)
	{
		if (Value.ReadBool(Out))
			return true;
		OutError = std::format("{}는 true/false여야 합니다", Key);
		return false;
	};
	const auto Vector = [&](FVector& Out)
	{
		if (Value.ReadVector(Out))
			return true;
		OutError = std::format("{}는 FVector여야 합니다", Key);
		return false;
	};

	uint32 Unsigned = 0;
	if (Key == "TexturePath")
	{
		if (Value.ReadString(Desc.TexturePath))
			return true;
		OutError = "TexturePath는 문자열이어야 합니다";
		return false;
	}
	if (Key == "Cols")              return Count(1, Desc.Cols);
	if (Key == "Rows")              return Count(1, Desc.Rows);
	if (Key == "FrameCount")        return Count(0, Desc.FrameCount);
	if (Key == "FrameRate")         return Number(Desc.FrameRate);
	if (Key == "bRandomStartFrame") return Bool(Desc.bRandomStartFrame);
	if (Key == "ParticleCount")
	{
		if (!Count(0, Unsigned))
			return false;
		Desc.ParticleCount = static_cast<int32>(Unsigned);
		return true;
	}
	if (Key == "MinLifeTime")       return Number(Desc.MinLifeTime);
	if (Key == "MaxLifeTime")       return Number(Desc.MaxLifeTime);
	if (Key == "MinVelocity")       return Vector(Desc.MinVelocity);
	if (Key == "MaxVelocity")       return Vector(Desc.MaxVelocity);
	if (Key == "Acceleration")      return Vector(Desc.Acceleration);
	if (Key == "StartScale")        return Number(Desc.StartScale);
	if (Key == "EndScale")          return Number(Desc.EndScale);
	if (Key == "FadeStart")         return Number(Desc.FadeStart);
	if (Key == "bBurst")            return Bool(Desc.bBurst);

	bOutUnknownKey = true;
	return false;
}
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"

FEffectManager::FEffectManager()
{
	RegisterDefaultEffects();
}

void FEffectManager::Init(UWorld* InWorld)
{
	if (!InWorld || World == InWorld)
		return;

	if (World)
		Shutdown();

	World = InWorld;
	++SessionGeneration;

	// 같은 Outer에 같은 이름이 있으면 NewObject가 기존 객체를 재사용한다. 같은 월드에서 다시 Init하거나
	// 레벨에 같은 이름의 액터가 있으면 한 액터가 레벨에 두 번 등록되어 두 번 지워지므로 이름을 고유하게 만든다.
	const FName PoolName = MakeUniqueObjectName(World->GetPersistentLevel(), AActor::StaticClass(), FName("EffectPool"));
	PoolOwner = World->SpawnActor<AActor>(PoolName);
	if (!PoolOwner)
	{
		HTR_LOG(Error, "[Effect] failed to spawn pool owner");
		World = nullptr;
		return;
	}

	Slots.Reserve(InitialPoolSize);
	FreeSlots.Reserve(InitialPoolSize);
	for (int32 i = 0; i < InitialPoolSize; ++i)
	{
		if (!AddPooledComponent())
			break;
	}
}

// 풀 액터와 컴포넌트는 월드가 액터를 정리할 때 함께 지운다. 여기서는 참조만 끊는다.
// 프리셋 표는 다음 플레이에서도 쓰도록 남겨 둔다.
void FEffectManager::Shutdown()
{
	Slots.Empty();
	FreeSlots.Empty();
	TextEffects.Empty();
	PoolOwner = nullptr;
	World = nullptr;
}

void FEffectManager::Tick(float DeltaTime)
{
	if (!World)
		return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FSlot& Slot = Slots[i];
		if (Slot.State == ESlotState::Free)
			continue;

		UParticleSubUVComponent* Component = Slot.Component;

		// 대상이 Destroy되면 실제로 지워지기(FlushPendingKill) 전에 떼어 둔다. 붙인 채로 두면 부모가 지워질 때
		// 부모 기준이던 상대 위치가 그대로 월드 위치가 되어 이펙트가 엉뚱한 곳으로 튄다.
		if (Slot.bAttached && !Slot.AttachTarget.IsValid())
		{
			DetachToPool(Slot);
			if (Slot.State == ESlotState::Active)
				Component->StopEmitting();
		}

		if (Slot.State == ESlotState::Active)
		{
			if (Slot.RemainingTime >= 0.0f)
			{
				Slot.RemainingTime -= DeltaTime;
				if (Slot.RemainingTime <= 0.0f)
					Component->StopEmitting();
			}

			// 루프가 아닌 이펙트는 컴포넌트가 스스로 방출을 멈춘다.
			if (!Component->IsEmitting())
				Slot.State = ESlotState::Stopping;
		}

		// 방출을 멈춰도 남은 입자가 사라질 때까지 기다려야 뚝 끊기지 않는다.
		if (Slot.State == ESlotState::Stopping && Component->IsFinished())
			ReleaseSlot(i);
	}

	TickTextEffects(DeltaTime);
}

// HUD는 매 프레임 비워지므로(UWorld::Tick 시작의 BeginFrame) 살아 있는 동안 매 프레임 다시 넣는다.
// 화면 좌표는 HUD가 렌더 시점에 그 뷰포트의 View로 구한다.
void FEffectManager::TickTextEffects(float DeltaTime)
{
	FHUD& HUD = World->GetHUD();

	for (int32 i = TextEffects.Num() - 1; i >= 0; --i)
	{
		FTextEffect& Effect = TextEffects[i];
		Effect.Age += DeltaTime;
		if (Effect.Age >= Effect.Duration)
		{
			TextEffects.RemoveAtSwap(i);
			continue;
		}

		Effect.Location.Z += TextRiseSpeed * DeltaTime;

		const float LifeRatio = Effect.Age / Effect.Duration;
		float Alpha = 1.0f;
		if (LifeRatio > TextFadeStart)
		{
			const float T = (LifeRatio - TextFadeStart) / (1.0f - TextFadeStart);
			Alpha = 1.0f - T * T * (3.0f - 2.0f * T);
		}

		// 처음엔 크게 튀어나왔다가 원래 크기로
		const float PopRatio = std::min(Effect.Age / TextPopTime, 1.0f);
		const float FontSize = TextFontSize * (TextPopScale + (1.0f - TextPopScale) * PopRatio);

		FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.6f * Alpha);
		FLinearColor Color = Effect.Color;
		Color.A *= Alpha;

		HUD.AddWorldText(Effect.Text, Effect.Location, FontSize, Shadow, FVector2(TextShadowOffset, TextShadowOffset));
		HUD.AddWorldText(Effect.Text, Effect.Location, FontSize, Color);
	}
}

void FEffectManager::SpawnTextEffect(const FString& Text, const FVector& Location, const FLinearColor& Color, float Duration)
{
	if (!World)
	{
		HTR_LOG(Warning, "[Effect] SpawnTextEffect({}) before the effect manager is initialized", Text);
		return;
	}

	FTextEffect Effect;
	Effect.Text = Text;
	Effect.Location = Location;
	Effect.Color = Color;
	Effect.Duration = Duration > 0.0f ? Duration : 1.0f;
	TextEffects.Add(Effect);
}

void FEffectManager::RegisterEffect(FName Name, const FEffectDesc& Desc)
{
	Descs[Name] = Desc;
}

bool FEffectManager::HasEffect(FName Name) const
{
	return Descs.Find(Name) != nullptr;
}

const FEffectDesc* FEffectManager::FindEffect(FName Name) const
{
	return Descs.Find(Name);
}

FEffectHandle FEffectManager::SpawnEffect(FName Effect, const FVector& Location, const FRotator& Rotation, bool bLoop, float Duration)
{
	if (!World)
	{
		HTR_LOG(Warning, "[Effect] SpawnEffect({}) before the effect manager is initialized", Effect.ToString());
		return FEffectHandle();
	}

	const FEffectDesc* Desc = Descs.Find(Effect);
	if (!Desc)
	{
		HTR_LOG(Warning, "[Effect] unknown effect: {}", Effect.ToString());
		return FEffectHandle();
	}

	const int32 Index = AcquireSlot();
	if (Index < 0)
		return FEffectHandle();

	// Activate가 방출 지점과 방향을 읽으므로 트랜스폼을 먼저 정한다.
	Slots[Index].Component->SetWorldLocationAndRotation(Location, Rotation, false);
	return StartSlot(Index, *Desc, bLoop, Duration);
}

FEffectHandle FEffectManager::SpawnEffectAttached(FName Effect, AActor* Target, const FTransform& Relative, bool bLoop, float Duration)
{
	if (!World)
	{
		HTR_LOG(Warning, "[Effect] SpawnEffectAttached({}) before the effect manager is initialized", Effect.ToString());
		return FEffectHandle();
	}

	USceneComponent* TargetRoot = ::IsValid(Target) ? Target->GetRootComponent() : nullptr;
	if (!TargetRoot)
	{
		HTR_LOG(Warning, "[Effect] SpawnEffectAttached({}): invalid target", Effect.ToString());
		return FEffectHandle();
	}

	const FEffectDesc* Desc = Descs.Find(Effect);
	if (!Desc)
	{
		HTR_LOG(Warning, "[Effect] unknown effect: {}", Effect.ToString());
		return FEffectHandle();
	}

	const int32 Index = AcquireSlot();
	if (Index < 0)
		return FEffectHandle();

	// 소유는 풀 액터 그대로 두고 부착만 대상 루트로 옮긴다. 부착은 컴포넌트끼리의 관계라
	// 대상이 움직이면 MarkTransformDirty가 이 컴포넌트까지 전파된다.
	FSlot& Slot = Slots[Index];
	Slot.Component->SetupAttachment(TargetRoot, EAttachmentRule::KeepRelative);
	Slot.Component->SetRelativeLocationAndRotation(Relative.Location, Relative.Rotation);
	Slot.bAttached = true;
	Slot.AttachTarget = TWeakObjectPtr<AActor>(Target);

	return StartSlot(Index, *Desc, bLoop, Duration);
}

FEffectHandle FEffectManager::StartSlot(int32 Index, const FEffectDesc& Desc, bool bLoop, float Duration)
{
	FSlot& Slot = Slots[Index];
	UParticleSubUVComponent* Component = Slot.Component;

	Component->ApplyDesc(Desc);
	Component->SetLoop(bLoop);
	Component->Activate();

	Slot.State = ESlotState::Active;
	Slot.RemainingTime = Duration > 0.0f ? Duration : -1.0f;

	return FEffectHandle{ Index, Slot.Generation };
}

void FEffectManager::StopEffect(FEffectHandle Handle, bool bImmediate)
{
	if (!FindSlot(Handle))
		return;

	if (bImmediate)
	{
		ReleaseSlot(Handle.Index);
		return;
	}

	FSlot& Slot = Slots[Handle.Index];
	Slot.Component->StopEmitting();
	Slot.State = ESlotState::Stopping;
}

bool FEffectManager::IsEffectActive(FEffectHandle Handle) const
{
	return FindSlot(Handle) != nullptr;
}

int32 FEffectManager::GetActiveCount() const
{
	int32 Count = 0;
	for (const FSlot& Slot : Slots)
	{
		if (Slot.State != ESlotState::Free)
			++Count;
	}
	return Count;
}

// 크기·속도는 GameJamMap0 스케일(캐릭터 키 약 1)에 맞춘 값이다. 스크립트에서 RegisterEffect로 덮어써 튜닝할 수 있다.
void FEffectManager::RegisterDefaultEffects()
{
	// 팝콘을 받았을 때: 금색 반짝이가 살짝 솟았다가 작아지며 사라진다.
	// StarParticle은 8x8 칸 모두 반짝이 덩어리라 시작 프레임을 섞어 입자마다 달라 보이게 한다.
	FEffectDesc PopcornCatch;
	PopcornCatch.TexturePath = "Resources/SubUV/StarParticle.png";
	PopcornCatch.Cols = 8;
	PopcornCatch.Rows = 8;
	PopcornCatch.FrameRate = 30.0f;
	PopcornCatch.bRandomStartFrame = true;
	PopcornCatch.bBurst = true;
	PopcornCatch.ParticleCount = 6;
	PopcornCatch.MinLifeTime = 0.5f;
	PopcornCatch.MaxLifeTime = 0.8f;
	PopcornCatch.MinVelocity = FVector(-0.7f, -0.7f, 0.8f);
	PopcornCatch.MaxVelocity = FVector(0.7f, 0.7f, 1.6f);
	PopcornCatch.Acceleration = FVector(0.0f, 0.0f, -1.0f);
	PopcornCatch.StartScale = 1.3f;
	PopcornCatch.EndScale = 0.6f;
	PopcornCatch.FadeStart = 0.5f;
	RegisterEffect(FName("PopcornCatch"), PopcornCatch);

	// 팝콘이 아닌 것이 바닥·캐릭터에 부딪혔을 때: 흰 덩어리가 사방으로 튀고 떨어지며 부풀어 사라진다.
	// ParticleAtlas는 4x4 칸이 작은 구 → 큰 구 순서라 0번부터 수명 동안 한 번 재생한다 (16프레임 / 약 0.65초).
	FEffectDesc HitSplash;
	HitSplash.TexturePath = "Assets/SubUV/ParticleAtlas.png";
	HitSplash.Cols = 4;
	HitSplash.Rows = 4;
	HitSplash.FrameRate = 24.0f;
	HitSplash.bRandomStartFrame = false;
	HitSplash.bBurst = true;
	HitSplash.ParticleCount = 10;
	HitSplash.MinLifeTime = 0.45f;
	HitSplash.MaxLifeTime = 0.65f;
	HitSplash.MinVelocity = FVector(-2.5f, -2.5f, 1.2f);
	HitSplash.MaxVelocity = FVector(2.5f, 2.5f, 2.8f);
	HitSplash.Acceleration = FVector(0.0f, 0.0f, -7.0f);
	HitSplash.StartScale = 0.3f;
	HitSplash.EndScale = 0.7f;
	HitSplash.FadeStart = 0.6f;
	RegisterEffect(FName("HitSplash"), HitSplash);

	// 발밑 먼지: 걷는 동안 루프로 켜 두고 멈출 때 StopEffect 한다. 작은 흰 덩어리가 부풀며 꼬리를 남긴다.
	// CloudAtlas(회색)는 크림색 바닥에서 어둡게 떠 보여 흰 ParticleAtlas를 쓴다.
	FEffectDesc FootSmoke;
	FootSmoke.TexturePath = "Assets/SubUV/ParticleAtlas.png";
	FootSmoke.Cols = 4;
	FootSmoke.Rows = 4;
	FootSmoke.FrameRate = 22.0f;
	FootSmoke.bRandomStartFrame = false;
	FootSmoke.ParticleCount = 8;
	FootSmoke.MinLifeTime = 0.5f;
	FootSmoke.MaxLifeTime = 0.7f;
	FootSmoke.MinVelocity = FVector(-0.25f, -0.25f, 0.15f);
	FootSmoke.MaxVelocity = FVector(0.25f, 0.25f, 0.45f);
	FootSmoke.StartScale = 0.22f;
	FootSmoke.EndScale = 0.5f;
	FootSmoke.FadeStart = 0.35f;
	RegisterEffect(FName("FootSmoke"), FootSmoke);
}

bool FEffectManager::AddPooledComponent()
{
	UParticleSubUVComponent* Component = Cast<UParticleSubUVComponent>(
		PoolOwner->AddComponent(UParticleSubUVComponent::StaticClass(), NAME_None));
	if (!Component)
		return false;

	// AddComponent가 바로 BeginPlay를 보내 자동으로 켜진다. 끄고, 풀 액터가 나중에 BeginPlay를
	// 받으며 컴포넌트 BeginPlay를 한 번 더 부를 때 다시 켜지지 않게 한다.
	Component->SetAutoActivate(false);
	Component->Deactivate();

	FSlot Slot;
	Slot.Component = Component;
	Slot.Generation = SessionGeneration << 16;
	Slots.Add(Slot);
	FreeSlots.Add(Slots.Num() - 1);
	return true;
}

int32 FEffectManager::AcquireSlot()
{
	if (FreeSlots.Num() == 0)
	{
		if (Slots.Num() >= MaxPoolSize || !AddPooledComponent())
		{
			HTR_LOG(Warning, "[Effect] pool exhausted ({} effects active)", Slots.Num());
			return -1;
		}
	}

	const int32 Index = FreeSlots[FreeSlots.Num() - 1];
	FreeSlots.Pop();
	return Index;
}

// 즉시 정지·자연 종료·대상 파괴가 모두 여기를 지나므로 떼어내기도 여기서 한다.
void FEffectManager::ReleaseSlot(int32 Index)
{
	FSlot& Slot = Slots[Index];
	if (Slot.bAttached)
		DetachToPool(Slot);

	Slot.Component->Deactivate();
	Slot.State = ESlotState::Free;
	Slot.RemainingTime = -1.0f;
	++Slot.Generation;
	FreeSlots.Add(Index);
}

void FEffectManager::DetachToPool(FSlot& Slot)
{
	// KeepWorld: 대상이 사라진 자리에서 남은 입자가 마저 사라지게 한다.
	Slot.Component->SetupAttachment(PoolOwner->GetRootComponent(), EAttachmentRule::KeepWorld);
	Slot.bAttached = false;
	Slot.AttachTarget.Reset();
}

const FEffectManager::FSlot* FEffectManager::FindSlot(FEffectHandle Handle) const
{
	if (Handle.Index < 0 || Handle.Index >= Slots.Num())
		return nullptr;

	const FSlot& Slot = Slots[Handle.Index];
	if (Slot.Generation != Handle.Generation || Slot.State == ESlotState::Free)
		return nullptr;

	return &Slot;
}
