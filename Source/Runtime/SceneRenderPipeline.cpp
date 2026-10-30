#include "EnginePCH.h"
#include "SceneRenderPipeline.h"
#include "Engine/Viewport.h"
#include "Engine/World.h"
#include "Render/SceneView.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/ColorWorkflow.h"
#include "Render/ViewMode.h"
#include "Core/ResourcePaths.h"
#include "Engine/HUD.h"

bool FSceneRenderPipeline::Init(FRenderer* InRenderer)
{
    Renderer = InRenderer;

    // 실패해도 엔진을 띄우는 기존 동작을 유지한다.
    SkyboxRenderer = MakeUnique<FSkyboxRenderer>();
    SkyboxRenderer->Init(ResourcePaths::DefaultSkyPanorama);

    FogRenderer = MakeUnique<FFogRenderer>();
    FogRenderer->Init();

    FXAARenderer = MakeUnique<FFXAARenderer>();
    FXAARenderer->Init();

    GammaEncodeRenderer = MakeUnique<FGammaEncodeRenderer>();
    GammaEncodeRenderer->Init();

    HUDRenderer = MakeUnique<FHUDRenderer>();
    HUDRenderer->Init();

    return true;
}

bool FSceneRenderPipeline::ViewSupportsShadows(const FSceneView& View, const FSceneRenderSettings& Settings) const
{
    return View.RenderMode.ViewMode == EViewMode::Lit && View.RenderMode.ShadingModel == EShadingModel::Phong
        && Settings.bDrawPrimitives && !Settings.bVisualizeTileLightCount;
}

bool FSceneRenderPipeline::RenderScene(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, FSceneRenderer& SceneRenderer,
    uint32 ShadowResourceIndex, bool bSupportsShadows, const FSceneRenderSettings& Settings, FShadowFrameData& OutDirectionalFrame)
{
    const FRenderingInfo& SceneRenderingInfo = Viewport.GetSceneRenderingInfo();
    FLightingConstants Lighting;
    FSceneRenderData& Data = Viewport.GetSceneRenderData();

    // 지원 View만 공유 그림자를 요청한다. 비지원 View는 다른 View의 결과를 지우지 않는다.
    const FShadowFrameData* ShadowFrame = nullptr;
    if (bSupportsShadows)
    {
        if (ShadowResourceIndex != InvalidShadowResourceIndex)
            ShadowFrame = Renderer->RenderWorldShadows(ShadowResourceIndex, ViewWorld, SceneRenderer);
        Renderer->RenderDirectionalShadow(Viewport, ViewWorld, SceneRenderer, View, OutDirectionalFrame);
    }
    else
        Viewport.GetDirectionalShadow().ResetFrame();
    const bool bReadyForShading = Renderer->BindWorldShadows(ShadowResourceIndex, SceneRenderer.GetFrameNumber(), bSupportsShadows);
    const bool bDirectionalReady = Renderer->BindDirectionalShadow(Viewport, SceneRenderer.GetFrameNumber(), bSupportsShadows);
    // 셰이딩 준비가 성공한 경우에만 기존 요청 배열을 비소유 참조로 전달한다.
    const TArray<FShadowRequestPerLight>* ShadowRequests = bReadyForShading && ShadowFrame ? &ShadowFrame->ShadowRequestsPerLight : nullptr;
    ViewWorld.GetScene().BuildLightingConstants(Lighting, ShadowRequests, bDirectionalReady ? &OutDirectionalFrame.ShadowRequestsPerLight : nullptr);
    ViewWorld.GetScene().BuildStructuredLight(Data.PointLights, Data.SpotLights, Data.CullingLights, View.ViewMatrix, ShadowRequests);
    Lighting.bReadyForShading = bReadyForShading ? 1u : 0u;
    Lighting.NumShadowInfos = bReadyForShading ? Renderer->GetShadowResources(ShadowResourceIndex)->NumShadowInfos : 0u;
    Renderer->SetLighting(Lighting, Data.PointLights, Data.SpotLights);

    // 불투명·반투명 Pass (Linear 씬 컬러)
    RenderOpaquePass(Viewport, SceneRenderingInfo, View, Lighting.Directional, Settings);
    // Viewport 자원을 GPU 바인딩이 붙잡지 않게 조명 패스 소비 후 해제한다.
    Renderer->BindDirectionalShadow(Viewport, SceneRenderer.GetFrameNumber(), false);

    return bDirectionalReady;
}

