#include "EnginePCH.h"
#include "Engine.h"
#include "Engine/GameInstance.h"
#include "Script/LuaScriptManager.h"
#include "Audio/AudioManager.h"

UEngine* GEngine = nullptr;

bool UEngine::Init()
{
	/*World = FObjectFactory::ConstructObject<UWorld>();

	if (!World || !World->Init()) return false;*/

	FAudioManager::Get().Initialize();
	FLuaScriptManager::Get().Initialize();

 	return true;
}

void UEngine::TravelToScene(const FString& ScenePath)
{
	HTR_LOG(Warning, "[Engine] TravelToScene({}) is not supported by this engine mode", ScenePath);
}

void UEngine::QuitGame()
{
	HTR_LOG(Warning, "[Engine] QuitGame is not supported by this engine mode");
}

FWorldContext& UEngine::CreateNewWorldContext(EWorldType WorldType)
{
	FWorldContext* NewWorldContext = new FWorldContext();
	WorldList.Add(NewWorldContext);
	NewWorldContext->WorldType = WorldType;
	NewWorldContext->ContextHandle = FName();

	// 플레이하는 컨텍스트만 GameInstance를 가진다. UE도 에디터 컨텍스트에는 없다.
	if (WorldType == EWorldType::Game || WorldType == EWorldType::PIE)
	{
		NewWorldContext->OwningGameInstance = NewObject<UGameInstance>(this);
		NewWorldContext->OwningGameInstance->Init();
	}

	return *NewWorldContext;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* InWorld)
{
	for (FWorldContext& WorldContext : WorldList)
	{
		if (WorldContext.World() == InWorld)
		{
			return &WorldContext;
		}
	}
	return nullptr;
}

void UEngine::DestroyWorldContext(UWorld* InWorld)
{
	for (int32 idx = 0; idx < WorldList.Num(); ++idx)
	{
		if (WorldList[idx].World() == InWorld)
		{
			//WorldContextDestroyedEvent.Broadcast(WorldList[idx]);

			FWorldContext& Context = WorldList[idx];
			// 월드가 지워진 GameInstance를 가리키지 않도록 먼저 끊는다.
			if (UWorld* World = Context.World())
				World->SetGameInstance(nullptr);
			Context.SetCurrentWorld(NULL);

			if (Context.OwningGameInstance)
			{
				Context.OwningGameInstance->Shutdown();
				delete Context.OwningGameInstance;
				Context.OwningGameInstance = nullptr;
			}
			WorldList.RemoveAt(idx);
			break;
		}
	}
}

void FWorldContext::SetCurrentWorld(UWorld* World)
{
	ThisCurrentWorld = World;
	if (World)
		World->SetGameInstance(OwningGameInstance);
}

FWorldContext::FWorldContext() : WorldType(EWorldType::None), ContextHandle(NAME_None) {}
FWorldContext::~FWorldContext() = default;
