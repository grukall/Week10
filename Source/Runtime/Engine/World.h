#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MovementComponent.h"
#include "Component/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Engine/EngineTypes.h"
#include "Engine/HUD.h"
#include "Engine/EffectManager.h"
#include "Engine/TimeDilationManager.h"

//class ACameraActor;
class ULevel;
class UBillboardComponent;
class UShapeComponent;
class UGameInstance;
class AGameModeBase;
class AGameStateBase;
class APlayerController;
class FLuaScriptInsatance;
class ULevelSequencePlayer;

struct FLODViewContext;

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	void Reset() { *this = FRenderStats(); }
};

class UWorld : public UObject
{
	DECLARE_CLASS(UWorld, UObject)

public:
	UWorld() = default;
	virtual ~UWorld();

	bool Init();
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);

	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float DeltaTime);

	void ClearWorld();

	void GatherRenderPackets(FRenderQueue& RenderArray, const FLODViewContext* LODView = nullptr, const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr);

	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }

	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);

	// 파괴 예정으로 표시. Frame 끝에서 delete
	void DeferDelete(UObject* Object);
	// 실제로 delete. Tick 끝과 ClearWorld 에서 호출
	void FlushPendingKill();

	// Coroutine을 World가 매 Frame 진행하도록 등록 / 해제 ( BeginPlay, EndPlay에서 호출 )
	void RegisterScriptInstance(FLuaScriptInstance* Instance);
	void UnregisterScriptInstance(FLuaScriptInstance* Instance);

	// 시퀀스 플레이어를 World가 매 Frame 진행하도록 등록 / 해제 ( 플레이어의 Initialize와 소멸자에서 호출 )
	// UE처럼 액터 Tick보다 먼저 돌려, 같은 Frame의 게임플레이 코드가 시퀀스가 적용된 값을 보게 한다.
	void RegisterSequencePlayer(ULevelSequencePlayer* Player);
	void UnregisterSequencePlayer(ULevelSequencePlayer* Player);

	// Path 스크립트를 쓰는 모든 Instance를 다시 로드한다 ( PIE 중 Hot Reload ). 다시 로드한 개수를 반환
	int32 ReloadScripts(const FString& Path);

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix(*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
		FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr);

	// 플레이 월드 준비가 끝나면 엔진이 한 번 부른다. GameMode->StartPlay()로 넘긴다.
	void BeginPlay();
	void EndPlay();

	// UE의 AWorldSettings::NotifyBeginPlay. GameState::HandleBeginPlay가 부른다.
	// Level의 모든 액터에 BeginPlay를 보내고 bBegunPlay를 켠다. 그 뒤 스폰된 액터는 Tick에서 BeginPlay를 받는다.
	void NotifyBeginPlay();
	bool HasBegunPlay() const { return bBegunPlay; }

	// FWorldContext::SetCurrentWorld가 연결한다. 에디터 월드는 nullptr.
	UGameInstance* GetGameInstance() const { return OwningGameInstance; }
	void SetGameInstance(UGameInstance* InGameInstance) { OwningGameInstance = InGameInstance; }

	// GameInstance에게 GameMode를 만들게 하고, GameMode는 GameState를 스폰한다.
	// 플레이 월드를 준비한 뒤(씬 로드·PIE 복제 직후) 한 번 부른다. 이미 있으면 아무것도 하지 않는다.
	bool SetGameMode();
	AGameModeBase* GetAuthGameMode() const { return AuthorityGameMode; }

	AGameStateBase* GetGameState() const { return GameState; }
	void SetGameState(AGameStateBase* InGameState) { GameState = InGameState; }

	// GameMode가 스폰할 때 넣고, PlayerController가 EndPlay에서 뺀다. 찾을 때는 각자의 PlayerIndex로 찾는다.
	void AddPlayerController(APlayerController* Controller);
	void RemovePlayerController(APlayerController* Controller);
	// 없으면 nullptr. 새로 만들려면 GameMode의 GetOrCreatePlayerController를 쓴다.
	APlayerController* GetPlayerController(int32 PlayerIndex) const;
	const TArray<APlayerController*>& GetPlayerControllers() const { return PlayerControllers; }

	// 움직인 컴포넌트를 표시해두면 Tick 끝에서 한 번 UpdateOverlaps를 불러준다. 에디터 월드에서는 무시한다.
	void MarkOverlapDirty(UPrimitiveComponent* Component);
	// 사라질 컴포넌트를 대기 목록에서 뺀다.
	void CancelOverlapUpdate(UPrimitiveComponent* Component);
	// Shape와 모양이 겹치는 이 월드의 Shape를 모두 모은다. 이벤트 규칙은 호출하는 쪽이 거른다 (UE의 ComponentOverlapMulti).
	void OverlapMulti(UShapeComponent* Shape, TArray<UPrimitiveComponent*>& OutOverlaps);
	// 넓은 단계. 월드 AABB가 Bounds와 겹치는 이 월드의 Shape를 모은다 (Ignore 제외). Overlap과 Sweep이 같이 쓴다.
	void GatherShapesInBounds(const FBox& Bounds, const UShapeComponent* Ignore, TArray<UShapeComponent*>& OutShapes);
	// Shape는 BeginPlay에서 등록하고, 컴포넌트·액터가 지워질 때 Scene.RemovePrimitive와 같이 뺀다.
	void AddShape(UShapeComponent* Shape);
	void RemoveShape(UShapeComponent* Shape);

	FScene& GetScene() { return Scene; }

	FEffectManager& GetEffectManager() { return EffectManager; }

	FHUD& GetHUD() { return HUD; }
	const FHUD& GetHUD() const { return HUD; }

	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }

	const FRenderStats& GetRenderStats() const { return RenderStats; }

	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }
	bool IsGameWorld() const { return WorldType == EWorldType::PIE || WorldType == EWorldType::Game; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;


	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	FTimeDilationManager& GetTimeDilationManager() { return TimeDilationManager; }

	// 직접 설정하는 기본 배율. 슬로모·히트스톱은 이 값 위에 곱해진다.
	void SetTimeDilation(float InTimeDilation) { TimeDilationManager.SetBaseTimeDilation(InTimeDilation); }
	// 이번 프레임 게임 시간에 곱해지는 최종 배율
	float GetTimeDilation() const { return TimeDilationManager.GetTimeDilation(); }

	void StartSlomo(float TargetDilation, float Duration) { TimeDilationManager.StartSlomo(TargetDilation, Duration); }
	void StartHitStop(float Duration, float Dilation = 0.0f) { TimeDilationManager.StartHitStop(Duration, Dilation); }

private:
	EWorldType WorldType = EWorldType::None;

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets;              // 스태틱 묶음에 못 들어가는 것 (반투명 섹션, Renderer 없는 호출)
		TArray<uint32> SlowPathIndices;
		std::vector<FStaticDrawGroup> Groups;       // 불투명 스태틱 메시: (머티리얼, 메시, LOD)별 묶음. 항목만 매 프레임 비운다
		uint32 LastGroup = 0;                       // 바로 전 물체가 들어간 묶음 (연속한 물체는 대개 같은 묶음)
		uint32 StaticDrawCount = 0;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};

	TArray<FGatherChunk> GatherChunks;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;

	// Tick은 NotifyBeginPlay 이후 스폰된 액터에게만 BeginPlay를 보낸다. 그 전에 쌓인 것은 NotifyBeginPlay가 비운다.
	TQueue<AActor*> BeginPlayList;
	bool bBegunPlay = false;

	void UpdatePendingOverlaps();
	TArray<UPrimitiveComponent*> PendingOverlapUpdates;
	TArray<UPrimitiveComponent*> OverlapUpdatesInProgress;
	// 이 월드의 Shape만 모아둔다. 전역 순회(TObjectIterator)로 다른 월드까지 훑지 않도록
	TArray<UShapeComponent*> Shapes;

	// 넓은 단계용 BVH. 목록이 바뀌면 다시 짓고, 프레임마다 UpdatePendingOverlaps에서 한 번 Refit한다.
	// 그 사이 움직인 Shape는 BVH 경계가 낡았으니 MovedShapes에 따로 두고 현재 경계로 하나씩 검사한다.
	void RefreshShapeBVH();
	static FBox GetShapeBVHBounds(UShapeComponent* const& Shape);
	TBVH<UShapeComponent*> ShapeBVH{ &UWorld::GetShapeBVHBounds };
	TArray<UShapeComponent*> MovedShapes;
	bool bShapeListChanged = true;

	//메인 카메라
	ACameraActor* MainCamera = nullptr;

	// 소유하지 않는다. GameInstance는 FWorldContext가, GameMode·GameState는 Level이 지운다.
	UGameInstance* OwningGameInstance = nullptr;
	AGameModeBase* AuthorityGameMode = nullptr;
	AGameStateBase* GameState = nullptr;
	TArray<APlayerController*> PlayerControllers;

	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;
	FHUD HUD;
	// 게임 월드에서만 NotifyBeginPlay 때 초기화된다.
	FEffectManager EffectManager;

	// GatherRenderPackets가 매 프레임 채우는 컬링 결과. 용량을 재사용한다.
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;

	FRenderStats RenderStats;

	// Destroy 되었지만 아직 delete 하지 않은 객체 모아두는 배열 ( Frame 끝에서 지울 수 있도록 )
	TArray<UObject*> PendingKillObjects;

	void TickScriptCoroutines(float DeltaTime);

	// 비소유. Instance 는 Actor/Component 가 소유
	TArray<FLuaScriptInstance*> ScriptInstances;
	bool bTickingScripts = false;

	void TickSequencePlayers(float DeltaTime);

	// 비소유. 플레이어는 ALevelSequenceActor(또는 에디터 Sequencer)가 소유
	TArray<ULevelSequencePlayer*> SequencePlayers;
	bool bTickingSequences = false;

	FTimeDilationManager TimeDilationManager;
};