void FSceneRenderPipeline::RenderPostProcess(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, const FSceneRenderSettings& Settings)
{
    const FRenderingInfo& SceneRenderingInfo = Viewport.GetSceneRenderingInfo();
    const FRenderingInfo& ViewRenderingInfo = Viewport.GetRenderingInfo();

    // 데이터 시각화 뷰는 씬 컬러 값 자체가 결과이므로 안개·인코딩·FXAA로 값을 바꾸지 않는다.
    const EViewMode ViewMode = View.RenderMode.ViewMode;
    const bool bVisualizeData = IsDataVisualization(ViewMode);

    //직교인 경우 안개 Pass를 그리지 않는다.
    if (!View.IsOrthographic() && !bVisualizeData && Settings.ShowFlags.Fog)
        // 1. 안개 Pass (Linear 씬 컬러)
        RenderFogPass(ViewWorld, SceneRenderingInfo, View.GetCameraLocation(), View.ViewProjection);

    // 2. Gamma Encode Pass: Linear 씬 컬러 → (Lit이면 ACES) → 표시용 sRGB
    const bool bLinear = !bVisualizeData && ColorWorkflow::bLinear;
    FGammaEncodeParams Params;
    Params.bEncode = bLinear && Settings.ShowFlags.GammaCorrection;
    Params.bToneMap = bLinear && Settings.ShowFlags.Tonemapper && ViewMode == EViewMode::Lit;
    Params.ExposureEV = Settings.ExposureEV;
    
    // 데이터 시각화 뷰는 값 자체가 결과라 화면 효과를 넣지 않는다
    if (!bVisualizeData && Settings.ShowFlags.PostProcessing)
    {
        const FScreenEffectDebug& Debug = Settings.ScreenEffectDebug;
        if (Debug.bOverride)
        {
            Params.VignetteIntensity = Debug.VignetteIntensity;
            Params.OverlayColor = FLinearColor(Debug.FadeColor.R, Debug.FadeColor.G, Debug.FadeColor.B, Debug.FadeAmount);
            Params.LetterBoxAspect = Debug.LetterBoxAspect;
            Params.LetterBoxAmount = Debug.LetterBoxAmount;
        }
        else
        {
            Params.VignetteIntensity = View.PostProcessSettings.VignetteIntensity;
            Params.OverlayColor = View.OverlayColor;
            Params.LetterBoxAspect = View.LetterBoxAspect;
            Params.LetterBoxAmount = View.LetterBoxAmount;
        }
        if (!Settings.ShowFlags.Vignette)
            Params.VignetteIntensity = 0.0f;
    }
    RenderGammaEncodePass(SceneRenderingInfo, ViewRenderingInfo, Params);

    // 3. FXAA Pass: 밝기 계산이 감마 공간을 전제로 하므로 인코딩 뒤에 한다
    if (!bVisualizeData && Settings.ShowFlags.AntiAliasing)
        RenderFXAAPass(Viewport.GetFxaaTarget(), ViewRenderingInfo);
}

