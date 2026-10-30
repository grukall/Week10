#include "EnginePCH.h"
#include "World.h"
#include "Level.h"

#include "Core/EngineStatics.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Input/InputSystem.h"

#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "LevelSequence/LevelSequencePlayer.h"

#include "UObject/UObjectIterator.h"

#include "Collision/Ray.h"
#include "Collision/ShapeCollision.h"
#include "Component/ShapeComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/ExponentialHeightFogComponent.h"
#include "Component/LightComponentBase.h"

#include "Component/StaticMeshComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include "Math/Frustum.h"

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"

#include "UObject/UObjectGlobals.h"
#include "Script/LuaScriptInstance.h"

#include <filesystem>

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick); // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정
DECLARE_CYCLE_STAT("Gather Render Packets", STAT_GatherRenderPackets);
DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);
DECLARE_CYCLE_STAT("Gather - LOD", STAT_GatherLOD);
DECLARE_CYCLE_STAT("Gather - Submit", STAT_GatherSubmit);

UWorld::~UWorld()
{

}

bool UWorld::Init()
{
	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		HTR_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	//레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	//카메라 생성
	CreateMainCamera();

	return true;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (!Class) return nullptr;
	if (!Class->IsChildOf(AActor::StaticClass())) return nullptr;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(PersistentLevel, Class, InName);

	if (!NewActor)
	{
		HTR_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor에 World/Level 연결
	NewActor->World = this;
	NewActor->Level = PersistentLevel;

	// 3. Transform 적용
	const FTransform SpawnTransform = Transform ? *Transform : FTransform::Identity;

	if (NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(SpawnTransform);
	}

	for (UActorComponent* Component : NewActor->GetComponents())
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			Scene.AddPrimitive(Primitive);
		else if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
			Scene.AddFogInfo(Fog->GetUUID(), Fog->GetFogInfo());
		else if (ULightComponentBase* Light = Cast<ULightComponentBase>(Component))
			Scene.AddLight(Light);
	}

	// 4. Level->Actors에 등록
	PersistentLevel->AddActor(NewActor);

	// 5. PlayList에 추가
	BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float FrameDeltaTime)
{
	TimeDilationManager.Tick(FrameDeltaTime);

	const float DeltaTime = FrameDeltaTime * TimeDilationManager.GetTimeDilation();
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	// 에디터 월드면 더이상 틱을 돌리지 않는다.
	if (WorldType != EWorldType::Editor)
	{
		// 지난 Frame 명령 버리고 새로 채우기
		HUD.BeginFrame();

		// 처음 BeginPlay는 GameMode::StartPlay → NotifyBeginPlay가 한꺼번에 보낸다.
		// 여기서는 그 뒤에 스폰된 액터만 받는다.
		while (bBegunPlay && !BeginPlayList.IsEmpty())
		{
			BeginPlayList.Peek()->DispatchBeginPlay();
			BeginPlayList.Dequeue();
		}

		// 액터 Tick보다 먼저 입력을 처리해 스크립트가 이번 프레임 입력으로 움직이게 한다 (UE PlayerController::PlayerTick).
		// 바인딩 안에서 컨트롤러가 늘거나 줄 수 있어 사본을 돈다.
		{
			const TArray<APlayerController*> Controllers = PlayerControllers;
			for (APlayerController* Controller : Controllers)
			{
				if (::IsValid(Controller))
					Controller->ProcessPlayerInput();
			}
		}

		// 시퀀스는 액터 Tick보다 먼저 값을 써 둔다 (UE도 TG_PrePhysics 전에 시퀀스를 평가한다).
		TickSequencePlayers(DeltaTime);

		{
			SCOPE_CYCLE_COUNTER(STAT_ActorTick);
			// 모든 Actor를 도는 대신 등록된 Tick 함수(메인 카메라 포함)만 실행한다.
			TickTaskManager.RunAllTickGroups(DeltaTime);
		}

		// 이동이 다 끝난 최종 위치로 판정한다.
		UpdatePendingOverlaps();

		// Coroutine
		TickScriptCoroutines(DeltaTime);

		// 이번 프레임의 스폰(겹침 이벤트·스크립트)까지 받은 뒤, 지워지기 전에 이펙트 수명을 정리한다.
		EffectManager.Tick(DeltaTime);


		// 게임플레이 이동이 다 끝난 최종 위치로 카메라를 갱신한다.
		// 카메라 연출(쉐이크·블렌드)은 히트스톱·슬로모 중에도 흘러야 하므로 배율을 곱하지 않은 시간을 쓴다
		{
			const TArray<APlayerController*> Controllers = PlayerControllers;
			for (APlayerController* Controller : Controllers)
				if (::IsValid(Controller))
					Controller->UpdatePlayerCameraManager(FrameDeltaTime);
		}

	}

	// Frame 끝에서 Destroy 된 객체 실제로 지우기
	FlushPendingKill();

}

