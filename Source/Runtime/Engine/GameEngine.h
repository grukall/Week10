#pragma once

#include "Engine.h"
#include "Engine/World.h"
#include "Render/SceneRenderer.h"
#include "Engine/Viewport.h"
#include "SceneRenderPipeline.h"

class UGameEngine final: public UEngine
{
	DECLARE_CLASS(UGameEngine, UEngine)
public:
	UGameEngine() = default;
	~UGameEngine() = default;

	UGameEngine(const UGameEngine&) = delete;
	UGameEngine& operator=(const UGameEngine&) = delete;

	FEngineConfig GetConfig() const override;

	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnResize(uint32 Width, uint32 Height) override;
	void PreExit() override;

	void TravelToScene(const FString& ScenePath) override;
	void QuitGame() override;

	FSceneRenderSettings RenderSettings{};

private:
	UWorld* GameWorld = nullptr;
	// Lua가 요청한 다음 씬. 월드 Tick 도중에 월드를 지울 수 없으므로 다음 프레임 시작에 바꾼다.
	FString PendingScenePath;
	FSceneRenderer SceneRenderer;
	//FXAA나 Fog Renderer를 보관중
	FSceneRenderPipeline ScenePipeline;
	FViewport Viewport;
	

	FString GetStartScenePath() const;
	// 새 월드를 만들어 씬을 불러오고 뷰포트에 붙인 뒤 BeginPlay까지 한다. 경로가 비면 빈 월드다.
	void BeginWorld(const FString& ScenePath);
	// 뷰포트에서 월드를 떼고 EndPlay·컨텍스트 정리까지 한다. World 객체 자체는 지우지 않는다.
	void EndWorld();
	void TravelToPendingScene();
	void SetupMainCamera();
	void UpdateGameInput();
	void RenderViewports();

	void RenderFrame(FViewport& InViewport, UWorld& ViewWorld, const FSceneView& View);
	// 뷰포트의 오프스크린 결과를 메인 스왑체인 백버퍼로 복사한다. Present는 FEngineLoop가 한다.
	void CopyViewportToBackbuffer();
	void ReleaseWorldShadowResources(UWorld* World);
	
};