#pragma once

#include "Engine/Engine.h"
#include "Core/Window.h"
#include "Core/Types.h"

#include "Render/Renderer.h"
#include "Render/RenderDevice.h"
#include "Render/Swapchain.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/LineBatcher.h"
#include "Render/DepthViewRenderer.h"
#include "Render/ShadowCascadeDebugRenderer.h"
#include "Engine/GameViewportClient.h"
#include "Render/SceneRenderer.h"

#include "SceneRenderPipeline.h"


#include "Editor/EditorUI/EditorUI.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/EditorControls/EditorControlsPanel.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Settings/ProjectSettingsPanel.h"
#include "Editor/DebugTools/DebugPanel.h"
#include "Editor/DebugTools/ShadowAtlasPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"


#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/Sequencer/SequencerPanel.h"

#include "PlayInEditorDataTypes.h"
#include "Core/Misc/Optional.h"

//Temp
#include "Text/Font.h"
#include "Text/TextRenderer.h"

class UEditorEngine : public UEngine
{
	DECLARE_CLASS(UEditorEngine, UEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;

	// PIE에서 게임 종료는 에디터가 아니라 PIE 세션만 끝낸다 (UE QuitGame과 같다).
	void QuitGame() override { if (IsPlaying()) RequestEndPlayMap(); }
	// PIE에서 씬 전환은 요청만 받고, 다음 프레임 시작에 PIE 세션(창·뷰포트)을 유지한 채 PlayWorld만 바꾼다.
	void TravelToScene(const FString& ScenePath) override;

	// Active View의 입력과 Picking 결과만 Gizmo 및 선택 상태에 반영한다.
	void UpdateGizmoAndPicking();
	// View 하나의 Scene·Grid·Gizmo·텍스트를 해당 ViewProjection으로 렌더한다.
	// 씬은 SceneRenderingInfo(Linear float)에 그리고, 인코딩한 결과와 에디터 표시는 ViewRenderingInfo(sRGB)에 그린다.
	void RenderFrame(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, FEditorViewportClient* OverlayClient);
	// 네 View 결과와 ImGui를 메인 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	void PresentFrame();
	void DeleteActor(AActor* Actor);
	void DeleteComponent(UActorComponent* Component);

	//Play 버튼을 눌렀을때 Play Session 실행을 요청한다.
	inline void RequestPlaySession() { bPlaySessionRequested = true; }
	inline void RequestEndPlayMap() { bRequestEndPlayMapQueued = true; bIsPaused = false; }

	//실제 PIE 를 실행
	void StartPlayInEditorSession();
	UWorld* CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld);
	// 씬에 배치된 카메라가 있으면 메인 카메라로, 없으면 활성 에디터 뷰포트 시점에서 시작한다.
	void SetupPIEMainCamera();
	// 지금 PlayWorld를 지우고 씬 파일에서 불러온 새 PIE 월드로 바꿔 BeginPlay까지 한다.
	void TravelPIEToScene(const FString& ScenePath);

	void OnActiveWorldChanged();

	void EndPlayMap();

	bool IsPlaying() const { return PlayWorld != nullptr; }

	////FEditorDelegates::PrePIEEnded / EndPIE.Broadcast()
	//void TeardownPlaySession(const FWorldContext& PIEContext);            // 액터 EndPlay, 월드 정리(CleanupWorld)
	//void RestoreEditorWorld(UWorld* EditorWorld);            // GWorld를 에디터 월드로 복구
	//void DestroyWorldContext(UWorld* PlayWorld);

private:
	// 이번 프레임 DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
	void BeginFrame(float DeltaTime);
	// 패널 요청과 입력을 Panel과 클라이언트에 전달해 레이아웃·카메라 상태를 갱신한다.
	void UpdateEditorViewportState(float DeltaTime);
	// 월드를 정확히 한 번 Tick·Capture한 뒤 에디터 상호작용을 갱신한다.
	void TickWorldAndEditor(float DeltaTime);
	// 한 번 캡처한 월드 결과를 재사용해 현재 레이아웃의 각 View를 렌더한다.
	void RenderViewports();
	void ReleaseWorldShadowResources(UWorld* World);
	bool ViewSupportsShadows(const FSceneView& View) const;
	// SettingsPanel 값 중 씬 렌더에 필요한 것만 파이프라인 설정으로 옮긴다.
	FSceneRenderSettings MakeSceneRenderSettings() const;
	// 현재 표시 슬롯 또는 네이티브 창 포커스로 게임 카메라 입력을 켜고 끈다.
	void UpdatePIEInput();
	FGameViewportClient* GetPIEGameViewportClient() const;