void UWorld::MarkOverlapDirty(UPrimitiveComponent* Component)
{
	// 에디터에서 기즈모로 끌 때는 게임 이벤트를 내지 않는다 (UE도 PIE·게임에서만 낸다).
	if (WorldType == EWorldType::Editor || !Component)
		return;
	if (PendingOverlapUpdates.Find(Component) == INDEX_NONE)
		PendingOverlapUpdates.Add(Component);

	// 위치·크기가 바뀌었으니 BVH에 저장된 경계는 더 이상 맞지 않는다
	UShapeComponent* Shape = Cast<UShapeComponent>(Component);
	if (Shape && !Shape->bMovedSinceShapeRefit && Shapes.Find(Shape) != INDEX_NONE)
	{
		Shape->bMovedSinceShapeRefit = true;
		MovedShapes.Add(Shape);
	}
}

void UWorld::CancelOverlapUpdate(UPrimitiveComponent* Component)
{
	const int32 Index = PendingOverlapUpdates.Find(Component);
	if (Index != INDEX_NONE)
		PendingOverlapUpdates.RemoveAtSwap(Index);

	// 처리 중인 목록은 순회 인덱스가 밀리지 않게 지우지 않고 비워만 둔다.
	const int32 InProgressIndex = OverlapUpdatesInProgress.Find(Component);
	if (InProgressIndex != INDEX_NONE)
		OverlapUpdatesInProgress[InProgressIndex] = nullptr;
}

void UWorld::UpdatePendingOverlaps()
{
	// 이동이 다 끝났으니 BVH 경계를 한 번 맞춘다.
	RefreshShapeBVH();

	// 이벤트 처리 중에 다시 움직인 컴포넌트는 다음 Tick 목록에 들어가도록 먼저 옮겨 비운다.
	OverlapUpdatesInProgress = std::move(PendingOverlapUpdates);
	PendingOverlapUpdates.Empty();

	for (int32 i = 0; i < OverlapUpdatesInProgress.Num(); ++i)
	{
		// 앞 컴포넌트의 이벤트에서 뒤 컴포넌트가 삭제되면 CancelOverlapUpdate가 nullptr로 바꿔둔다.
		if (UPrimitiveComponent* Component = OverlapUpdatesInProgress[i])
			Component->UpdateOverlaps();
	}
	OverlapUpdatesInProgress.Empty();
}

void UWorld::OverlapMulti(UShapeComponent* Shape, TArray<UPrimitiveComponent*>& OutOverlaps)
{
	OutOverlaps.Empty();
	if (!Shape)
		return;

	// 넓은 단계로 AABB가 겹치는 것만 추린 뒤 정밀 판정
	TArray<UShapeComponent*> Candidates;
	GatherShapesInBounds(Shape->CalcBounds(), Shape, Candidates);
	for (UShapeComponent* Other : Candidates)
	{
		if (ShapeCollision::OverlapShapes(Shape, Other))
			OutOverlaps.Add(Other);
	}
}

