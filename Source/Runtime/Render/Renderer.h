#pragma once

#include "RenderPacket.h"
#include "Texture2D.h"
#include "Text/Font.h"

#include "RenderingInfo.h"
#include "ViewMode.h"
#include "LightInfo.h"
#include "Occlusion/GPUOcclusion.h"
#include "TileLightCulling.h"
#include "LocalLight.h"
#include "ShadowResources.h"
#include "Math/Color.h"
#include "ShowFlags.h"

struct FDeferredDecalSceneProxy;
struct FShadowFrameData;
struct FShadowRequestPerLight;
struct FShadowDepthPassInfo;
class FViewport;
class FSceneRenderer;
class UWorld;
struct FSceneView;

// Directional의 기존 Depth 셰이더와 b0 크기를 공유하되, 별도 버퍼에 올린다.
struct FShadowDepthPassConstants
{
	FMatrix ViewProjection;
	FVector LightPosition{};
	float InvRadius = 0.0f;
};
static_assert(sizeof(FShadowDepthPassConstants) == 80);
static_assert(offsetof(FShadowDepthPassConstants, LightPosition) == 64);
static_assert(offsetof(FShadowDepthPassConstants, InvRadius) == 76);

constexpr uint32 ObjectSlotBytes = 256;
// UberLit이 데칼 결과를 읽는 슬롯. 그림자 SRV(t12~t15) 다음 칸이다.
constexpr uint32 DBufferSRVSlot = 16;
// 데칼 노멀(DBufferB)을 읽는 슬롯
constexpr uint32 DBufferNormalSRVSlot = 17;

struct FPerObjectConstants
{
	FMatrix World;
	FMatrix NormalMatrix;
	uint32 bReceiveShadow = 0;
	uint32 Padding[3] = {};
};
static_assert(sizeof(FPerObjectConstants) == 144);
static_assert(offsetof(FPerObjectConstants, bReceiveShadow) == 128);
static_assert(sizeof(FPerObjectConstants) <= ObjectSlotBytes);

struct FViewConstants
{
	FMatrix ViewProjection;
	FVector CameraPosition;
	float Padding = 0.0f;
};
static_assert(sizeof(FViewConstants) == 80, "HLSL b0 레이아웃(matrix + float3 + float)과 같아야 한다");

struct FDecalConstants
{
	FMatrix ViewProjection;
	FMatrix InvViewProjection;
	FMatrix DecalToWorld;
	FMatrix WorldToDecal;
	FLinearColor DecalColor;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;
	float FadeAlpha = 1.0f;
	// 0: 노멀맵 없음, 1: 노멀맵 있음, 2: 노멀맵 있고 G 채널을 뒤집는다(bFlipNormalGreen)
	float NormalMode = 0.0f;
};
static_assert(sizeof(FDecalConstants) == 288, "HLSL b6 FDecalConstants(DecalCommon.hlsli)와 같아야 한다");

inline FMatrix MakeNormalMatrix(const FMatrix& World) { return World.Inverse().GetTransposed(); }

inline void WriteObjectSlot(uint8* SlotBase, uint32 Slot, const FMatrix& World, const FMatrix& NormalMatrix, bool bReceiveShadow = false)
{
	uint8* Dest = SlotBase + static_cast<size_t>(Slot) * ObjectSlotBytes;
	std::memcpy(Dest + offsetof(FPerObjectConstants, World), &World, sizeof(FMatrix));
	std::memcpy(Dest + offsetof(FPerObjectConstants, NormalMatrix), &NormalMatrix, sizeof(FMatrix));
	const uint32 Receive = bReceiveShadow ? 1u : 0u;
	std::memcpy(Dest + offsetof(FPerObjectConstants, bReceiveShadow), &Receive, sizeof(Receive));
}

struct FSortEntry
{
	uint64 Key;
	uint32 PacketIndex;
};

class UCameraComponent;

