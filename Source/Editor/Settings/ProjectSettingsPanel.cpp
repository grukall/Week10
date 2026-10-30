#include "EnginePCH.h"
#include "Editor/Settings/ProjectSettingsPanel.h"

#include "Core/EngineLog.h"
#include <algorithm>

namespace
{
	// 씬은 Assets 아래 어디에 있어도 고를 수 있다.
	constexpr const char* SceneSearchRoot = "Assets";
	constexpr float CategoryWidth = 160.0f;
}

// 메뉴에서 열 때만 보이도록 닫힌 상태로 시작한다.
bool FProjectSettingsPanel::Init()
{
	SetOpen(false);
	return true;
}

void FProjectSettingsPanel::Open()
{
	Settings = FProjectSettings{};
	Settings.Load(FProjectSettings::DefaultPath);
	RefreshSceneList();
	SetOpen(true);
}

void FProjectSettingsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(640.0f, 320.0f), ImGuiCond_FirstUseEver);
	bool bOpen = IsOpen();
	const bool bVisible = ImGui::Begin(GetPanelName(), &bOpen);
	SetOpen(bOpen);
	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	// 왼쪽: 카테고리 목록
	ImGui::BeginChild("Categories", ImVec2(CategoryWidth, 0.0f), ImGuiChildFlags_Borders);
	ImGui::TextDisabled("Project");
	if (ImGui::Selectable("  Default Map", Category == ECategory::DefaultMap))
		Category = ECategory::DefaultMap;
	ImGui::EndChild();

	ImGui::SameLine();

	// 오른쪽: 선택한 카테고리의 항목
	ImGui::BeginChild("Details");
	switch (Category)
	{
	case ECategory::DefaultMap:
		DrawDefaultMap();
		break;
	}
	ImGui::EndChild();

	ImGui::End();
}

void FProjectSettingsPanel::DrawDefaultMap()
{
	ImGui::SeparatorText("Default Maps");

	const char* Preview = Settings.StartScene.empty() ? "(None)" : Settings.StartScene.c_str();
	ImGui::SetNextItemWidth(-160.0f);
	if (ImGui::BeginCombo("Game Default Map", Preview))
	{
		if (ImGui::Selectable("(None)", Settings.StartScene.empty()))
		{
			Settings.StartScene.clear();
			SaveSettings();
		}
		for (const FString& Path : ScenePaths)
		{
			const bool bSelected = Path == Settings.StartScene;
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				Settings.StartScene = Path;
				SaveSettings();
			}
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Scene the game build (HitoriGame.exe) opens on start.");

	// 목록을 만든 뒤 파일이 지워졌거나 손으로 쓴 경로가 틀린 경우를 알린다.
	std::error_code Error;
	if (!Settings.StartScene.empty() && !fs::exists(FProjectSettings::ToNativePath(Settings.StartScene), Error))
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Scene file not found. The game will start with an empty world.");

	if (ImGui::Button("Refresh"))
		RefreshSceneList();
	ImGui::SameLine();
	ImGui::TextDisabled("%d scenes under %s/", ScenePaths.Num(), SceneSearchRoot);

	ImGui::Dummy(ImVec2(0.0f, 8.0f));
	ImGui::TextDisabled("Saved to %s", FProjectSettings::DefaultPath);
}

void FProjectSettingsPanel::RefreshSceneList()
{
	ScenePaths.Reset();

	std::error_code Error;
	for (fs::recursive_directory_iterator It(SceneSearchRoot, fs::directory_options::skip_permission_denied, Error), End;
		!Error && It != End; It.increment(Error))
	{
		if (!It->is_regular_file(Error) || It->path().extension() != ".scene")
			continue;
		// 작업 디렉터리 기준 상대 경로라 다른 PC나 패키징 폴더에서도 그대로 통한다.
		const std::u8string U8 = It->path().lexically_normal().generic_u8string();
		ScenePaths.Add(FString(U8.begin(), U8.end()));
	}
	std::sort(ScenePaths.begin(), ScenePaths.end());
}

void FProjectSettingsPanel::SaveSettings()
{
	if (Settings.Save(FProjectSettings::DefaultPath))
		HTR_LOG(Info, "[Project] Game Default Map = {}", Settings.StartScene.empty() ? "(None)" : Settings.StartScene);
}