void UWorld::GatherShapesInBounds(const FBox& Bounds, const UShapeComponent* Ignore, TArray<UShapeComponent*>& OutShapes)
{
	OutShapes.Empty();

	// 지워진 Shape가 낡은 BVH에서 나오지 않도록 목록이 바뀌었으면 먼저 다시 짓는다
	if (bShapeListChanged)
		RefreshShapeBVH();

	auto Intersects = [&Bounds](const FBox& OtherBounds)
	{
		return !(Bounds.Max.X < OtherBounds.Min.X || Bounds.Min.X > OtherBounds.Max.X
			|| Bounds.Max.Y < OtherBounds.Min.Y || Bounds.Min.Y > OtherBounds.Max.Y
			|| Bounds.Max.Z < OtherBounds.Min.Z || Bounds.Min.Z > OtherBounds.Max.Z);
	};

	// 1. 가만히 있던 Shape는 BVH로 추린다. 움직인 Shape는 저장된 경계가 낡았으니 건너뛴다.
	ShapeBVH.Query(Intersects, [&](UShapeComponent* Other)
	{
		if (Other != Ignore && !Other->bMovedSinceShapeRefit)
			OutShapes.Add(Other);
	});

	// 2. 마지막 Refit 뒤에 움직인 Shape는 지금 경계로 하나씩 본다
	for (UShapeComponent* Other : MovedShapes)
	{
		if (Other != Ignore && Intersects(Other->CalcBounds()))
			OutShapes.Add(Other);
	}
}

FBox UWorld::GetShapeBVHBounds(UShapeComponent* const& Shape)
{
	return Shape->CalcBounds();
}

void UWorld::RefreshShapeBVH()
{
	if (bShapeListChanged)
		ShapeBVH.Build(std::span(Shapes.GetData(), Shapes.Num()));
	else if (!MovedShapes.IsEmpty())
		ShapeBVH.Refit();

	for (UShapeComponent* Shape : MovedShapes)
		Shape->bMovedSinceShapeRefit = false;
	MovedShapes.Empty();
	bShapeListChanged = false;
}

void UWorld::AddShape(UShapeComponent* Shape)
{
	// PIE 복제 때는 AddComponent와 액터 BeginPlay에서 BeginPlay가 두 번 불린다
	if (Shape && Shapes.Find(Shape) == INDEX_NONE)
	{
		Shapes.Add(Shape);
		bShapeListChanged = true;
	}
}

void UWorld::RemoveShape(UShapeComponent* Shape)
{
	const int32 Index = Shapes.Find(Shape);
	if (Index != INDEX_NONE)
	{
		Shapes.RemoveAtSwap(Index);
		bShapeListChanged = true;
	}

	const int32 MovedIndex = MovedShapes.Find(Shape);
	if (MovedIndex != INDEX_NONE)
	{
		Shape->bMovedSinceShapeRefit = false;
		MovedShapes.RemoveAtSwap(MovedIndex);
	}
}

void UWorld::ClearWorld()
{
	// 풀 액터는 아래에서 다른 액터와 함께 지워진다. 그 전에 참조를 끊는다.
	EffectManager.Shutdown();
	FlushPendingKill();
	ScriptInstances.Empty();

	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}

	// 모든 액터가 함께 지워지니 겹침 이벤트 없이 대기 목록만 버린다.
	PendingOverlapUpdates.Empty();
	Shapes.Empty();
	MovedShapes.Empty();
	ShapeBVH.Clear();
	bShapeListChanged = true;

	// GameMode·GameState도 Level의 액터라 아래에서 함께 지워진다.
	AuthorityGameMode = nullptr;
	GameState = nullptr;
	PlayerControllers.Empty();

	// 액터를 지우기 전에 렌더 프록시와 틱 등록부터 푼다. ClearActors는 액터를 delete만 하므로,
	// 그대로 두면 지워진 컴포넌트를 가리키는 프록시가 FScene에 남아 다음 프레임에 터진다.
	Scene.RemoveAllPrimitives();
	Scene.RemoveAllFogInfo();
	Scene.RemoveAllLights();
	for (ULevel* Level : Levels)
	{
		for (AActor* Actor : Level->Actors)
			if (Actor)
				Actor->RegisterAllActorTickFunctions(false);
		Level->ClearActors();
	}
}

