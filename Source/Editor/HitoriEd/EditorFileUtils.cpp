#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include <shellapi.h>

namespace
{
	struct FFileDialogOptions
	{
		const char* Filter = nullptr;        // "Name (*.ext)\0*.ext\0..."
		const char* DefaultExt = nullptr;
		FString InitialDir;                  // 비어 있으면 시스템이 정한다
		bool bSave = false;                  // 저장 대화상자 (덮어쓰기 확인)
		bool bOwnedByActiveWindow = false;   // 활성 창을 부모로 두어 그 위에 뜨게 한다
		bool bProjectRelative = false;       // 작업 디렉터리(저장소 루트) 기준 상대 경로로 돌려준다
	};

	// Windows 열기·저장 대화상자. 취소하면 빈 문자열
	FString ShowFileDialog(const FFileDialogOptions& Options)
	{
		char FilePath[MAX_PATH] = {};

		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(OPENFILENAMEA);
		ofn.hwndOwner = Options.bOwnedByActiveWindow ? GetActiveWindow() : nullptr;
		ofn.lpstrFile = FilePath;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = Options.Filter;
		ofn.lpstrDefExt = Options.DefaultExt;
		ofn.lpstrInitialDir = Options.InitialDir.empty() ? nullptr : Options.InitialDir.c_str();
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (Options.bSave ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

		const BOOL bOk = Options.bSave ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
		if (!bOk)
			return FString();
		if (!Options.bProjectRelative)
			return FString(FilePath);

		// 씬·시퀀스 파일에 절대 경로가 남으면 다른 PC에서 열 수 없으므로 작업 디렉터리(저장소 루트) 기준으로 바꾼다
		std::error_code Ec;
		const std::filesystem::path Relative = std::filesystem::relative(FilePath, std::filesystem::current_path(), Ec);
		if (Ec || Relative.empty())
			return FString(FilePath);
		return Relative.generic_string();
	}

	// 저장소 루트 아래 폴더. 없으면 bCreate일 때 만들고, 그래도 없으면 빈 문자열
	FString ProjectFolder(const std::filesystem::path& SubPath, bool bCreate)
	{
		const std::filesystem::path Folder = std::filesystem::current_path() / SubPath;
		std::error_code Ec;
		if (bCreate)
			std::filesystem::create_directories(Folder, Ec);
		return std::filesystem::is_directory(Folder, Ec) ? Folder.string() : FString();
	}
}

namespace
{
	// 콘솔 창 없이 프로그램을 실행하고 표준 출력을 받아 온다
	std::string RunAndCaptureOutput(std::wstring CommandLine)
	{
		SECURITY_ATTRIBUTES Sa{ sizeof(Sa), nullptr, TRUE };
		HANDLE ReadPipe = nullptr;
		HANDLE WritePipe = nullptr;
		if (!CreatePipe(&ReadPipe, &WritePipe, &Sa, 0))
			return {};
		SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW Si{ sizeof(Si) };
		Si.dwFlags = STARTF_USESTDHANDLES;
		Si.hStdOutput = WritePipe;
		Si.hStdError = WritePipe;

		PROCESS_INFORMATION Pi{};
		const BOOL bStarted = CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &Si, &Pi);
		CloseHandle(WritePipe);

		std::string Output;
		if (bStarted)
		{
			char Buffer[512];
			DWORD Read = 0;
			while (ReadFile(ReadPipe, Buffer, sizeof(Buffer), &Read, nullptr) && Read > 0)
				Output.append(Buffer, Read);
			WaitForSingleObject(Pi.hProcess, 5000);
			CloseHandle(Pi.hProcess);
			CloseHandle(Pi.hThread);
		}
		CloseHandle(ReadPipe);
		return Output;
	}