	// 별도 PIE 창(New Editor Window)을 만들고 정리한다.
	bool CreatePIEWindow();
	void DestroyPIEWindow();
	// 현재 OS 표시 상태와 Single Panel 크기를 World Tick 전에 갱신한다. GPU 자원은 만들지 않는다.
	void UpdatePIEWindowState();
	// 렌더 단계에서 표시 가능한 창의 Swapchain 크기만 갱신한다.
	bool UpdatePIEWindowTargets();
	// PIE 창의 오프스크린 결과를 백버퍼로 복사한다. Present는 EndFrame에서 한다.
	void CopyPIEWindowToBackbuffer();
	bool IsPIEInWindow() const { return PIEWindow != nullptr; }
	// 화면 합성과 View 설정 보관으로 프레임을 마무리한다.
	void EndFrame();

	//Overlay Pass
	void RenderOverlayPass(FEditorViewportClient* OverlayClient, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, FRenderQueue& RenderQueue);
	//Depth Pass
	void RenderDepthPass(const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward);



	FWorldContext& GetEditorWorldContext();
	FWorldContext* GetPIEWorldContext(int32 WorldPIEInstance = 0);
	UWorld* GetActiveWorld() const { return PlayWorld ? PlayWorld : EditorWorld; }
	// PIE 중이고 일시정지가 아닌 View는 Gizmo·Picking 등 에디터 상호작용을 하지 않는다.
	bool IsPlayingView(int32 ViewIndex) const { return ViewportsPanel->GetGameViewportClient(ViewIndex) && !bIsPaused; }

	// FEngineLoop 소유. OnInit에서 받아 둔다.
	FWindow* MainWindow = nullptr;
	FSwapchain* MainWindowSC = nullptr;
	FRenderer* Renderer = nullptr;

	TUniquePtr<FEditorUI> EditorUI;

	// Editor·Game 공용 씬 렌더(그림자·Opaque·Skybox)와 후처리(Fog·Gamma·FXAA)를 담은 Pipeline
	FSceneRenderPipeline ScenePipeline;

	TUniquePtr<FImGuiRenderer> ImGuiRenderer;
	TUniquePtr<FGridRenderer> GridRenderer;
	TUniquePtr<FGizmoRenderer> GizmoRenderer;
	TUniquePtr<FTextRenderer> TextRenderer;
	TUniquePtr<FLineBatcher> LineBatcher;
	TUniquePtr<FGizmo> Gizmo;
	TUniquePtr<FOutline> Outline;
	TUniquePtr<FOutlineRenderer> OutlineRenderer;
	TUniquePtr<FDepthViewRenderer> DepthViewRenderer;
	TUniquePtr<FShadowCascadeDebugRenderer> ShadowCascadeDebugRenderer;

	UFont* SystemFont;

	FOutputLogPanel* OutputLogPanel = nullptr;

	FDetailsPanel* DetailsPanel = nullptr;
	FEditorControlsPanel* EditorControlsPanel = nullptr;
	FSettingsPanel* SettingsPanel = nullptr;
	FProjectSettingsPanel* ProjectSettingsPanel = nullptr;
	FDebugPanel* DebugPanel = nullptr;
	FShadowAtlasPanel* ShadowAtlasPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FSceneRenderer SceneRenderer;
	FOutlinerPanel* OutlinerPanel = nullptr;
	FContentDrawerPanel* ContentDrawerPanel = nullptr;
	FSequencerPanel* SequencerPanel = nullptr;

	UWorld* EditorWorld = nullptr;
	UWorld* PlayWorld = nullptr;

	// Play 버튼이 다음 세션을 띄울 위치
	EPlayModeType PlayMode = EPlayModeType::InViewport;

	// New Editor Window(PIE) 모드에서만 존재한다. Swapchain이 창 핸들을 쓰므로 창보다 먼저 해제한다.
	TUniquePtr<FWindow> PIEWindow;
	TUniquePtr<FSwapchain> PIESwapchain;
	TUniquePtr<FViewportsPanel> PIEPanel;
	FViewport* PIEViewport = nullptr; // 현재 PIE 세션의 화면. Panel 소유이며 종료 전에 해제한다.
	bool bPIEWindowRendered = false;

	FVector UUIDLocation;

	FEditorCommand Save;

	FEditorCommand Play;
	FEditorCommand Pause;
	FEditorCommand Resume;
	FEditorCommand StepFrame;
	FEditorCommand Stop;

	bool bPlaySessionRequested = false;
	bool bRequestEndPlayMapQueued = false;
	// Lua OpenScene이 요청한 다음 PIE 씬. World Tick 도중에는 월드를 지울 수 없어 다음 프레임 시작에 처리한다.
	FString PendingPIEScenePath;
	bool bStepRequested = false;
	bool bIsPaused = false;

	void ResetSceneSelection();

	void CreateNewScene();
	void OpenScene();
	void SaveCurrentScene();
	void SaveSceneAs();
};
