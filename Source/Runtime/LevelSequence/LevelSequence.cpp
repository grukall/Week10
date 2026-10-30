#include "EnginePCH.h"
#include "LevelSequence.h"

#include "Core/ProjectSettings.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

#include <fstream>
#include <filesystem>

namespace
{
	constexpr int32 SequenceVersion = 1;

	const char* GetInterpName(ESequenceInterp Interp)
	{
		switch (Interp)
		{
		case ESequenceInterp::Constant: return "Constant";
		case ESequenceInterp::Linear:   return "Linear";
		case ESequenceInterp::Cubic:    return "Cubic";
		}
		return "Cubic";
	}

	ESequenceInterp ParseInterpName(const FString& Name)
	{
		if (Name == "Constant") return ESequenceInterp::Constant;
		if (Name == "Linear")   return ESequenceInterp::Linear;
		return ESequenceInterp::Cubic;
	}

	// 채널은 이름을 키로 적는다: { "Location.X": [ {Time, Value, Interp}, ... ] }
	// 키가 없는 채널은 적지 않는다.
	json SectionToJson(const FSequenceSection& Section, ESequenceTrackType Type)
	{
		json Channels = json::object();
		for (int32 i = 0; i < Section.Channels.Num(); ++i)
		{
			const FSequenceChannel& Channel = Section.Channels[i];
			if (!Channel.HasKeys())
				continue;

			json Keys = json::array();
			for (const FSequenceKey& Key : Channel.Keys)
				Keys.push_back({ {"Time", Key.Time}, {"Value", Key.Value}, {"Interp", GetInterpName(Key.Interp)} });
			Channels[FSequenceTrack::GetChannelName(Type, i)] = std::move(Keys);
		}

		json Out = { {"Start", Section.StartTime}, {"End", Section.EndTime}, {"Channels", std::move(Channels)} };
		if (Type == ESequenceTrackType::Event)
		{
			json Events = json::array();
			for (const FSequenceEventKey& Event : Section.Events)
				Events.push_back({ {"Time", Event.Time}, {"Function", Event.Function}, {"Args", Event.Args} });
			Out["Events"] = std::move(Events);
		}
		return Out;
	}

	void SectionFromJson(const json& In, ESequenceTrackType Type, FSequenceSection& Out)
	{
		Out.StartTime = In.value("Start", 0.0f);
		Out.EndTime = In.value("End", 0.0f);
		Out.Channels.SetNum(FSequenceTrack::GetChannelCount(Type));

		const json Channels = In.value("Channels", json::object());
		for (int32 i = 0; i < Out.Channels.Num(); ++i)
		{
			const auto It = Channels.find(FSequenceTrack::GetChannelName(Type, i));
			if (It == Channels.end() || !It->is_array())
				continue;

			FSequenceChannel& Channel = Out.Channels[i];
			for (const json& KeyJson : *It)
			{
				FSequenceKey& Key = Channel.Keys.AddDefaulted_GetRef();
				Key.Time = KeyJson.value("Time", 0.0f);
				Key.Value = KeyJson.value("Value", 0.0f);
				Key.Interp = ParseInterpName(KeyJson.value("Interp", FString("Cubic")));
			}
			// 손으로 고친 파일이라도 시간순으로 읽히게 한다
			Channel.SortKeys();
		}

		for (const json& EventJson : In.value("Events", json::array()))
		{
			FSequenceEventKey& Event = Out.Events.AddDefaulted_GetRef();
			Event.Time = EventJson.value("Time", 0.0f);
			Event.Function = EventJson.value("Function", FString());
			Event.Args = EventJson.value("Args", json::array());
		}
		std::stable_sort(Out.Events.begin(), Out.Events.end(),
			[](const FSequenceEventKey& A, const FSequenceEventKey& B) { return A.Time < B.Time; });
	}

	json TracksToJson(const TArray<FSequenceTrack>& Tracks)
	{
		json TracksJson = json::array();
		for (const FSequenceTrack& Track : Tracks)
		{
			json SectionsJson = json::array();
			for (const FSequenceSection& Section : Track.Sections)
				SectionsJson.push_back(SectionToJson(Section, Track.Type));

			TracksJson.push_back({
				{"Type", FSequenceTrack::GetTypeName(Track.Type)},
				{"Property", Track.PropertyPath},
				{"Sections", std::move(SectionsJson)},
			});
		}
		return TracksJson;
	}

	// Owner는 경고 메시지에 쓰는 이름 (바인딩 이름 또는 "Master")
	void TracksFromJson(const json& In, const FString& Owner, TArray<FSequenceTrack>& Out)
	{
		Out.Reset();
		for (const json& TrackJson : In)
		{
			ESequenceTrackType Type;
			const FString TypeName = TrackJson.value("Type", FString());
			if (!FSequenceTrack::ParseTypeName(TypeName, Type))
			{
				HTR_LOG(Warning, "LevelSequence: unknown track type '{}' in '{}', skipped", TypeName, Owner);
				continue;
			}

			FSequenceTrack& Track = Out.AddDefaulted_GetRef();
			Track.Type = Type;
			Track.PropertyPath = TrackJson.value("Property", FString());
			for (const json& SectionJson : TrackJson.value("Sections", json::array()))
				SectionFromJson(SectionJson, Type, Track.Sections.AddDefaulted_GetRef());
		}
	}
}

