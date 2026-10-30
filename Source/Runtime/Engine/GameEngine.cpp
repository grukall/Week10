#include "EnginePCH.h"
#include "GameEngine.h"

#include "Launch/LaunchEngineLoop.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Core/ProjectSettings.h"
#include "Serialization/JsonArchive.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/Swapchain.h"
#include "Render/Texture2D.h"
#include "Input/InputSystem.h"

FEngineConfig UGameEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori";
	Desc.Width = 1280;          // 0이면 모니터 전체 해상도
	Desc.Height = 720;
	Desc.bBorderless = false;   // 제목 표시줄과 테두리가 있는 일반 창
	Desc.SyncInterval = 0;
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false; // Esc는 게임의 일시정지 키다. 창 모드라 닫기 버튼으로 끝낼 수 있다
	return Desc;
}

bool UGameEngine::Init()
{
	if (!Super::Init())
		return false;

	// Skybox·Fog·FXAA·Gamma는 Editor와 공용인 파이프라인이 소유한다.
	ScenePipeline.Init(GetEngineLoop().GetRenderer());

	BeginWorld(GetStartScenePath());
	return true;
}

// Config/Project.json의 StartScene 경로. 설정이 없으면 빈 문자열이고 원인은 경고 로그로 남긴다.
FString UGameEngine::GetStartScenePath() const
{
	FProjectSettings Settings;
	if (!Settings.Load(FProjectSettings::DefaultPath))
		return {};
	if (Settings.StartScene.empty())
		HTR_LOG(Warning, "[Game] StartScene is not set in {}", FProjectSettings::DefaultPath);
	return Settings.StartScene;
}

void UGameEngine::BeginWorld(const FString& ScenePath)
{
	FWorldContext& WorldContext = CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Game, true));
	GameWorld = WorldContext.World();

	// 액터는 SpawnActor로 BeginPlay 대기열에 들어가 첫 World Tick에서 BeginPlay가 불린다.
	// 씬이 없어도 게임은 빈 월드로 뜨게 하고, 원인은 경고 로그로 남긴다.
	if (ScenePath.empty())
		HTR_LOG(Warning, "[Game] no scene given; starting with an empty world");
	else if (!FJsonArchive::LoadWorld(GameWorld, FProjectSettings::ToNativePath(ScenePath)))
		HTR_LOG(Warning, "[Game] failed to load scene {}; starting with an empty world", ScenePath);
	else
		HTR_LOG(Info, "[Game] scene loaded: {}", ScenePath);

	// 씬 파일에는 GameMode가 없다. 불러온 뒤 플레이 월드마다 새로 스폰한다.
	GameWorld->SetGameMode();

	// 뷰포트 연결과 입력 설정이 이 카메라에 적용되므로 그 전에 정한다.
	SetupMainCamera();

	// 게임 화면은 창 전체를 덮는 뷰포트 하나다. 창은 PreInit에서 이미 만들어져 크기가 정해져 있다.
	Viewport.BeginPlay(MakeUnique<FGameViewportClient>(*GameWorld));
	Viewport.SetOutputSize(GetEngineLoop().GetViewportWidth(), GetEngineLoop().GetViewportHeight());
	// 게임 화면이 입력을 받는다. 카메라 기본 자유 비행은 FGameViewportClient가 꺼 두고, 조작은 Lua 스크립트가 한다.
	Viewport.GetGameClient()->SetInputEnabled(true);

	// 월드 준비가 끝났다. GameMode::StartPlay가 액터들의 BeginPlay를 시작시킨다 (UE LoadMap의 마지막 단계).
	GameWorld->BeginPlay();
}

// 클라이언트가 World의 카메라를 빌려 쓰므로, 뷰포트의 게임 클라이언트를 World보다 먼저 놓는다.
void UGameEngine::EndWorld()
{
	Viewport.EndPlay();
	// UE의 UGameEngine::PreExit처럼 액터 EndPlay → GameInstance Shutdown 순서로 끝낸다.
	GameWorld->EndPlay();
	SceneRenderer.EndFrame();
	ReleaseWorldShadowResources(GameWorld);
	// 컨텍스트를 지우면서 GameInstance도 Shutdown·해제한다.
	DestroyWorldContext(GameWorld);
}

void UGameEngine::TravelToScene(const FString& ScenePath)
{
	PendingScenePath = ScenePath;
}

void UGameEngine::QuitGame()
{
	GetEngineLoop().RequestExit();
}

// PIE 종료(UEditorEngine::EndPlayMap)와 같은 순서로 이전 월드를 지우고 새 씬으로 시작한다.
void UGameEngine::TravelToPendingScene()
{
	const FString ScenePath = PendingScenePath;
	PendingScenePath.clear();

	EndWorld();
	GameWorld->ClearWorld();
	delete GameWorld;
	GameWorld = nullptr;

	BeginWorld(ScenePath);
}

// 씬에 배치된 첫 ACameraActor를 메인 카메라로 지정한다. PIE 시작과 같은 규칙이다.
// 배치된 카메라가 없으면 UWorld::CreateMainCamera가 만든 기본 카메라를 그대로 쓴다.
void UGameEngine::SetupMainCamera()
{
	for (AActor* Actor : GameWorld->GetPersistentLevel()->GetActors())
	{
		if (ACameraActor* Camera = Cast<ACameraActor>(Actor))
		{
			GameWorld->SetMainCamera(Camera);
			return;
		}
	}
	HTR_LOG(Info, "[Game] no placed camera in scene; using default main camera");
}

// FEngineLoop가 스왑체인을 조정한 뒤 새 창 크기로 호출한다. 0 크기(최소화)에는 호출되지 않는다.
void UGameEngine::OnResize(uint32 Width, uint32 Height)
{
	Viewport.SetOutputSize(Width, Height);
}

