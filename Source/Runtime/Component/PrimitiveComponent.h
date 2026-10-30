#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Core/Delegates/Delegate.h"

class FPrimitiveSceneProxy;
class AActor;
class UShapeComponent;
class UWorld;

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane,

	Particle,
	ParticleQuad
};

class UPrimitiveComponent :public USceneComponent
{
	DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
	REFLECT_START(ClassName)
		PROPERTY(bVisible)
		PROPERTY(bCastShadow)
		PROPERTY(bReceiveShadow)
		FUNCTION(GetRenderMesh)
		FUNCTION(GetNumMaterials)
		FUNCTION(GetMaterial)
		FUNCTION(SetMaterial)
		FUNCTION(CastsShadow)
		FUNCTION(SetCastShadow)
		FUNCTION(ReceivesShadow)
		FUNCTION(SetReceiveShadow)
		FUNCTION(IsVisible)
		FUNCTION(SetVisible)
		PROPERTY(bGenerateOverlapEvents)
		PROPERTY(bBlockComponent)
	REFLECT_END()
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	// Todo: subuv
	//void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	bool CastsShadow() const { return bCastShadow; }
	void SetCastShadow(bool bValue) { bCastShadow = bValue; MarkRenderStateDirty(); }
	bool ReceivesShadow() const { return bReceiveShadow; }
	void SetReceiveShadow(bool bValue) { bReceiveShadow = bValue; MarkRenderStateDirty(); }
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	bool IsVisible() const { return bVisible; }
	void SetVisible(bool bInVisible)
	{
		bVisible = bInVisible;
		MarkRenderStateDirty();
	}

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty();

	virtual void OnTransformDirty() override;

	// bSweep이고 이 컴포넌트가 막는 Shape면 가는 길을 검사해 처음 막히는 곳에서 멈춘다.
	// 회전은 검사하지 않고 끝에서 그대로 적용한다 (UE도 Sweep은 이동만 본다).
	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	bool GetGenerateOverlapEvents() const { return bGenerateOverlapEvents; }
	bool IsBlockComponent() const { return bBlockComponent; }

	// 현재 위치에서 겹친 컴포넌트를 다시 구해 기존 목록과 비교하고 Begin/End를 낸다.
	// 움직인 컴포넌트만 World가 Tick에서 불러준다
	void UpdateOverlaps();
	// 사라지기 직전에 부른다. 겹쳐 있던 상대 모두에게 End를 보내고 목록을 비운다.
	void ClearComponentOverlaps();

	const TArray<UPrimitiveComponent*>& GetOverlapInfos() const { return OverlappingComponents; }
	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	bool IsOverlappingActor(const AActor* Other) const;

	// (자신, 상대 액터, 상대 컴포넌트)
	TMulticastDelegate<UPrimitiveComponent*, AActor*, UPrimitiveComponent*> OnComponentBeginOverlap;
	TMulticastDelegate<UPrimitiveComponent*, AActor*, UPrimitiveComponent*> OnComponentEndOverlap;
	// (자신, 상대 액터, 상대 컴포넌트, 충돌 정보). Sweep 이동이 bBlockComponent인 상대에 막혔을 때
	TMulticastDelegate<UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&> OnComponentHit;

protected:
	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);
	bool bVisible = true;
	bool bCastShadow = true;
	bool bReceiveShadow = true;
	// 겹침 시작·끝 이벤트를 받을지. 양쪽 모두 true여야 이벤트가 난다
	bool bGenerateOverlapEvents = true;
	// true면 Sweep 이동을 막고 Hit 이벤트를 낸다
	bool bBlockComponent = false;

	// 도형 크기처럼 판정 결과가 바뀌는 값을 고친 자식 클래스도 다음 Tick 갱신을 예약할 수 있게 연다.
	void MarkOverlapsDirty();

private:
	// 양쪽에 같은 값이 나와야 한다. 한쪽만 겹친다고 보면 매 프레임 Begin/End가 번갈아 난다.
	bool ShouldGenerateOverlapWith(const UPrimitiveComponent* Other) const;
	// 양쪽 목록을 함께 고치고 양쪽에 이벤트를 낸다.
	void BeginComponentOverlap(UPrimitiveComponent* Other);
	void EndComponentOverlap(UPrimitiveComponent* Other);

	// 둘 다 bBlockComponent일 때만 Sweep이 막힌다 (ShouldGenerateOverlapWith와 반대 조건).
	bool ShouldBlockWith(const UPrimitiveComponent* Other) const;
	// Shape를 Delta만큼 옮기는 길에 처음 막히는 곳을 찾는다. 막히면 OutHit을 채우고 true.
	bool SweepBlocking(UShapeComponent* Shape, UWorld* World, const FVector& Delta, FHitResult& OutHit) const;
	// Shape를 Delta만큼 옮기는 길에 스치는 겹침 상대를 PendingSweepOverlaps에 모은다 (UE의 Sweep PendingOverlaps).
	void GatherSweepOverlaps(UShapeComponent* Shape, UWorld* World, const FVector& Delta);
	// 막혔을 때 양쪽 액터와 컴포넌트에 Hit을 알린다. 상대에게는 뒤집은 Hit을 준다.
	void DispatchBlockingHit(const FHitResult& Hit);

	friend class FScene;
	// AActor::AddActorWorldOffset가 자식 Shape로 Sweep하고 Hit을 알린다
	friend class AActor;
	FPrimitiveSceneProxy* SceneProxy = nullptr;

	// 지금 겹쳐 있는 상대. 상대 목록에도 항상 this가 들어 있다.
	TArray<UPrimitiveComponent*> OverlappingComponents;
	// 이번 프레임 Sweep 경로에서 스친 상대. UpdateOverlaps가 Begin을 낸 뒤 비우고, 최종 위치에서 안 겹치면 거기서 End가 난다.
	TArray<UPrimitiveComponent*> PendingSweepOverlaps;
	// BeginPlay 전(스폰 도중)에는 겹침을 계산하지 않는다.
	bool bHasBegunPlay = false;
};