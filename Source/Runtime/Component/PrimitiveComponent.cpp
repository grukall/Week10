#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/ShapeComponent.h"
#include "Component/BoxComponent.h"
#include "Component/SphereComponent.h"
#include "Component/CapsuleComponent.h"
#include "Collision/ShapeCollision.h"

namespace
{
	// Sweep 한 칸의 길이를 정할 때 쓴다. 도형에서 가장 얇은 방향의 반 두께
	float GetMinHalfSize(UShapeComponent* Shape)
	{
		if (UBoxComponent* Box = Cast<UBoxComponent>(Shape))
		{
			const FVector Extent = Box->GetScaledBoxExtent();
			return FMath::Min(Extent.X, FMath::Min(Extent.Y, Extent.Z));
		}
		if (USphereComponent* Sphere = Cast<USphereComponent>(Shape))
			return Sphere->GetScaledSphereRadius();
		if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Shape))
			return Capsule->GetScaledCapsuleRadius();
		return 0.0f;
	}

	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
	bHasBegunPlay = true;
	// 처음부터 겹친 상태로 시작해도 첫 Tick에 Begin이 나도록
	MarkOverlapsDirty();
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
	//if (Mesh && Material)
	//{
	//	FRenderPacket rp;
	//	rp.mesh = Mesh;
	//	rp.material = Material;
	//	rp.model = GetWorldMatrix();

	//	// Todo: subuv
	//	//rp.bSubUV = false;

	//	RenderQueue.Enqueue(rp);
	//}
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bVisible" || Property.Name == "bCastShadow" || Property.Name == "bReceiveShadow")
		MarkRenderStateDirty();
	if (Property.Name == "bGenerateOverlapEvents" || Property.Name == "bBlockComponent")
		MarkOverlapsDirty();
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
	// 여기서 바로 계산하지 않고 표시만 한다. 한 프레임에 여러 번 움직여도 Tick에서 한 번만 계산하고,
	// 이벤트 처리 함수가 또 무언가를 옮겨도 Setter 안에서 재귀가 생기지 않는다.
	MarkOverlapsDirty();
}

void UPrimitiveComponent::MarkOverlapsDirty()
{
	if (!bHasBegunPlay || !GetOwner() || !GetOwner()->GetWorld())
		return;
	GetOwner()->GetWorld()->MarkOverlapDirty(this);
}

bool UPrimitiveComponent::ShouldGenerateOverlapWith(const UPrimitiveComponent* Other) const
{
	return Other
		&& Other != this
		&& Other->bHasBegunPlay
		&& Other->GetOwner() != GetOwner()                  // 같은 액터끼리는 겹침으로 보지 않는다
		&& bGenerateOverlapEvents && Other->bGenerateOverlapEvents
		&& !(bBlockComponent && Other->bBlockComponent);    // 둘 다 Block이면 Overlap이 아니라 Hit 대상
}

void UPrimitiveComponent::UpdateOverlaps()
{
	// 0. Sweep 경로에서 스친 상대에게 먼저 Begin을 낸다. 지나쳐버렸으면 아래 2번에서 End가 이어진다.
	const TArray<UPrimitiveComponent*> SweepOverlaps = std::move(PendingSweepOverlaps);
	PendingSweepOverlaps.Empty();
	for (UPrimitiveComponent* Swept : SweepOverlaps)
	{
		// 스친 뒤 같은 프레임에 Destroy된 상대는 건너뛴다 (실제 삭제는 프레임 끝 FlushPendingKill)
		if (::IsValid(Swept) && ShouldGenerateOverlapWith(Swept))
			BeginComponentOverlap(Swept);
	}

	// 1. 지금 위치에서 겹친 상대를 새로 구한다. Shape가 아니면 겹칠 수 있는 게 없다.
	TArray<UPrimitiveComponent*> NewOverlaps;
	UShapeComponent* Shape = Cast<UShapeComponent>(this);
	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	if (Shape && World && bHasBegunPlay)
	{
		TArray<UPrimitiveComponent*> Candidates;
		World->OverlapMulti(Shape, Candidates);
		for (UPrimitiveComponent* Candidate : Candidates)
		{
			if (ShouldGenerateOverlapWith(Candidate))
				NewOverlaps.Add(Candidate);
		}
	}

	// 2. 끝난 겹침. EndComponentOverlap이 OverlappingComponents를 고치므로 복사본을 돈다.
	const TArray<UPrimitiveComponent*> OldOverlaps = OverlappingComponents;
	for (UPrimitiveComponent* Old : OldOverlaps)
	{
		if (NewOverlaps.Find(Old) == INDEX_NONE)
			EndComponentOverlap(Old);
	}

	// 3. 새로 시작한 겹침
	for (UPrimitiveComponent* New : NewOverlaps)
	{
		if (OverlappingComponents.Find(New) == INDEX_NONE)
			BeginComponentOverlap(New);
	}
}