void UWorld::GatherRenderPackets(FRenderQueue& RenderQueue, const FLODViewContext* LODView, const FFrustumPlanes* Frustum, FRenderer* Renderer)
{
	// 멤버로 두어 매 프레임 용량을 재사용한다.
	RenderStats.Reset();
	VisibleProxies.Reset();

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// 컬링 단계에서는 프록시 포인터만 모으고, 컴포넌트 역참조(가시성 확인)는 어차피 컴포넌트를 읽는 Gather로 미룬다.
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy) { VisibleProxies.Add(Proxy); };

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&) { return EBVHCullResult::Inside; }, Visit);
		}
	}

	RenderStats.TotalPrimitives = Scene.Proxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
	const uint8* OccludedMask = nullptr;
	if (Renderer && LODView && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), *LODView))
		{
			const std::vector<uint8>& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
					if (!Occluded[i])
						VisibleProxies[Kept++] = VisibleProxies[i];
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	uint8* SlotDest = Renderer ? Renderer->BeginObjectConstants(MaxSlots) : nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	FTaskPool& Pool = FTaskPool::Get();
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, Pool.GetNumThreads() * 4);

	if (GatherChunks.Num() < ChunkCount)
		GatherChunks.SetNum(ChunkCount);     // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.

	for (uint32 c = 0; c < GatherChunks.Num(); ++c)   // 이번에 안 쓰는 조각의 묶음도 비워 둔다 (Renderer에 넘기지 않도록)
	{
		FGatherChunk& Chunk = GatherChunks[c];
		Chunk.Packets.Reset();               // 용량은 유지, 개수만 0
		Chunk.SlowPathIndices.Reset();
		std::fill(std::begin(Chunk.LODCounts), std::end(Chunk.LODCounts), 0u);
		std::fill(std::begin(Chunk.LODTriangles), std::end(Chunk.LODTriangles), 0ull);
		for (FStaticDrawGroup& Group : Chunk.Groups)
			Group.Items.clear();             // 용량은 유지
		Chunk.StaticDrawCount = 0;
	}

	// Renderer가 있으면 불투명 스태틱 메시는 패킷 대신 조각별 묶음에 작은 항목으로 넣는다.
	// 동일 바인딩의 개별 드로우를 묶어 패킷 복사와 항목별 정렬을 줄인다.
	const bool bStaticGroups = Renderer != nullptr;
	if (Renderer)
		Renderer->ResetStaticDrawGroups();

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		if (LODView)
		{
			LODInputs.Reserve(VisibleProxies.Num());
			for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
				LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
			SelectLODs(LODInputs, *LODView, SelectedLODs);
		}
		Pool.ParallelFor(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD)
								return Last;
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()) continue;

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = LODView ? SelectedLODs[VisibleIndex] : 0;
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (SlotDest)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						WriteObjectSlot(SlotDest, VisibleIndex, Proxy->GetLocalToWorld(), Proxy->GetNormalMatrix(), Proxy->ReceivesShadow());
						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->BlendState == EBlendState::Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.Mesh = Mesh;
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				}
			});

		// 묶음은 World 메모리 그대로 Renderer에 넘긴다 (복사 없음). 정렬은 Renderer가 묶음 단위로 한다.
		uint32 StaticDraws = 0;
		if (Renderer)
		{
			for (uint32 c = 0; c < ChunkCount; ++c)
			{
				StaticDraws += GatherChunks[c].StaticDrawCount;
				for (const FStaticDrawGroup& Group : GatherChunks[c].Groups)
					if (!Group.Items.empty())
						Renderer->AddStaticDrawGroup(&Group);
			}
		}

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
			TotalPackets += GatherChunks[c].Packets.Num();

		RenderQueue.Reserve(TotalPackets + 256);             // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
			if (GatherChunks[c].Packets.Num() > 0)
				RenderQueue.Append(GatherChunks[c].Packets);  // 조각 순서대로 이어 붙이기

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
					continue;

				const uint32 FirstNew = RenderQueue.Num();

				// 프록시 캐시가 없는 스태틱 메시는 기존처럼 LOD를 골라 제출하고, 그 외는 컴포넌트에 맡긴다.
				UStaticMeshComponent* StaticMeshComponent = LODView ? Cast<UStaticMeshComponent>(Primitive) : nullptr;
				if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
				{
					const uint32 LOD = SelectedLODs[VisibleIndex];
					StaticMeshComponent->SubmitToRenderQueue(RenderQueue, LOD);
				}
				else
				{
					Primitive->SubmitToRenderQueue(RenderQueue);
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					if (!SlotDest || NextExtraSlot >= MaxSlots) break;
					FRenderPacket& Packet = RenderQueue[p];
					if (Packet.Proxy)
					{
						WriteObjectSlot(SlotDest, NextExtraSlot, Packet.Proxy->GetLocalToWorld(), Packet.Proxy->GetNormalMatrix(), Packet.Proxy->ReceivesShadow());
					}
					else
					{
						// 프록시 없는 패킷(빌보드 등)은 캐시가 없으므로 여기서 계산한다.
						const FMatrix& Model = Packet.Model ? *Packet.Model : FMatrix::Identity;
						WriteObjectSlot(SlotDest, NextExtraSlot, Model, MakeNormalMatrix(Model));
					}
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}

		if (SlotDest) Renderer->EndObjectConstants();
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles) RenderStats.Triangles += T;
	}
}