// UObject(World) 일괄 삭제 전에 월드를 정리한다. World는 이후 UObject 일괄 삭제가 지운다.
void UGameEngine::PreExit()
{
	EndWorld();
}

void UGameEngine::ReleaseWorldShadowResources(UWorld* World)
{
	if (!World) return;
	FWorldContext* Context = GetWorldContextFromWorld(World);
	if (!Context) return;
	// CPU의 비소유 참조를 먼저 끊고 GPU 자원 슬롯을 반환한다.
	SceneRenderer.ReleaseWorldShadows(Context->ShadowResourceIndex);
	GetEngineLoop().GetRenderer()->ReleaseShadowResources(Context->ShadowResourceIndex);
	Context->ShadowResourceIndex = InvalidShadowResourceIndex;
}

void UGameEngine::Tick(float DeltaTime)
{
	// 지난 프레임에 요청된 씬 전환은 월드가 돌고 있지 않은 프레임 시작에 처리한다.
	if (!PendingScenePath.empty())
		TravelToPendingScene();

	UpdateGameInput();
	GameWorld->Tick(DeltaTime);

	GameWorld->GetScene().UpdateAllTransforms();
	RenderViewports();

}

void UGameEngine::UpdateGameInput()
{
	// 게임 화면은 창 전체라 창 기준 마우스 좌표가 곧 화면 기준 좌표다
	FViewportInput Input;
	Input.MousePosition = FVector2(static_cast<float>(FInputSystem::GetMouseX()),
		static_cast<float>(FInputSystem::GetMouseY()));
	Input.bEnabled = true;
	Viewport.UpdateInput(Input);
}

void UGameEngine::RenderViewports()
{
	SceneRenderer.BeginFrame();
	// TODO: 그림자 컬링 설정을 엔진 멤버로 두고 넘긴다. 지금은 FSceneRenderer 기본값을 쓴다.
	//SceneRenderer.SetShadowCullingSettings()

	//TODO: 지금은 한 개의 Viewport만 가지고 Game이 돌아가게 만들었음
	
	FViewportClient* Client = Viewport.GetActiveClient();
	Viewport.ClearSceneData();
	if (!Viewport.IsVisible() || !Client || !Client->HasCamera())
	{
		SceneRenderer.EndFrame();
		return;
	}


	Viewport.EnsureRenderTargets();
	UWorld* ViewWorld = Client->GetWorld();
	const FSceneView View = Viewport.CreateSceneView();
	// 쉐도우 맵은 World의 첫 View를 그릴 때 준비되므로, 그 전에 그림자를 쓰는 View를 등록한다.
	if (ScenePipeline.ViewSupportsShadows(View, RenderSettings))
		SceneRenderer.AddShadowView(*ViewWorld, View);
	FSceneRenderData& Data = Viewport.GetSceneRenderData();
	SceneRenderer.PrepareView(*ViewWorld, View, Data);
	// 파티클/빌보드 상수가 다음 수집으로 바뀌기 전에 이 화면을 소비한다.
	RenderFrame(Viewport, *ViewWorld, View);

	Data.ReleaseSceneReferences();
	CopyViewportToBackbuffer();


	SceneRenderer.EndFrame();
}

void UGameEngine::RenderFrame(FViewport& InViewport, UWorld& ViewWorld, const FSceneView& View)
{
	// 그림자 자원 번호는 World 컨텍스트에 보관하고 처음 쓸 때 할당한다.
	const bool bShadows = ScenePipeline.ViewSupportsShadows(View, RenderSettings);
	uint32 ShadowResourceIndex = InvalidShadowResourceIndex;
	if (bShadows)
	{
		if (FWorldContext* Context = GetWorldContextFromWorld(&ViewWorld))
		{
			if (Context->ShadowResourceIndex == InvalidShadowResourceIndex)
				Context->ShadowResourceIndex = GetEngineLoop().GetRenderer()->AllocateShadowResources(false);
			ShadowResourceIndex = Context->ShadowResourceIndex;
		}
	}

	// 1. 그림자,라이팅,불투명,반투명 
	FShadowFrameData DirectionalFrame;
	ScenePipeline.RenderScene(InViewport, ViewWorld, View, SceneRenderer, ShadowResourceIndex, bShadows, RenderSettings, DirectionalFrame);
	// 2. 후처리: Fog -> Gamma -> FXAA (표시용 sRGB)
	ScenePipeline.RenderPostProcess(InViewport, ViewWorld, View, RenderSettings);
	// 3. HUD
	ViewWorld.GetHUD().SetScreenSize(InViewport.GetOutputSize());
	ScenePipeline.RenderHUD(InViewport, ViewWorld.GetHUD(), View);
}

void UGameEngine::CopyViewportToBackbuffer()
{
	const FRenderingInfo& BackbufferInfo = GetEngineLoop().GetSwapchain()->GetRenderingInfo();
	if (BackbufferInfo.ColorRenderTargets.Num() == 0)
		return;

	// 백버퍼는 SRV가 없어 후처리 입력으로 쓸 수 없으므로 오프스크린 결과를 통째로 복사한다.
	// 창 크기 변경 직후 한 프레임은 크기가 어긋날 수 있어 그때는 건너뛴다.
	FTexture2D* Backbuffer = BackbufferInfo.ColorRenderTargets[0].Texture;
	if (!Backbuffer || !Viewport.GetColorTarget() ||
		BackbufferInfo.ViewportSetting.Width != Viewport.GetRenderingInfo().ViewportSetting.Width ||
		BackbufferInfo.ViewportSetting.Height != Viewport.GetRenderingInfo().ViewportSetting.Height)
		return;

	RenderCommand::CopyTexture(Backbuffer, Viewport.GetColorTarget());
}