	// vswhere(VS 설치 시 함께 깔리는 설치 정보 조회 도구)로 최신 devenv.exe 경로를 찾는다. 한 번만 조회한다.
	const std::wstring& FindDevenvPath()
	{
		static std::wstring Cached;
		static bool bSearched = false;
		if (bSearched)
			return Cached;
		bSearched = true;

		const wchar_t* ProgramFilesX86 = _wgetenv(L"ProgramFiles(x86)");
		if (!ProgramFilesX86)
			return Cached;

		const std::filesystem::path VsWhere = std::filesystem::path(ProgramFilesX86) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
		if (!std::filesystem::exists(VsWhere))
			return Cached;

		std::string Output = RunAndCaptureOutput(L"\"" + VsWhere.wstring() + L"\" -latest -prerelease -property productPath -utf8");
		while (!Output.empty() && (Output.back() == '\n' || Output.back() == '\r' || Output.back() == ' '))
			Output.pop_back();
		if (Output.empty())
			return Cached;

		const int Len = MultiByteToWideChar(CP_UTF8, 0, Output.c_str(), static_cast<int>(Output.size()), nullptr, 0);
		std::wstring Path(Len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Output.c_str(), static_cast<int>(Output.size()), Path.data(), Len);

		if (std::filesystem::exists(Path))
			Cached = Path;
		return Cached;
	}

	// 파일 이름에 쓸 수 없는 문자와 공백을 _로 바꾼다
	FString MakeSafeFileName(const FString& Name)
	{
		FString Result = Name;
		for (char& C : Result)
		{
			if (C == ' ' || C == '<' || C == '>' || C == ':' || C == '"' || C == '/' || C == '\\' || C == '|' || C == '?' || C == '*'
				|| static_cast<unsigned char>(C) < 32)
			{
				C = '_';
			}
		}
		return Result;
	}

	// template.lua가 없을 때 쓰는 기본 내용
	constexpr const char* DefaultScriptSource =
		"function BeginPlay()\n"
		"end\n"
		"\n"
		"function EndPlay()\n"
		"end\n"
		"\n"
		"function Tick(dt)\n"
		"end\n";
}

FString FEditorFileUtils::CurrentScenePath = "";

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
		return false;

	World->ClearWorld();

	CurrentScenePath.clear();

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
		return false;

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);

		return false;
	}

	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::LoadWorld(World, FilePath))
		return false;

	// 이제 이 파일이 현재 Scene
	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	FFileDialogOptions Options;
	Options.Filter = "Scene Files (*.scene)\0*.scene\0";
	Options.DefaultExt = "scene";
	Options.InitialDir = "Scene/";
	Options.bSave = true;
	return ShowFileDialog(Options);
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	FFileDialogOptions Options;
	Options.Filter =
		"Scene Files (*.scene)\0*.scene\0"
		"All Files (*.*)\0*.*\0";
	Options.DefaultExt = "scene";
	Options.InitialDir = "Scene/";
	return ShowFileDialog(Options);
}

FString FEditorFileUtils::OpenScriptFileDialog()
{
	FFileDialogOptions Options;
	Options.Filter =
		"Lua Scripts (*.lua)\0*.lua\0"
		"All Files (*.*)\0*.*\0";
	Options.DefaultExt = "lua";
	Options.InitialDir = ProjectFolder(std::filesystem::path("Assets") / "Scripts", false);
	Options.bOwnedByActiveWindow = true;
	Options.bProjectRelative = true;
	return ShowFileDialog(Options);
}

FString FEditorFileUtils::OpenSequenceFileDialog(bool bSave)
{
	FFileDialogOptions Options;
	Options.Filter =
		"Level Sequences (*.sequence)\0*.sequence\0"
		"All Files (*.*)\0*.*\0";
	Options.DefaultExt = "sequence";
	Options.InitialDir = ProjectFolder(std::filesystem::path("Assets") / "Sequences", true);
	Options.bSave = bSave;
	Options.bOwnedByActiveWindow = true;
	Options.bProjectRelative = true;
	return ShowFileDialog(Options);
}