//void UWorld::GatherRenderPackets(TArray<FRenderPacket>& RenderArray, const FLODViewContext* LODView, const FFrustumPlanes* Frustum)
//{
//	for (TObjectIterator<UPrimitiveComponent> Itr; Itr; ++Itr)
//	{
//		if (!*Itr || !Itr->IsVisible())
//			continue;
//
//		if (Frustum)
//		{
//			const FBox Box = Itr->CalcBounds();
//			const FAABB Bounds{
//				(Box.Min + Box.Max) * 0.5f,
//				(Box.Max - Box.Min) * 0.5f
//			};
//			if (!IsAABBInFrustum(Bounds, *Frustum))
//				continue;
//		}
//
//		if (LODView)
//		{
//			if (auto* Component = Cast<UStaticMeshComponent>(*Itr))
//			{
//				if (UStaticMesh* Mesh =
//					Component->GetStaticMesh())
//				{
//					const uint32 LOD = SelectStaticMeshLOD(
//						*Mesh,
//						Component->GetWorldMatrix(),
//						*LODView);
//
//					Component->SubmitToRenderQueue(RenderQueue, LOD);
//					continue;
//				}
//			}
//		}
//
//		Itr->SubmitToRenderQueue(RenderQueue);
//	}
//}

// 메인 카메라 생성
void UWorld::CreateMainCamera()
{
	if (MainCamera)
		return;

	MainCamera = NewObject<ACameraActor>();

	if (!MainCamera)
	{
		HTR_LOG(Error, "Failed to create MainCamera");
		return;
	}

	MainCamera->World = this;
	MainCamera->Level = nullptr;
	MainCamera->GetCameraComponent()->SetRelativeLocation(FVector(-5.0f, -5.0f, 5.0f));

	// 메인 카메라는 Level에 속하지 않아 BeginPlay를 거치지 않으므로 여기서 등록한다.
	MainCamera->RegisterAllActorTickFunctions(true);
}

