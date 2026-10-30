#pragma once
#include "Core/Types.h"

#include "Render/FogRenderer.h"
#include "Render/FXAARenderer.h"
#include "Render/GammaEncodeRenderer.h"
#include "Render/SkyboxRenderer.h"
#include "Render/HUDRenderer.h"
#include "Render/ShadowFrameData.h"
#include "Render/ShowFlags.h"
#include "Render/PostProcessSettings.h"

class FViewport;
class UWorld;
struct FSceneView;
class FRenderer;
class FSceneRenderer;
class FHUD;

// 씬 렌더에 필요한 설정. Editor는 SettingsPanel 값을 채워 넘기고 Game은 기본값을 쓴다.
struct FSceneRenderSettings
{
	bool bDrawPrimitives = true;
	bool bVisualizeTileLightCount = false;
	FEngineShowFlags ShowFlags;		// Fog, FXAA, ToneMapping, GammaCorrection, Vignette
	float ExposureEV = 0.6f;
	FScreenEffectDebug ScreenEffectDebug; 	// Todo: PCM 연결 후 삭제
};

class FSceneRenderPipeline final
{
public:
	FSceneRenderPipeline() = default;
	~FSceneRenderPipeline() = default;

	FSceneRenderPipeline(const FSceneRenderPipeline&) = delete;
	FSceneRenderPipeline& operator=(const FSceneRenderPipeline&) = delete;

	// FRenderer는 FEngineLoop 소유라 빌려 쓴다.
	bool Init(FRenderer* InRenderer);

	// Lit + Phong이고 프리미티브를 그리며 타일 라이트 시각화가 꺼진 View만 그림자를 쓴다.
	bool ViewSupportsShadows(const FSceneView& View, const FSceneRenderSettings& Settings) const;

	// 그림자·라이팅을 준비하고 불투명·반투명을 Linear 씬 컬러에 그린다.
	// ShadowResourceIndex는 호출하는 엔진이 World별로 할당해 넘긴다. Directional 그림자가 준비됐으면 true를 반환한다.
	bool RenderScene(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, FSceneRenderer& SceneRenderer,
		uint32 ShadowResourceIndex, bool bSupportsShadows, const FSceneRenderSettings& Settings, FShadowFrameData& OutDirectionalFrame);

	// Fog → Gamma Encode → FXAA. Linear 씬 컬러를 표시용 sRGB로 만든다.
	void RenderPostProcess(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, const FSceneRenderSettings& Settings);

	//  안개, 톤매핑, FXAA 이후 맨 마지막에 UI 그리기
	// View는 이 뷰포트에 그린 장면의 View. HUD의 월드 텍스트를 같은 View로 투영한다.
	void RenderHUD(FViewport& Viewport, const FHUD& HUD, const FSceneView& View);

	//기존 EditorEngine에있는 Renderer들중에는 Editor에서도 써야하고 Game에서도 써야하는 Renderer들이있음
	//지금까진 EditorEngine에서 모든 Renderer가있어서 둘다 공통으로 진행되는 파이프라인을 FSceneRenderPipeline에 모아서 실행
private:
	void RenderOpaquePass(FViewport& Viewport, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, const FDirectionalLightInfo& DirectionalLight, const FSceneRenderSettings& Settings);
	void RenderFogPass(UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FVector& ViewCameraLocation, const FMatrix& ViewProjection);
	void RenderGammaEncodePass(const FRenderingInfo& SceneRenderingInfo, const FRenderingInfo& ViewRenderingInfo, const FGammaEncodeParams& Params);
	void RenderFXAAPass(FTexture2D* FxaaTarget, const FRenderingInfo& ViewRenderingInfo);

	FRenderer* Renderer = nullptr;

	TUniquePtr<FSkyboxRenderer> SkyboxRenderer;
	TUniquePtr<FFogRenderer> FogRenderer;
	TUniquePtr<FFXAARenderer> FXAARenderer;
	TUniquePtr<FGammaEncodeRenderer> GammaEncodeRenderer;
	TUniquePtr<FHUDRenderer> HUDRenderer;
};
