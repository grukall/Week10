#include "EnginePCH.h"
#include "Core/ProjectSettings.h"

#include "Core/EngineLog.h"

bool FProjectSettings::Load(const FString& Path)
{
	std::ifstream File(Path);
	if (!File.is_open())
	{
		HTR_LOG(Warning, "[Project] settings file not found: {}", Path);
		return false;
	}

	const json Root = json::parse(File, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		HTR_LOG(Warning, "[Project] invalid json: {}", Path);
		return false;
	}

	// 키가 없으면 기본값을 유지한다. 타입이 다르면 nlohmann이 예외를 던지므로 먼저 확인한다.
	if (const auto It = Root.find("StartScene"); It != Root.end())
	{
		if (It->is_string())
			StartScene = It->get<FString>();
		else
			HTR_LOG(Warning, "[Project] StartScene must be a string: {}", Path);
	}
	return true;
}

bool FProjectSettings::Save(const FString& Path) const
{
	// 나중에 추가될 키를 지우지 않도록 기존 내용 위에 덮어쓴다.
	json Root = json::object();
	if (std::ifstream In(Path); In.is_open())
	{
		json Existing = json::parse(In, nullptr, false);
		if (!Existing.is_discarded() && Existing.is_object())
			Root = std::move(Existing);
	}
	Root["StartScene"] = StartScene;

	std::error_code Error;
	const fs::path Parent = fs::path(Path).parent_path();
	if (!Parent.empty())
		fs::create_directories(Parent, Error);

	std::ofstream Out(Path);
	if (!Out.is_open())
	{
		HTR_LOG(Error, "[Project] failed to write settings: {}", Path);
		return false;
	}
	Out << Root.dump(2) << '\n';
	return true;
}

FString FProjectSettings::ToNativePath(const FString& Utf8Path)
{
	const std::u8string U8(Utf8Path.begin(), Utf8Path.end());
	return fs::path(U8).string();
}