void UWorld::SetMainCamera(ACameraActor* Camera)
{
	if (MainCamera == Camera)
		return;

	if (MainCamera)
		MainCamera->RegisterAllActorTickFunctions(false);

	MainCamera = Camera;

	if (MainCamera)
	{
		MainCamera->World = this;
		MainCamera->RegisterAllActorTickFunctions(true);
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

bool UWorld::DestroyActor(AActor* Actor)
{
	// 이미 Destroy 된 Actor면 재처리 하지 않기
	if (!Actor || Actor->IsPendingKill() )
		return false;

	ULevel* Level = Actor->GetLevel();

	if (!Level)
		return false;

	// 0. 아직 컴포넌트와 프록시가 살아 있을 때 EndPlay를 알린다.
	Actor->DispatchEndPlay();

	if (Actor == AuthorityGameMode)
		AuthorityGameMode = nullptr;
	if (Actor == GameState)
		GameState = nullptr;

	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 2. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	// 3. 프록시 제거
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->ClearComponentOverlaps();
			if (UShapeComponent* Shape = Cast<UShapeComponent>(Primitive))
				RemoveShape(Shape);
			Scene.RemovePrimitive(Primitive);
		}
		else if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
		{
			Scene.RemoveFogInfo(Fog->GetUUID());
		}
		else if (ULightComponentBase* Light = Cast<ULightComponentBase>(Component))
		{
			Scene.RemoveLight(Light);
		}
	}

	Actor->RegisterAllActorTickFunctions(false);

	/*
	// 4. Actor 삭제
	delete Actor;
	*/

	// 바로 delete 하지 않기
	// Component 파괴 예정으로 표시 ( Actor 소멸자가 delete 할 예정 )
	for (UActorComponent* Component : Actor->GetComponents())
		if (Component) Component->MarkPendingKill();
	
	DeferDelete(Actor);

	HTR_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

// Delete 예정임을 표시
void UWorld::DeferDelete(UObject* Object)
{
	if (!Object || Object->IsPendingKill())
		return;

	Object->MarkPendingKill();
	PendingKillObjects.Add(Object);
}

void UWorld::FlushPendingKill()
{
	// 소멸자 안에서 또 Destroy가 불려 목록이 늘어날 수 있으므로, 목록을 옮겨 놓고 지운 뒤 다시 확인
	while (PendingKillObjects.Num() > 0)
	{
		TArray<UObject*> ToDelete = std::move(PendingKillObjects);
		PendingKillObjects.Empty();

		for (UObject* Object : ToDelete)
			delete Object;
	}
}

void UWorld::RegisterScriptInstance(FLuaScriptInstance* Instance)
{
	if (Instance && ScriptInstances.Find(Instance) == INDEX_NONE)
		ScriptInstances.Add(Instance);
}

void UWorld::UnregisterScriptInstance(FLuaScriptInstance* Instance)
{
	const int32 Index = ScriptInstances.Find(Instance);
	if (Index == INDEX_NONE)
		return;

	// 순회 중 ( 코루틴 안에서 Destroy ) 이면 Index 가 밀리지 않게 비워만 두고, 순회가 끝난 뒤 정리
	if (bTickingScripts)
		ScriptInstances[Index] = nullptr;
	else
		ScriptInstances.RemoveAtSwap(static_cast<uint32>(Index));
}

int32 UWorld::ReloadScripts(const FString& Path)
{
	// 코루틴 순회 도중(스크립트 안)에서 불리면 순회 중인 Instance를 바꾸게 되므로 막는다
	if (bTickingScripts)
	{
		HTR_LOG(Warning, "[Lua] 스크립트 실행 중에는 리로드할 수 없습니다 : {}", Path);
		return 0;
	}

	// 상대/절대 경로, 구분자 차이가 있어도 같은 파일이면 같게 본다
	auto Normalize = [](const FString& InPath)
	{
		std::error_code Ec;
		std::filesystem::path Absolute = std::filesystem::absolute(InPath, Ec);
		return (Ec ? std::filesystem::path(InPath) : Absolute).lexically_normal();
	};

	const std::filesystem::path Target = Normalize(Path);
	int32 Reloaded = 0;

	// 스크립트 최상위 코드가 Spawn / Destroy를 부를 수 있으므로 Coroutine 순회와 같은 방식으로 돈다
	bTickingScripts = true;

	const int32 Count = ScriptInstances.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		FLuaScriptInstance* Instance = ScriptInstances[i];
		if (Instance && Normalize(Instance->GetPath()) == Target && Instance->Reload())
			++Reloaded;
	}

	bTickingScripts = false;

	for (int32 i = ScriptInstances.Num() - 1; i >= 0; --i)
	{
		if (!ScriptInstances[i])
			ScriptInstances.RemoveAt(i);
	}

	return Reloaded;
}