// 오클루전 컬링의 효과 상한을 재기 위한 측정 결과 (디버그 전용)
struct FOcclusionMeasureResult
{
	bool bValid = false;
	uint32 TotalObjects = 0;      // 불투명 패스에 제출된 물체 수
	uint32 VisibleObjects = 0;    // 최종 깊이 버퍼에 픽셀을 1개 이상 남긴 물체 수
	uint32 TotalDraws = 0;
	uint32 VisibleDraws = 0;
	uint64 TotalTriangles = 0;
	uint64 VisibleTriangles = 0;
	double ElapsedMs = 0.0;       // 측정 자체에 걸린 시간 (GPU 대기 포함)
	// GPU 오클루전 검증. 판정만 하고 거르지 않은(Cull 끔) 상태에서 측정해야 의미가 있다.
	uint32 GPUOccludedDraws = 0;  // GPU가 가렸다고 판정한 드로우 수
	uint32 FalseCulls = 0;        // 그중 실제로는 픽셀이 보인 드로우 수. 0이어야 한다
};

enum class ESceneMeshPass : uint8 { Color, DepthPrepass, OpaqueAfterPrepass };

class FRenderer
{
public:
	bool Init();
	uint32 AllocateShadowResources(bool bEditorWorld);
	void ReleaseShadowResources(uint32 Index);
	const FShadowResources* GetShadowResources(uint32 Index) const;
	const FShadowFrameData* RenderWorldShadows(uint32 Index, UWorld& World, FSceneRenderer& SceneRenderer);
	bool RenderDirectionalShadow(FViewport& Viewport, UWorld& World, FSceneRenderer& SceneRenderer, const FSceneView& View, FShadowFrameData& Frame);

	// 매 View 호출한다. 비지원/실패 시 입력을 비워 이전 World의 바인딩을 남기지 않는다.
	bool BindWorldShadows(uint32 Index, uint64 FrameNumber, bool bSupported);
	bool BindDirectionalShadow(const FViewport& Viewport, uint64 FrameNumber, bool bSupported);

	void SetViewMode(const FViewRenderMode& Mode);
	void SetLighting(const FLightingConstants& GlobalLights, const TArray<FPointLightInfo>& PointLights, const TArray<FSpotLightInfo>& SpotLights);

	// 기존 단일 카메라의 ViewProjection으로 렌더 큐 전체를 그린다.
	void RenderAll(FRenderQueue& InQueue, UCameraComponent* CameraComponent);

	// SceneView의 ViewProjection을 직접 받아 View별 렌더 큐를 그린다.
	void RenderAll(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraPosition);

	// Lit 전용. Depth Prepass 뒤, 불투명 셰이딩 전에 호출해 데칼 색은 DBuffer에, 노멀은 DBufferNormal에 쌓는다.
	// UberLit이 둘을 읽어 Albedo와 노멀에 섞은 뒤 조명을 계산하므로 데칼도 빛과 그림자를 받고 요철이 생긴다.
	// 데칼이 없어도 DBuffer를 지우므로 매 Lit View마다 호출한다.
	void RenderDecalsToDBuffer(const TArray<FDeferredDecalSceneProxy*>& Decals, const FSceneView& View,
		const FRenderingInfo& SceneInfo, FTexture2D* DBuffer, FTexture2D* DBufferNormal, EDecalMethod Method);

	// 큐를 정렬해 불투명 패킷만 그린다. 반투명은 RenderTranslucent 호출 전까지 보관한다.
	void RenderOpaque(const FMatrix& ViewProjection, bool bAfterPrepass = false);

	// 불투명과 반투명 사이에 호출해 데칼을 씬 컬러에 바로 덧그린다.
	// Unlit의 Albedo 데칼은 AlphaBlend로 그리며, 조명이 없어 DBuffer를 거친 결과와 같다.
	// Emissive 데칼은 Lit/Unlit 모두 조명이 끝난 뒤 Emissive(가산)로 그려 빛을 더한다.
	// SceneInfo는 진행 중인 씬 패스이며, 깊이를 읽기 전용 DSV로 바꿔 그린 뒤 원래 바인딩으로 이어 준다.
	void RenderDecal(const TArray<FDeferredDecalSceneProxy*>& Decals, const FSceneView& View, const FRenderingInfo& SceneInfo, EDecalMethod Method, EBlendState BlendState);
	// stat decal 그리기 수치를 뷰 시작에 한 번 되돌린다. 한 뷰에서 Albedo와 Emissive 패스가 따로 그려도 합쳐서 센다.
	void BeginDecalStats(EDecalMethod Method, uint32 TotalDecals);
	void RenderDepthPrepass(const FMatrix& ViewProjection);

