#pragma once

#include "Collision/ShapeCollision.h"
#include "Math/Frustum.h"
#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Core/Types.h"
#include "Containers/Array.h"
#include "Component/DecalComponent.h"

class FScene;
class UDecalComponent;
class UMaterial;
class FPrimitiveSceneProxy;
class FVertexBuffer;

// Mesh 방식 데칼 정점. DecalMesh.hlsl의 VSInput(POSITION, NORMAL)과 순서·크기가 같아야 한다.
// 노멀은 받는 메시의 정점 노멀이라, 데칼 노멀맵을 박스가 아니라 표면 기준으로 얹을 수 있다.
struct FDecalMeshVertex
{
    FVector Position;
    FVector Normal;
};

struct FDeferredDecalSceneProxy
{
public:
    explicit FDeferredDecalSceneProxy(UDecalComponent* DecalComponent);
    ~FDeferredDecalSceneProxy();       // FVertexBuffer가 불완전 타입이라 cpp에서 지운다
    void UpdateFromComponent();       // 컴포넌트 → 스냅샷 복사
    UDecalComponent* GetComponent() { return Component; }
    FScene* GetScene() { return Scene; }

    // 셰이더용
    FMatrix      WorldToDecal;        // 박스를 -1~1로 만드는 역행렬 (DecalSize 스케일 포함)
    FMatrix      DecalToWorld;        // 박스 드로우, 디버그용
    UMaterial*   Material = nullptr;  // null이면 "DecalMaterial"로 대체 (UE Proxy처럼)
    FLinearColor DecalColor;
    float        FadeAlpha = 1.0f;        // 시간 Fade. 컴포넌트가 Tick마다 쓴다
    float        FadeScreenSize = 0.0f;   // 화면 크기 Fade 기준. 카메라마다 달라 렌더러가 뷰별로 계산한다
    EDecalBlendType BlendType = EDecalBlendType::Albedo;  // FScene이 이 값으로 그리기 목록을 나눈다
    int32        SortOrder = 0;
    bool         bVisible = true;

    // 컬링용 (3~4단계)
    FCollisionBox OBB;                // Narrow Phase: ShapeCollision::BoxBox
    FAABB         Bounds;             // Broad Phase: Scene BVH Query

    TArray<FDecalMeshVertex> MeshVertices;            // 잘라 낸 삼각형 목록 (월드 위치와 받는 면의 월드 노멀, 3개씩)
    TUniquePtr<FVertexBuffer> MeshVertexBuffer;       // 위를 올린 GPU 버퍼
    bool bMeshDirty = true;

private:
    friend class FScene;
    UDecalComponent* Component = nullptr;
    FScene* Scene = nullptr;
    bool bQueuedForUpdate = false;
};