void UPrimitiveComponent::ClearComponentOverlaps()
{
	const TArray<UPrimitiveComponent*> OldOverlaps = OverlappingComponents;
	for (UPrimitiveComponent* Old : OldOverlaps)
		EndComponentOverlap(Old);
	PendingSweepOverlaps.Empty();

	// 대기 중인 갱신이 남아 있으면 사라진 포인터로 UpdateOverlaps가 불린다.
	if (GetOwner() && GetOwner()->GetWorld())
		GetOwner()->GetWorld()->CancelOverlapUpdate(this);
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* Other)
{
	if (OverlappingComponents.Find(Other) != INDEX_NONE)
		return;

	AActor* MyActor = GetOwner();
	AActor* OtherActor = Other->GetOwner();
	// 목록에 넣기 전에 봐야 "두 액터의 첫 겹침"인지 알 수 있다.
	const bool bActorsWereOverlapping = MyActor && MyActor->IsOverlappingActor(OtherActor);

	OverlappingComponents.Add(Other);
	Other->OverlappingComponents.Add(this);

	// 컴포넌트 명단에 알린다. 스크립트의 OnComponentOverlap도 이 명단의 구독자로 불린다 (AActor::BindScriptEvents)
	OnComponentBeginOverlap.Broadcast(this, OtherActor, Other);
	Other->OnComponentBeginOverlap.Broadcast(Other, MyActor, this);
	HTR_LOG(Info, "BeginOverlap: {} <-> {}", GetName(), Other->GetName());

	if (!bActorsWereOverlapping && MyActor && OtherActor)
	{
		MyActor->NotifyActorBeginOverlap(OtherActor);
		OtherActor->NotifyActorBeginOverlap(MyActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* Other)
{
	const int32 MyIndex = OverlappingComponents.Find(Other);
	if (MyIndex == INDEX_NONE)
		return;

	OverlappingComponents.RemoveAtSwap(MyIndex);
	const int32 OtherIndex = Other->OverlappingComponents.Find(this);
	if (OtherIndex != INDEX_NONE)
		Other->OverlappingComponents.RemoveAtSwap(OtherIndex);

	AActor* MyActor = GetOwner();
	AActor* OtherActor = Other->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, Other);
	Other->OnComponentEndOverlap.Broadcast(Other, MyActor, this);
	HTR_LOG(Info, "EndOverlap: {} <-> {}", GetName(), Other->GetName());

	// 목록에서 뺀 뒤에 봐야 "두 액터의 마지막 겹침이 끝났는지" 알 수 있다.
	if (MyActor && OtherActor && !MyActor->IsOverlappingActor(OtherActor))
	{
		MyActor->NotifyActorEndOverlap(OtherActor);
		OtherActor->NotifyActorEndOverlap(MyActor);
	}
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	for (const UPrimitiveComponent* Comp : OverlappingComponents)
	{
		if (Comp == Other)
			return true;
	}
	return false;
}

bool UPrimitiveComponent::IsOverlappingActor(const AActor* Other) const
{
	if (!Other)
		return false;
	for (const UPrimitiveComponent* Comp : OverlappingComponents)
	{
		if (Comp->GetOwner() == Other)
			return true;
	}
	return false;
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* OutHit)
{
	const FVector Start = GetWorldLocation();

	// 호출하는 쪽(ProjectileMovement)이 같은 FHitResult를 반복해서 넘기므로 매번 비운다
	FHitResult Hit;
	Hit.Init(Start, Start + Delta);

	UShapeComponent* Shape = Cast<UShapeComponent>(this);
	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	// 막는 Shape가 아니면 검사할 게 없다. 겹침은 이동 후 Tick에서 따로 갱신된다.
	const bool bCanSweep = bSweep && Shape && World && bHasBegunPlay && bBlockComponent
		&& Delta.Size() > KINDA_SMALL_NUMBER;

	const bool bBlocked = bCanSweep && SweepBlocking(Shape, World, Delta, Hit);

	// 실제로 간 거리(멈춘 곳까지)에서 스친 겹침 상대를 모은다. Offset 판정이라 옮기기 전에 해야 한다.
	const bool bCanSweepOverlap = bSweep && Shape && World && bHasBegunPlay && bGenerateOverlapEvents
		&& Delta.Size() > KINDA_SMALL_NUMBER;
	if (bCanSweepOverlap)
		GatherSweepOverlaps(Shape, World, Delta * Hit.Time);

	// 안 막혔으면 Hit.Time = 1이라 끝까지 간다
	SetWorldLocationAndRotation(Start + Delta * Hit.Time, NewRotation, bSweep);

	if (OutHit)
		*OutHit = Hit;
	if (bBlocked)
		DispatchBlockingHit(Hit);

	return true;
}

bool UPrimitiveComponent::ShouldBlockWith(const UPrimitiveComponent* Other) const
{
	return Other
		&& Other != this
		&& Other->bHasBegunPlay
		&& Other->GetOwner() != GetOwner()
		&& bBlockComponent && Other->bBlockComponent;
}

bool UPrimitiveComponent::SweepBlocking(UShapeComponent* Shape, UWorld* World, const FVector& Delta, FHitResult& OutHit) const
{
	// 한 칸을 너무 크게 잡으면 얇은 벽을 건너뛰므로 칸 수 상한과 경계 정밀도를 둔다
	constexpr int32 MaxSweepSteps = 64;
	constexpr int32 BinarySearchIterations = 12;

	// 1. 넓은 단계: 시작 AABB와 끝 AABB를 합친 범위에 걸치는 Shape만 후보로 삼는다
	FBox SweptBounds = Shape->CalcBounds();
	SweptBounds.Expand(FBox{ SweptBounds.Min + Delta, SweptBounds.Max + Delta });
	TArray<UShapeComponent*> Shapes;
	World->GatherShapesInBounds(SweptBounds, Shape, Shapes);

	TArray<UShapeComponent*> Candidates;
	float StepLength = GetMinHalfSize(Shape);
	for (UShapeComponent* Other : Shapes)
	{
		if (!ShouldBlockWith(Other))
			continue;

		// 2. 출발할 때부터 박혀 있는 상대 (UE의 bStartPenetrating)
		FContact Contact;
		if (ShapeCollision::OverlapShapes(Shape, Other, &Contact))
		{
			// Contact.Normal은 나 → 상대. 상대 쪽으로 가려 하면 그 자리에서 막고, 빠져나가는 이동은 허용한다.
			if (Delta.Dot(Contact.Normal) > 0.0f)
			{
				OutHit.bBlockingHit = true;
				OutHit.bStartPentrating = true;
				OutHit.Time = 0.0f;
				OutHit.Distance = 0.0f;
				OutHit.Location = OutHit.TraceStart;
				OutHit.Normal = OutHit.ImpactNormal = -Contact.Normal;
				OutHit.ImpactPoint = Contact.Point;
				OutHit.PenetrationDepth = Contact.Depth;
				OutHit.HitComponent = Other;
				return true;
			}
			continue;
		}

		Candidates.Add(Other);
		// 상대가 얇아도 건너뛰지 않도록 칸 길이는 양쪽 중 더 얇은 쪽 기준
		StepLength = FMath::Min(StepLength, GetMinHalfSize(Other));
	}
	if (Candidates.IsEmpty())
		return false;

	// Time(0~1)만큼 갔을 때 막히는 상대. 컴포넌트를 실제로 옮기지 않고 Offset으로 시험한다.
	auto FindBlocking = [&](float Time, FContact* OutContact) -> UShapeComponent*
	{
		for (UShapeComponent* Other : Candidates)
		{
			if (ShapeCollision::OverlapShapes(Shape, Other, OutContact, Delta * Time))
				return Other;
		}
		return nullptr;
	};

	// 3. 칸 단위로 전진하며 처음 겹치는 칸을 찾는다
	const float Distance = Delta.Size();
	const int32 NumSteps = FMath::Clamp((int32)std::ceil(Distance / FMath::Max(StepLength, KINDA_SMALL_NUMBER)), 1, MaxSweepSteps);
	float SafeTime = 0.0f;   // 여기까지는 안 겹친다
	float HitTime = -1.0f;   // 여기서는 겹친다
	for (int32 Step = 1; Step <= NumSteps; ++Step)
	{
		const float Time = (float)Step / NumSteps;
		if (FindBlocking(Time, nullptr))
		{
			HitTime = Time;
			break;
		}
		SafeTime = Time;
	}
	if (HitTime < 0.0f)
		return false;

	// 4. 이분 탐색: 안 겹치는 SafeTime과 겹치는 HitTime 사이를 반씩 좁혀 닿는 순간을 찾는다
	for (int32 Iter = 0; Iter < BinarySearchIterations; ++Iter)
	{
		const float Mid = (SafeTime + HitTime) * 0.5f;
		if (FindBlocking(Mid, nullptr))
			HitTime = Mid;
		else
			SafeTime = Mid;
	}

	// 멈출 곳은 SafeTime(안 겹침), 접촉 정보는 막 닿은 HitTime에서 얻는다
	FContact Contact;
	UShapeComponent* HitShape = FindBlocking(HitTime, &Contact);
	OutHit.bBlockingHit = true;
	OutHit.Time = SafeTime;
	OutHit.Distance = Distance * SafeTime;
	OutHit.Location = OutHit.TraceStart + Delta * SafeTime;
	// FHitResult의 법선은 상대 표면에서 나를 향한다. Contact.Normal(나 → 상대)의 반대
	OutHit.Normal = OutHit.ImpactNormal = -Contact.Normal;
	OutHit.ImpactPoint = Contact.Point;
	OutHit.PenetrationDepth = 0.0f;
	OutHit.HitComponent = HitShape;
	return true;
}

void UPrimitiveComponent::GatherSweepOverlaps(UShapeComponent* Shape, UWorld* World, const FVector& Delta)
{
	constexpr int32 MaxSweepSteps = 64;

	const float Distance = Delta.Size();
	if (Distance <= KINDA_SMALL_NUMBER)
		return;

	// 넓은 단계는 SweepBlocking과 같다: 시작 AABB와 끝 AABB를 합친 범위
	FBox SweptBounds = Shape->CalcBounds();
	SweptBounds.Expand(FBox{ SweptBounds.Min + Delta, SweptBounds.Max + Delta });
	TArray<UShapeComponent*> Shapes;
	World->GatherShapesInBounds(SweptBounds, Shape, Shapes);

	const float MyHalfSize = GetMinHalfSize(Shape);
	for (UShapeComponent* Other : Shapes)
	{
		if (!::IsValid(Other) || !ShouldGenerateOverlapWith(Other) || PendingSweepOverlaps.Find(Other) != INDEX_NONE)
			continue;

		// 닿았는지만 보면 되니 이분 탐색 없이 칸 단위로만 훑는다. 칸 길이는 더 얇은 쪽 기준
		const float StepLength = FMath::Max(FMath::Min(MyHalfSize, GetMinHalfSize(Other)), KINDA_SMALL_NUMBER);
		const int32 NumSteps = FMath::Clamp((int32)std::ceil(Distance / StepLength), 1, MaxSweepSteps);
		for (int32 Step = 0; Step <= NumSteps; ++Step)
		{
			if (ShapeCollision::OverlapShapes(Shape, Other, nullptr, Delta * ((float)Step / NumSteps)))
			{
				PendingSweepOverlaps.Add(Other);
				break;
			}
		}
	}
}

void UPrimitiveComponent::DispatchBlockingHit(const FHitResult& Hit)
{
	UPrimitiveComponent* Other = Hit.HitComponent;
	AActor* MyActor = GetOwner();
	AActor* OtherActor = Other ? Other->GetOwner() : nullptr;
	if (!MyActor || !OtherActor)
		return;

	// 상대 입장에서 본 Hit (UE의 FHitResult::GetReversedHit): 법선을 뒤집고 HitComponent를 나로
	FHitResult OtherHit = Hit;
	OtherHit.Normal = -Hit.Normal;
	OtherHit.ImpactNormal = -Hit.ImpactNormal;
	OtherHit.HitComponent = this;

	MyActor->NotifyHit(this, OtherActor, Other, Hit);
	OnComponentHit.Broadcast(this, OtherActor, Other, Hit);
	OtherActor->NotifyHit(Other, MyActor, this, OtherHit);
	Other->OnComponentHit.Broadcast(Other, MyActor, this, OtherHit);
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}
