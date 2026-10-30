#pragma once

#include "Collision/ShapeCollision.h"
#include "Math/Frustum.h"
#include "Math/Matrix.h"
#include "Math/Color.h"
#include "Core/Types.h"

class FScene;
class UDecalComponent;
class UMaterial;

struct FDeferredDecalSceneProxy
{
public:
    explicit FDeferredDecalSceneProxy(UDecalComponent* DecalComponent) : Component(DecalComponent)
    {

    }
    void UpdateFromComponent();       // 컴포넌트 → 스냅샷 복사
    UDecalComponent* GetComponent() { return Component; }
    FScene* GetScene() { return Scene; }

    // 셰이더용
    FMatrix      WorldToDecal;        // 박스를 -1~1로 만드는 역행렬 (DecalSize 스케일 포함)
    FMatrix      DecalToWorld;        // 박스 드로우, 디버그용
    UMaterial*   Material = nullptr;  // null이면 "DecalMaterial"로 대체 (UE Proxy처럼)
    FLinearColor DecalColor;
    float        FadeAlpha = 1.0f;
    int32        SortOrder = 0;
    bool         bVisible = true;

    // 컬링용 (3~4단계)
    FCollisionBox OBB;                // Narrow Phase: ShapeCollision::BoxBox
    FAABB         Bounds;             // Broad Phase: Scene BVH Query

private:
    friend class FScene;
    UDecalComponent* Component = nullptr;
    FScene* Scene = nullptr;
    bool bQueuedForUpdate = false;
};