FString FEditorFileUtils::CreateScriptFile(UObject* Owner)
{
	if (!Owner)
		return FString();

	// 저장하지 않은 씬은 이름이 없으므로 Untitled로 둔다
	const FString SceneName = CurrentScenePath.empty()
		? FString("Untitled")
		: std::filesystem::path(CurrentScenePath).stem().string();

	// 컴포넌트 이름은 액터마다 겹치기 쉬워서(UAmbientLightComponent 등) 소유 액터 이름을 함께 붙인다
	FString BaseName;
	if (UActorComponent* Component = Cast<UActorComponent>(Owner))
	{
		const AActor* OwnerActor = Component->GetOwner();
		BaseName = SceneName + "_" + (OwnerActor ? OwnerActor->GetName() : FString("NoOwner")) + "_" + Component->GetName();
	}
	else
	{
		BaseName = SceneName + "_" + Owner->GetName();
	}
	BaseName = MakeSafeFileName(BaseName);

	const std::filesystem::path Root = std::filesystem::current_path();
	const std::filesystem::path ScriptDir = Root / "Assets" / "Scripts";
	const std::filesystem::path ScriptFile = ScriptDir / (BaseName + ".lua");
	const FString RelativePath = std::filesystem::relative(ScriptFile, Root).generic_string();

	std::error_code Ec;
	std::filesystem::create_directories(ScriptDir, Ec);

	// 이미 있으면 덮어쓰지 않고 그 파일을 연결한다 (작성해 둔 스크립트를 날리지 않도록)
	if (std::filesystem::exists(ScriptFile))
	{
		HTR_LOG(Info, "Script already exists, linked : {}", RelativePath);
		return RelativePath;
	}

	const std::filesystem::path TemplateFile = ScriptDir / "template.lua";
	if (std::filesystem::exists(TemplateFile))
	{
		std::filesystem::copy_file(TemplateFile, ScriptFile, Ec);
	}
	else
	{
		std::ofstream Out(ScriptFile);
		if (Out)
			Out << DefaultScriptSource;
		else
			Ec = std::make_error_code(std::errc::io_error);
	}

	if (Ec)
	{
		HTR_LOG(Error, "Failed to create script : {} ({})", RelativePath, Ec.message());
		return FString();
	}

	HTR_LOG(Info, "Script created : {}", RelativePath);
	return RelativePath;
}

bool FEditorFileUtils::OpenScriptInEditor(const FString& ScriptPath)
{
	if (ScriptPath.empty())
		return false;

	const std::filesystem::path FilePath = std::filesystem::absolute(ScriptPath).lexically_normal();
	if (!std::filesystem::exists(FilePath))
	{
		HTR_LOG(Error, "Script file not found : {}", ScriptPath);
		return false;
	}

	// 실행되는 쪽(VS)이 창을 앞으로 가져올 수 있게 허락한다. 없으면 Windows가 포커스 이동을 막고 작업 표시줄만 깜빡인다.
	AllowSetForegroundWindow(ASFW_ANY);

	const std::wstring& Devenv = FindDevenvPath();
	if (!Devenv.empty())
	{
		// /edit : 이미 실행 중인 VS 인스턴스에서 파일을 연다 (없으면 가벼운 창으로 새로 띄운다)
		std::wstring CommandLine = L"\"" + Devenv + L"\" /edit \"" + FilePath.wstring() + L"\"";
		STARTUPINFOW Si{ sizeof(Si) };
		PROCESS_INFORMATION Pi{};
		if (CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Si, &Pi))
		{
			CloseHandle(Pi.hProcess);
			CloseHandle(Pi.hThread);
			return true;
		}
		HTR_LOG(Warning, "Failed to launch Visual Studio, falling back to default app");
	}

	// VS를 못 찾으면 .lua에 연결된 기본 프로그램으로 연다
	const HINSTANCE Result = ShellExecuteW(nullptr, L"open", FilePath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(Result) > 32;
}


