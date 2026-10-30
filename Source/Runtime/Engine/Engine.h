#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

#include "Containers/IndirectArray.h"

#include "Engine/World.h"
#include "EngineTypes.h"

#include "Asset/AssetManager.h"

class UGameInstance;

struct FWorldContext
{
	EWorldType WorldType;
	FName ContextHandle;
	// Renderer 소유 World별 그림자 자원 슬롯의 비소유 번호. 저장/복제하지 않는다.
	uint32 ShadowResourceIndex = InvalidShadowResourceIndex;

	// Game·PIE 컨텍스트만 가진다(에디터 컨텍스트는 nullptr). 컨텍스트와 함께 만들어지고 지워진다.
	UGameInstance* OwningGameInstance = nullptr;

	// 월드를 바꿔 끼우면 새 월드에 OwningGameInstance를 연결한다.
	void SetCurrentWorld(UWorld* World);

	inline UWorld* World() const
	{
		return ThisCurrentWorld;
	}

	FWorldContext();
	~FWorldContext();
	// WorldList는 포인터만 이동한다. 세션 자체의 복사/이동은 허용하지 않는다.
	FWorldContext(const FWorldContext&) = delete;
	FWorldContext& operator=(const FWorldContext&) = delete;
	FWorldContext(FWorldContext&&) = delete;
	FWorldContext& operator=(FWorldContext&&) = delete;

private:
	UWorld* ThisCurrentWorld = nullptr;
};

class FEngineLoop;

// 엔진 모드별 실행 설정. UE에서는 ini가 맡는 부분이다.
// Width·Height가 0이면 주 모니터 해상도를 쓴다.
struct FEngineConfig
{
	const wchar_t* Title = L"Hitori";
	uint32 Width = 0;
	uint32 Height = 0;
	bool bBorderless = true;
	uint32 SyncInterval = 0;   // 0 = VSync 끔 (FPS 측정용)
	bool bCreateDepthBuffer = true;   // 백버퍼에 3D를 직접 그리지 않는 엔진(에디터)은 끈다.
	bool bExitOnEscape = true;        // 테두리 없는 창에는 닫기 버튼이 없다.
	const char* SplashImage = nullptr;
};

// 모든 엔진 모드의 베이스. 플랫폼·루프·Present는 FEngineLoop가 맡고,
// 서브클래스(Game, Editor)는 Tick에서 한 프레임의 갱신과 렌더를 채운다.
class UEngine : public UObject
{
	DECLARE_CLASS(UEngine, UObject)
	friend class FEngineLoop;
public:
	// 창을 만들기 전에 호출된다.
	virtual FEngineConfig GetConfig() const { return {}; }

	// Device·Window·Swapchain·AssetManager가 준비된 뒤 호출된다.
	virtual bool Init();
	// 한 프레임의 갱신과 모든 렌더 패스를 기록한다. Present는 FEngineLoop가 호출 직후에 한다.
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnResize(uint32 Width, uint32 Height) {}
	// UObject 일괄 정리와 GPU 자원 해제 전에 호출된다.
	virtual void PreExit() {};

	// 게임 코드(Lua)가 다른 씬으로 넘어가기를 요청한다. 실제 전환은 다음 프레임 시작에 한다 (UE OpenLevel).
	virtual void TravelToScene(const FString& ScenePath);
	// 게임 종료 요청. 게임은 앱을 끝내고, 에디터는 PIE 세션만 끝낸다 (UE QuitGame).
	virtual void QuitGame();

	//UWorld* GetWorld() const { return World; }

	FWorldContext& CreateNewWorldContext(EWorldType WorldType);
	FWorldContext* GetWorldContextFromWorld(const UWorld* InWorld);
	void DestroyWorldContext(UWorld* InWorld);

	UAssetManager* AssetManager;
protected:
	FEngineLoop& GetEngineLoop() const { return *EngineLoop; }

	TIndirectArray<FWorldContext> WorldList;
	//UWorld* World = nullptr;
	int32 NextWorldContextHandle = 0;
private:
	FEngineLoop* EngineLoop = nullptr;
};

// FEngineLoop::PreInit에서 설정되고 Exit에서 해제된다.
extern UEngine* GEngine;