	// Pass 1: 한 면을 Clear하고 그린다.
	// 호출자가 해당 Request와 렌더 경로에 맞는 깊이 셰이더를 전달한다.
	// false면 해당 그림자를 셰이딩에 사용하지 않는다.
	bool DrawShadowDepth(const FShadowRequestPerLight& Request, const FShadowDepthPassInfo& Pass, FShaderProgram* Shader, ID3D11DepthStencilView* DSV, const FShadowAtlasRect& Rect);

	// Gather 전에 World가 불러 가려진 물체를 거른다.
	FGPUOcclusion& GetGPUOcclusion() { return GPUOcclusion; }
	
	// 캐시된 스태틱 메시 경로: World::Gather가 조각별로 채운 묶음을 이번 프레임 불투명 패스에 넘긴다.
	// 묶음 메모리는 World 소유이며 다음 Gather 전까지 유효하다. RenderQueueSorting이 묶음을 정렬하고 RenderOpaque가 그린다.
	void ResetStaticDrawGroups() { StaticGroups.clear(); }
	void AddStaticDrawGroup(const FStaticDrawGroup* Group) { StaticGroups.push_back(Group); }

	// RenderOpaque가 보관한 반투명 패킷을 먼 것부터 그린다.
	void RenderTranslucent(const FMatrix& ViewProjection);

	// 큐를 Material, Opaque, Mesh에 따라 정렬하고 View 상수(b0)를 올린다
	void RenderQueueSorting(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraPosition);

	// [측정 전용] RenderOpaque 직후에 호출한다. 불투명 패킷을 깊이 LESS_EQUAL·색 쓰기 없이 다시 그리며
	// 패킷마다 오클루전 쿼리를 걸어, 최종 화면에 실제로 픽셀을 남긴 물체 수를 센다.
	// GPU 결과를 기다리므로 매우 느리다. 버튼 등으로 한 프레임만 실행할 것.
	FOcclusionMeasureResult MeasureOpaqueOcclusion(const FMatrix& ViewProjection);

	uint8* BeginObjectConstants(uint32 MaxSlots);
	void EndObjectConstants();

	void CullLights(FTileLightCullingResources& Resources, const FMatrix& Projection, FTexture2D* Depth, const TArray<FCullingLight>& Lights, uint32 Width, uint32 Height, bool bVisualizeLightCount);

private:
	std::array<TUniquePtr<FShadowResources>, MaxShadowResourceSlots> WorldShadowResources;
	uint32 NextShadowResourceIndex = 1;
	// FIFO 소비용 배열의 용량만 재사용하며 매 View의 패킷 값은 새로 채운다.
	FRenderQueue RenderPackets;
	// 정렬된 RenderPackets에서 반투명 패킷이 시작되는 위치
	uint32 FirstTranslucentIndex = 0;
	uint32 FirstMaterialIndex = 0;
	TUniquePtr<FConstantBuffer> PerObjectCB;
	TUniquePtr<FConstantBuffer> ViewCB;
	TUniquePtr<FConstantBuffer> ShadowDepthPassCB;
	TUniquePtr<FConstantBuffer> LightingCB;
	TUniquePtr<FConstantBuffer> TileShadingCB;
	TUniquePtr<FConstantBuffer> DecalCB;
	FShaderProgram* DepthShader = nullptr;
    FShaderProgram* ShadowNearClampShader = nullptr;
	FShaderProgram* ShadowDistanceShader = nullptr;
	FShaderProgram* ShadowTileClearShader = nullptr;
	FShaderProgram* DecalMeshShader = nullptr;
	FShaderProgram* DeferredDecalShader = nullptr;
	ComPtr<ID3D11DepthStencilState> ShadowTileClearDepthState;

	// 모든 패킷의 World 행렬을 256바이트 칸에 한 번에 올린 버퍼. D3D11.1 오프셋 바인딩을 못 쓰면 PerObjectCB로 돌아간다.
	TUniquePtr<FConstantBuffer> PerObjectSlotCB;
	uint32 PerObjectSlotCapacity = 0;
	bool bUsePerObjectSlots = false;
	bool bObjectConstantsPrepared = false;
	UMaterial* LastMaterial;
	UStaticMesh* LastMesh;

	FShaderProgram* ViewModeShader = nullptr;
	FShaderProgram* ViewModeNormalShader = nullptr;

	// MeasureOpaqueOcclusion 전용. 처음 측정할 때 만들고 이후 재사용한다.
	TArray<ComPtr<ID3D11Query>> OcclusionQueries;
	ComPtr<ID3D11DepthStencilState> DepthLessEqualReadOnly;