ULevelSequence* ULevelSequence::LoadFromFile(const FString& Path)
{
	const FString NativePath = FProjectSettings::ToNativePath(Path);
	std::ifstream File(NativePath);
	if (!File.is_open())
	{
		HTR_LOG(Warning, "LevelSequence: cannot open '{}'", Path);
		return nullptr;
	}

	const json Root = json::parse(File, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		HTR_LOG(Warning, "LevelSequence: invalid json '{}'", Path);
		return nullptr;
	}

	ULevelSequence* Sequence = NewObject<ULevelSequence>();
	if (!Sequence->FromJson(Root))
	{
		HTR_LOG(Warning, "LevelSequence: unsupported file '{}'", Path);
		delete Sequence;
		return nullptr;
	}

	Sequence->AssetPath = Path;
	return Sequence;
}

bool ULevelSequence::SaveToFile(const FString& Path) const
{
	const FString NativePath = FProjectSettings::ToNativePath(Path);
	const std::filesystem::path Parent = std::filesystem::path(NativePath).parent_path();
	if (!Parent.empty())
		std::filesystem::create_directories(Parent);

	std::ofstream File(NativePath);
	if (!File.is_open())
	{
		HTR_LOG(Warning, "LevelSequence: cannot write '{}'", Path);
		return false;
	}

	json Root;
	ToJson(Root);
	File << Root.dump(4) << '\n';
	if (File.fail())
		return false;

	AssetPath = Path;
	return true;
}

void ULevelSequence::ToJson(json& Out) const
{
	json BindingsJson = json::array();
	for (const FSequenceBinding& Binding : Bindings)
	{
		BindingsJson.push_back({
			{"Name", Binding.Name},
			{"Tag", Binding.Tag},
			{"Template", Binding.HasTemplate() ? Binding.Template : json(nullptr)},
			{"CineCamera", Binding.bIsCineCamera},
			{"Tracks", TracksToJson(Binding.Tracks)},
		});
	}

	Out = {
		{"Version", SequenceVersion},
		{"Duration", Duration},
		{"DisplayRate", DisplayRate},
		{"CameraBlendIn", CameraBlendInTime},
		{"CameraBlendOut", CameraBlendOutTime},
		{"CameraBlendFunction", CameraBlendFunction},
		{"CameraBlendExp", CameraBlendExp},
		{"CameraCutEnd", CineCameraEndTime},
		{"Effects", Effects.is_object() ? Effects : json::object()},
		{"MasterTracks", TracksToJson(MasterTracks)},
		{"Bindings", std::move(BindingsJson)},
	};
}

bool ULevelSequence::FromJson(const json& In)
{
	if (In.value("Version", 0) != SequenceVersion)
		return false;

	Duration = In.value("Duration", 5.0f);
	DisplayRate = In.value("DisplayRate", 30.0f);
	CameraBlendInTime = In.value("CameraBlendIn", 0.0f);
	CameraBlendOutTime = In.value("CameraBlendOut", 0.0f);
	CameraBlendFunction = In.value("CameraBlendFunction", FString("Cubic"));
	CameraBlendExp = In.value("CameraBlendExp", 2.0f);
	CineCameraEndTime = In.value("CameraCutEnd", -1.0f);
	Effects = In.value("Effects", json::object());
	TracksFromJson(In.value("MasterTracks", json::array()), "Master", MasterTracks);

	Bindings.Reset();
	for (const json& BindingJson : In.value("Bindings", json::array()))
	{
		FSequenceBinding& Binding = Bindings.AddDefaulted_GetRef();
		Binding.Name = BindingJson.value("Name", FString());
		Binding.Tag = BindingJson.value("Tag", FString());
		Binding.Template = BindingJson.value("Template", json(nullptr));
		Binding.bIsCineCamera = BindingJson.value("CineCamera", false);
		TracksFromJson(BindingJson.value("Tracks", json::array()), Binding.Name, Binding.Tracks);
	}
	return true;
}

int32 ULevelSequence::FindCineCameraBinding() const
{
	for (int32 i = 0; i < Bindings.Num(); ++i)
	{
		if (Bindings[i].bIsCineCamera)
			return i;
	}
	return INDEX_NONE;
}

FSequenceBinding& ULevelSequence::AddBinding(const FString& Name)
{
	FSequenceBinding& Binding = Bindings.AddDefaulted_GetRef();
	Binding.Name = Name;
	return Binding;
}

json ULevelSequence::CaptureTemplate(AActor* Actor)
{
	if (!Actor)
		return json(nullptr);

	// 씬 저장과 같은 경로로 적은 뒤, 시퀀스 파일에 넣을 수 있게 json 트리로 다시 읽는다
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);

		FStructuredArchive::FRecord Record = Ar.Open().EnterRecord();
		FString ClassName = Actor->GetClass()->Name;
		Record << SA_VALUE("Class", ClassName);
		Actor->Serialize(Record);

		Ar.Close();
	}

	const FString Text(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	json Template = json::parse(Text, nullptr, false);
	return Template.is_discarded() ? json(nullptr) : Template;
}

AActor* ULevelSequence::SpawnFromTemplate(UWorld* World, const json& Template)
{
	if (!World || !World->GetPersistentLevel() || !Template.is_object())
		return nullptr;

	const FString Text = Template.dump();
	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Text.size()));
	std::memcpy(Bytes.GetData(), Text.data(), Text.size());

	FMemoryReader Reader(Bytes);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
		return nullptr;

	FStructuredArchive Ar(Formatter);
	FStructuredArchive::FRecord Record = Ar.Open().EnterRecord();
	AActor* Actor = World->GetPersistentLevel()->SpawnActorFromRecord(Record);
	Ar.Close();

	if (Actor)
		Actor->SetFlags(EObjectFlags::RF_Transient);
	return Actor;
}
