#pragma once

#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);

	// .lua 파일을 고르게 한다. 저장소 루트 기준 상대 경로를 돌려주고, 취소하면 빈 문자열.
	static FString OpenScriptFileDialog();

	// .sequence 파일을 고르게 한다 (bSave면 저장 대화상자). 저장소 루트 기준 상대 경로, 취소하면 빈 문자열.
	static FString OpenSequenceFileDialog(bool bSave);

	// Assets/Scripts/<씬>_<액터>.lua (컴포넌트면 <씬>_<액터>_<컴포넌트>.lua)를 template.lua로 만든다.
	// 같은 이름의 파일이 이미 있으면 덮어쓰지 않는다. 저장소 루트 기준 상대 경로를 돌려주고, 실패하면 빈 문자열.
	static FString CreateScriptFile(UObject* Owner);

	// 실행 중인 Visual Studio에서 파일을 열고 포커스를 옮긴다 (devenv /edit).
	// VS를 찾지 못하면 Windows 기본 연결 프로그램으로 연다. 파일이 없으면 false.
	static bool OpenScriptInEditor(const FString& ScriptPath);

private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

	static FString CurrentScenePath;
};

