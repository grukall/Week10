#include "EnginePCH.h"
#include "Component/HitSquashComponent.h"

#include "Component/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

#include <cmath>

namespace
{
	// 출렁이는 횟수. 1.5면 찌그러짐 → 늘어남 → 찌그러짐 순으로 한 번 반 출렁인다
	constexpr float SquashWobbles = 1.5f;
}

UHitSquashComponent::UHitSquashComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHitSquashComponent::Squash()
{
	if (!bSquashing)
	{
		Target.Reset();
		if (AActor* Owner = GetOwner())
		{
			for (UActorComponent* Component : Owner->GetComponents())
			{
				if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
				{
					Target = Mesh;
					break;
				}
			}
		}
		if (!Target.IsValid() || SquashTime <= 0.0f)
			return;
		BaseScale = Target->GetRelativeScale3D();
	}

	ElapsedTime = 0.0f;
	bSquashing = true;
	// 히트스톱으로 dt가 0이어도 맞은 순간부터 찌그러져 보이도록 바로 적용한다
	ApplyScale();
}

void UHitSquashComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!bSquashing)
		return;

	ElapsedTime += DeltaTime;
	if (ElapsedTime >= SquashTime)
		Restore();
	else
		ApplyScale();
}

void UHitSquashComponent::EndPlay()
{
	Restore();
	Super::EndPlay();
}

void UHitSquashComponent::ApplyScale()
{
	USceneComponent* Mesh = Target.Get();
	if (!Mesh)
	{
		bSquashing = false;
		return;
	}

	// 감쇠하는 cos: 1(찌그러짐)에서 시작해 음수(늘어남)를 지나 0으로 가라앉는다
	const float Alpha = ElapsedTime / SquashTime;
	const float Decay = (1.0f - Alpha) * (1.0f - Alpha);
	const float Weight = std::cos(2.0f * PI * SquashWobbles * Alpha) * Decay * SquashAmount;
	Mesh->SetRelativeScale3D(FVector(BaseScale.X * (1.0f + Weight), BaseScale.Y * (1.0f + Weight), BaseScale.Z * (1.0f - Weight)));
}

void UHitSquashComponent::Restore()
{
	if (bSquashing)
	{
		if (USceneComponent* Mesh = Target.Get())
			Mesh->SetRelativeScale3D(BaseScale);
	}
	bSquashing = false;
	ElapsedTime = 0.0f;
}