	FGPUOcclusion GPUOcclusion;
	FLocalLight LocalLight;
	FTileLightCulling TileLightCulling;

	void DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection, ESceneMeshPass Pass = ESceneMeshPass::Color);
	void DrawStaticGroups(ESceneMeshPass Pass = ESceneMeshPass::Color);
	void UpdatePerObjectConstants(const FMatrix& World, const FMatrix& NormalMatrix, bool bReceiveShadow = false);

	// 이번 프레임 스태틱 메시 묶음 (정렬 키 순). 비어 있지 않은 것만 담는다.
	std::vector<const FStaticDrawGroup*> StaticGroups;
	void BindMaterial(UMaterial* material, ESceneMeshPass Pass = ESceneMeshPass::Color);
	void UpdateMaterialParams(const FRenderPacket& RenderPacket);
	// 데칼 공통 상수를 채우고 방식에 맞게 그린다. 렌더 타깃과 읽기 전용 깊이는 호출자가 바인딩해 둔다.
	void DrawDecals(const TArray<FDeferredDecalSceneProxy*>& Decals, const FSceneView& View, const FViewportSettings& Viewport, FTexture2D* SceneDepth, EDecalMethod Method, EBlendState BlendState);
	void DrawDecalBoxes(const TArray<FDeferredDecalSceneProxy*>& Decals, const FSceneView& View, FDecalConstants& Constants, FTexture2D* SceneDepth);
	void DrawDecalReceivers(const TArray<FDeferredDecalSceneProxy*>& Decals, const FSceneView& View, FDecalConstants& Constants);

	// stat decal 비용 측정. GPU는 몇 프레임 늦게 끝나므로 슬롯을 돌려 쓰고, 끝난 슬롯만 기다리지 않고 읽는다.
	struct FDecalGPUQuery
	{
		ComPtr<ID3D11Query> Disjoint;	// 타임스탬프 주파수와 측정 중 클럭 변화 여부
		ComPtr<ID3D11Query> Begin;
		ComPtr<ID3D11Query> End;
		ComPtr<ID3D11Query> Pipeline;	// 픽셀 셰이더 실행 수
		uint32 StatsView = 0;			// 어느 뷰의 측정인지. 같은 뷰의 패스끼리 더한다
		bool bPending = false;
	};
	static constexpr uint32 DecalGPUQueryCount = 8;
	std::array<FDecalGPUQuery, DecalGPUQueryCount> DecalGPUQueries;
	uint32 NextDecalGPUQuery = 0;
	// BeginDecalStats마다 1씩 는다. 패스별 측정을 뷰 단위로 묶는 번호다
	uint32 DecalStatsView = 0;
	double DecalViewCpuMs = 0.0;		// 이번 뷰에서 지금까지 그린 패스들의 CPU 시간 합
	bool bDecalViewDrew = false;
	// GPU 결과는 몇 프레임 늦게 오므로 같은 뷰의 결과가 다 모일 때까지 따로 더해 둔다
	uint32 DecalGPUSumView = 0;
	double DecalGPUSumMs = 0.0;
	uint64 DecalGPUSumPixels = 0;
	bool bDecalGPUSumValid = false;		// 시간을 믿을 수 있는지. 하나라도 클럭이 바뀌었으면 false
	bool bDecalGPUSumStarted = false;
	void FlushDecalGPUSum();
	// 끝난 측정을 FDecalStats에 옮긴다
	void ReadDecalGPUQueries();
	// 쓸 슬롯이 아직 GPU에서 안 끝났으면 이번 측정은 건너뛰고 nullptr를 돌려준다
	FDecalGPUQuery* BeginDecalGPUQuery();
	void EndDecalGPUQuery(FDecalGPUQuery* Query);
	// 데칼마다 바뀌는 상수와 텍스처를 올린다. 숨겼거나, 화면 밖이거나, Fade로 다 사라져 그릴 것이 없으면 false
	bool BindDecal(const FDeferredDecalSceneProxy& Decal, const FSceneView& View, FDecalConstants& Constants, const FMatrix& BoxToWorld);
	void UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrix& ViewProjection);
	void EnsurePerObjectSlotCapacity(uint32 SlotCount);
	void UploadPerObjectConstants();

	TArray<FSortEntry> SortEntries;
};