void UWorld::TickScriptCoroutines(float DeltaTime)
{
	bTickingScripts = true;

	// 순회 중 새로 등록된 Instance 는 다음 프레임부터 돈다
	const int32 Count = ScriptInstances.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (FLuaScriptInstance* Instance = ScriptInstances[i])
			Instance->TickCoroutines(DeltaTime);
	}

	bTickingScripts = false;

	// 순회 중 비워 둔 칸을 정리한다
	for (int32 i = ScriptInstances.Num() - 1; i >= 0; --i)
	{
		if (!ScriptInstances[i])
			ScriptInstances.RemoveAt(i);
	}
}

void UWorld::RegisterSequencePlayer(ULevelSequencePlayer* Player)
{
	if (Player && SequencePlayers.Find(Player) == INDEX_NONE)
		SequencePlayers.Add(Player);
}

void UWorld::UnregisterSequencePlayer(ULevelSequencePlayer* Player)
{
	const int32 Index = SequencePlayers.Find(Player);
	if (Index == INDEX_NONE)
		return;

	// 순회 중이면 인덱스가 밀리지 않게 비워만 두고, 순회가 끝난 뒤 정리한다
	if (bTickingSequences)
		SequencePlayers[Index] = nullptr;
	else
		SequencePlayers.RemoveAt(Index);
}

void UWorld::TickSequencePlayers(float DeltaTime)
{
	bTickingSequences = true;

	// 순회 중 새로 등록된 플레이어는 다음 프레임부터 돈다
	const int32 Count = SequencePlayers.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (ULevelSequencePlayer* Player = SequencePlayers[i])
			Player->Tick(DeltaTime);
	}

	bTickingSequences = false;

	for (int32 i = SequencePlayers.Num() - 1; i >= 0; --i)
	{
		if (!SequencePlayers[i])
			SequencePlayers.RemoveAt(i);
	}
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
	FBillboardTraceTransform ResolveBillboard, const void* ViewContext)
{
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
			{
				if (!Proxy->IsVisible())
					return false;

				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};

				float T = InOutNearestT;
				if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
					return false;

				OutHit.HitComponent = Proxy->GetComponent();
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;

			if (!Component || !Component->IsVisible())
				return false;

			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				if (!ResolveBillboard)
				{
					FHitResult Hit;
					if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
						Hit.Distance >= InOutNearestT)
					{
						return false;
					}

					OutHit = Hit;
					InOutNearestT = Hit.Distance;
					return true;
				}

				const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

				const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

				float T = InOutNearestT;
				if (!Billboard->LineTraceComponentLocal(LocalRay, T))
				{
					return false;
				}

				OutHit.HitComponent = Billboard;
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!Component->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT) { return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT) { return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
	if (WorldType == EWorldType::Editor)
		return;

	if (!AuthorityGameMode)
	{
		HTR_LOG(Error, "[World] BeginPlay: no GameMode; actors will not begin play (call SetGameMode first)");
		return;
	}
	AuthorityGameMode->StartPlay();
}

