#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/ProjectSettings.h"

// Edit > Project Settings. 빌드(게임 exe)가 따르는 프로젝트 공용 설정을 편집해 Config/Project.json에 저장한다.
// editor.ini(개인 작업 설정)를 다루는 Settings 패널과 저장 대상이 다르다.
class FProjectSettingsPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Project Settings"; }

	// 열 때마다 디스크의 설정과 씬 목록을 다시 읽는다.
	void Open();

private:
	enum class ECategory
	{
		DefaultMap,
	};

	void DrawDefaultMap();
	// Assets 아래의 .scene 파일을 작업 디렉터리 기준 상대 경로로 모은다.
	void RefreshSceneList();
	void SaveSettings();

	FProjectSettings Settings;
	TArray<FString> ScenePaths;   // UTF-8 상대 경로 (예: Assets/Scenes/Main.scene)
	ECategory Category = ECategory::DefaultMap;
};