void FSceneRenderPipeline::RenderOpaquePass(FViewport& Viewport, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, const FDirectionalLightInfo& DirectionalLight, const FSceneRenderSettings& Settings)
{
    const FMatrix& ViewProjection = View.ViewProjection;
    const FVector ViewCameraLocation = View.GetCameraLocation();
    FRenderQueue& RenderQueue = Viewport.GetSceneRenderData().RenderQueue;

    // 뷰 모드가 셰이더 permutation을, Wireframe 여부가 Fill Mode를 정한다. 삼각형 연결은 그대로다.
    const FViewRenderMode ViewRenderMode = View.RenderMode;
    const bool bVisualizeLightCount = Settings.bVisualizeTileLightCount;
    const bool bDrawPrimitives = Settings.bDrawPrimitives;
    const bool bDepthPrepass = bDrawPrimitives && ViewRenderMode.ViewMode == EViewMode::Lit;
    Renderer->SetViewMode(ViewRenderMode);
    if (bDrawPrimitives)
        Renderer->RenderQueueSorting(RenderQueue, ViewProjection, ViewCameraLocation);
    if (bDepthPrepass)
    {
        FRenderingInfo DepthInfo = ViewRenderingInfo;
        DepthInfo.ColorRenderTargets.Reset();
        RenderCommand::BeginRenderPass(DepthInfo);
        RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
        RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Renderer->RenderDepthPrepass(ViewProjection);
        RenderCommand::EndRenderPass(DepthInfo);
        Renderer->CullLights(Viewport.GetLightCullingResources(), View.ProjectionMatrix, DepthInfo.DepthStencil.Texture, Viewport.GetSceneRenderData().CullingLights, DepthInfo.ViewportSetting.Width, DepthInfo.ViewportSetting.Height, bVisualizeLightCount);
    }

    // 배경은 화면에서 sRGB 0.3 회색으로 보여야 한다. Linear 작업 흐름에서는 인코딩될 것이므로 디코딩해 지운다.
    FRenderingInfo PassInfo = ViewRenderingInfo;
    const float ClearValue = ColorWorkflow::bLinear ? FLinearColor::SRGBToLinear(0.3f) : 0.3f;
    PassInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = FVector4(ClearValue, ClearValue, ClearValue, 1.0f);
    if (bDepthPrepass) PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
    RenderCommand::BeginRenderPass(PassInfo);

    const ERasterizerState SceneRasterizerState = ViewRenderMode.ViewMode == EViewMode::Wireframe
        ? ERasterizerState::Wireframe : ERasterizerState::SolidBack;

    // 렌더 루프 — 반드시 RenderAll보다 먼저
    // 직교는 시선이 모두 평행해 파노라마의 한 점만 확대되므로 하늘을 그리지 않고 지우기 색을 배경으로 쓴다.
    // 데이터 시각화 뷰도 하늘은 데이터가 없으므로 그리지 않는다.
    if (!View.IsOrthographic() && !IsDataVisualization(ViewRenderMode.ViewMode))
    {
        SkyboxRenderer->OnRender(ViewProjection, ViewCameraLocation, DirectionalLight);
    }

    if (bDrawPrimitives)
    {
        RenderCommand::SetRasterizerState(SceneRasterizerState);
        RenderCommand::SetBlendState(EBlendState::Opaque);
        RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

        RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // 불투명을 먼저 그리고 반투명을 먼 것부터 그린다.
        // Grid는 깊이를 쓰지 않는 반투명 위에도 겹칠 수 있지만, 언리얼의 에디터 프리미티브처럼 Overlay Pass에서 마지막에 합성한다.
        Renderer->RenderOpaque(ViewProjection, bDepthPrepass);
        Renderer->RenderTranslucent(ViewProjection);
        // 장면 Wireframe이 Grid·Gizmo·UI로 전파되지 않도록 복원한다.
        RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
    }

    RenderCommand::EndRenderPass(PassInfo);
    RenderCommand::UnbindShaderResources(8, 4, EShaderBindFlagBits::Pixel);
    RenderCommand::UnbindShaderResources(8, 2, EShaderBindFlagBits::Vertex);
}