void UWorld::NotifyBeginPlay()
{
	if (bBegunPlay)
		return;

	// 액터 BeginPlay(스크립트 포함)에서 이펙트를 스폰할 수 있도록 먼저 준비한다.
	// 풀 액터는 PersistentLevel에 들어가 아래 순회에서 함께 BeginPlay를 받는다.
	if (IsGameWorld())
		EffectManager.Init(this);

	// BeginPlay 안에서 스폰된 액터도 같은 순회에서 받도록 매번 크기를 다시 읽는다.
	for (ULevel* Level : Levels)
	{
		if (!Level)
			continue;

		for (int32 i = 0; i < Level->Actors.Num(); ++i)
		{
			if (AActor* Actor = Level->Actors[i])
				Actor->DispatchBeginPlay();
		}
	}

	// 위에서 모두 받았으므로 대기열을 버린다. 이후 스폰은 Tick에서 처리한다.
	while (!BeginPlayList.IsEmpty())
		BeginPlayList.Dequeue();

	bBegunPlay = true;
}

void UWorld::AddPlayerController(APlayerController* Controller)
{
	if (Controller && PlayerControllers.Find(Controller) == INDEX_NONE)
		PlayerControllers.Add(Controller);
}

void UWorld::RemovePlayerController(APlayerController* Controller)
{
	const int32 Index = PlayerControllers.Find(Controller);
	if (Index != INDEX_NONE)
		PlayerControllers.RemoveAt(static_cast<uint32>(Index));
}

APlayerController* UWorld::GetPlayerController(int32 PlayerIndex) const
{
	for (APlayerController* Controller : PlayerControllers)
	{
		if (Controller && Controller->GetPlayerIndex() == PlayerIndex)
			return Controller;
	}
	return nullptr;
}

bool UWorld::SetGameMode()
{
	if (WorldType == EWorldType::Editor || AuthorityGameMode)
		return false;
	if (!OwningGameInstance)
	{
		HTR_LOG(Error, "[World] SetGameMode: no GameInstance (world context was not created for play)");
		return false;
	}

	AuthorityGameMode = OwningGameInstance->CreateGameModeForWorld(this);
	if (!AuthorityGameMode)
	{
		HTR_LOG(Error, "[World] failed to spawn GameMode actor");
		return false;
	}
	AuthorityGameMode->PreInitializeComponents();
	return true;
}

void UWorld::EndPlay()
{
	// EndPlay 안에서 다른 액터를 Destroy하면 배열이 줄어들 수 있어 매번 크기를 다시 읽는다.
	for (ULevel* Level : Levels)
	{
		if (!Level)
			continue;

		for (int32 i = 0; i < Level->Actors.Num(); ++i)
		{
			if (AActor* Actor = Level->Actors[i])
				Actor->DispatchEndPlay();
		}
	}
	bBegunPlay = false;
	EffectManager.Shutdown();
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->Init();

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld * InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);

	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// 아직 필요 없는애들
	//TArray<UObject*> ObjectsToFixReferences;
	//TMap<UObject*, UObject*> ReplacementMap;

	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
				SceneComp->MarkTransformDirty();        // 부착/상대 Transform이 바뀌었으니 다시 계산

			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
				Scene.AddPrimitive(Primitive);
			else if (UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Component))
				Scene.AddFogInfo(Fog->GetUUID(), Fog->GetFogInfo());
			else if (ULightComponentBase* Light = Cast<ULightComponentBase>(Component))
				Scene.AddLight(Light);
		}

		BeginPlayList.Enqueue(Actor);   // Tick 등록은 BeginPlay에서 함
	}

	Scene.BuildBVH();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));

	// (선택) 메인 카메라는 레벨에 속하지 않아 따로 쓴다. PIE 시작 시점이나 에디터 시점을 복원할 때 쓸 수 있다.
	// MainCamera->GetCameraComponent()->Serialize(Record.EnterRecord("MainCamera"));
}