void FSceneRenderPipeline::RenderFogPass(UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FVector& ViewCameraLocation, const FMatrix& ViewProjection)
{
    const TArray<FExponentialHeightFogSceneInfo>& SceneFogInfos = ViewWorld.GetScene().FogInfos;

    if (SceneFogInfos.Num() == 0)
        return;

    FRenderingInfo PassInfo = ViewRenderingInfo;

    //Load는 기존 내용을 그대로 두고 그 위에 그림을 그린다는뜻
    for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
        Color.LoadOp = ERenderTargetLoadOp::Load;

    PassInfo.DepthStencil.Texture = nullptr;
    RenderCommand::BeginRenderPass(PassInfo);

    //Fog는 여러개있더라도 1개의 Fog만 인식해서 그리도록 해야한다.
    FogRenderer->OnRender(ViewRenderingInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, SceneFogInfos[0].FogInfo);

    RenderCommand::EndRenderPass(PassInfo);
}

void FSceneRenderPipeline::RenderGammaEncodePass(const FRenderingInfo& SceneRenderingInfo, const FRenderingInfo& ViewRenderingInfo, const FGammaEncodeParams& Params)
{
    FTexture2D* SceneColor = SceneRenderingInfo.ColorRenderTargets[0].Texture;
    FTexture2D* ViewColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
    if (!SceneColor || !ViewColor)
        return;

    // 출력은 표시용 타깃 전체를 덮어쓰므로 Clear·깊이가 필요 없다
    FRenderingInfo PassInfo;
    PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

    FRenderingDesc ColorDesc{};
    ColorDesc.Texture = ViewColor;
    ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
    PassInfo.ColorRenderTargets.Add(ColorDesc);
    PassInfo.DepthStencil.Texture = nullptr;

    RenderCommand::BeginRenderPass(PassInfo);
    GammaEncodeRenderer->OnRender(SceneColor, Params, static_cast<float>(PassInfo.ViewportSetting.Width), static_cast<float>(PassInfo.ViewportSetting.Height));
    RenderCommand::EndRenderPass(PassInfo);

    // 다음 프레임에 SceneColor를 RTV로 쓸 때 SRV로 남아 있으면 D3D가 강제로 해제하므로 먼저 비운다
    RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void FSceneRenderPipeline::RenderFXAAPass(FTexture2D* FxaaTarget, const FRenderingInfo& ViewRenderingInfo)
{
    FTexture2D* SceneColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
    if (!SceneColor || !FxaaTarget)
        return;

    // 출력은 임시 텍스처, 깊이 없음, 어차피 전부 덮어쓰니 Clear 불필요
    FRenderingInfo PassInfo;
    PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

    FRenderingDesc ColorDesc{};
    ColorDesc.Texture = FxaaTarget;
    ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
    PassInfo.ColorRenderTargets.Add(ColorDesc);
    PassInfo.DepthStencil.Texture = nullptr;

    //장면 텍스처를 읽어서 임시 텍스처에 FXAA 결과를 그린다
    RenderCommand::BeginRenderPass(PassInfo);
    FXAARenderer->OnRender(SceneColor);
    RenderCommand::EndRenderPass(PassInfo);

    // SceneColor가 SRV로 남아 있으면 Overlay 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다
    RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

    RenderCommand::CopyTexture(SceneColor, FxaaTarget);
}

void FSceneRenderPipeline::RenderHUD(FViewport& Viewport, const FHUD& HUD, const FSceneView& View)
{
    if (HUD.GetCommands().Num() == 0)
        return;

    // 표시용 타깃에 이어서 그린다 (Load). 깊이는 쓰지 않음.
    FRenderingInfo PassInfo = Viewport.GetRenderingInfo();
    for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
        Color.LoadOp = ERenderTargetLoadOp::Load;
    PassInfo.DepthStencil.Texture = nullptr;

    RenderCommand::BeginRenderPass(PassInfo);
    const FVector2 Size = Viewport.GetOutputSize();
    HUDRenderer->OnRender(HUD, Size.X, Size.Y, &View);
    RenderCommand::EndRenderPass(PassInfo);
}