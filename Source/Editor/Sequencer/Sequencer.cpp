#include "EnginePCH.h"
#include "Editor/Sequencer/Sequencer.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/SceneComponent.h"
#include "UObject/Class.h"
#include "UObject/Property.h"
#include "Camera/WaveOscillatorCameraShakePattern.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace
{
	// 엔진 종료 때 UObject가 일괄 삭제되므로, 아직 살아 있을 때만 지운다
	bool IsAlive(UObject* Object, uint32 ObjectUUID)
	{
		return Object && FindObjectByUUID(ObjectUUID) == Object;
	}

	bool IsStepTrack(ESequenceTrackType Type)
	{
		return Type == ESequenceTrackType::Bool || Type == ESequenceTrackType::Spawn;
	}

	// Items를 시간순으로 다시 놓고, 같은 배열을 가리키는 선택 핸들의 Key 인덱스를 새 자리로 옮긴다
	template <typename T, typename TTimeOf, typename TMatches>
	void SortWithRemap(TArray<T>& Items, TTimeOf TimeOf, TMatches Matches, TArray<FSequencerKeyHandle>& Selection)
	{
		const int32 Num = Items.Num();
		std::vector<int32> Order(Num);
		std::iota(Order.begin(), Order.end(), 0);
		std::stable_sort(Order.begin(), Order.end(), [&](int32 A, int32 B) { return TimeOf(Items[A]) < TimeOf(Items[B]); });

		bool bChanged = false;
		for (int32 i = 0; i < Num && !bChanged; ++i)
			bChanged = Order[i] != i;
		if (!bChanged)
			return;

		std::vector<int32> NewIndexOf(Num);
		TArray<T> Sorted;
		Sorted.Reserve(Num);
		for (int32 NewIndex = 0; NewIndex < Num; ++NewIndex)
		{
			NewIndexOf[Order[NewIndex]] = NewIndex;
			Sorted.Add(Items[Order[NewIndex]]);
		}
		Items = std::move(Sorted);

		for (FSequencerKeyHandle& Handle : Selection)
		{
			if (Matches(Handle) && Handle.Key >= 0 && Handle.Key < Num)
				Handle.Key = NewIndexOf[Handle.Key];
		}
	}
}

FSequencer::~FSequencer()
{
	Shutdown();
}

void FSequencer::Shutdown()
{
	DeletePlayerAndSequence();
	Sequence = nullptr;
	Player = nullptr;
}

void FSequencer::DeletePlayerAndSequence()
{
	bPlaying = false;
	bPreviewActive = false;

	if (IsAlive(Player, PlayerUUID))
	{
		Player->InvalidateBindings();
		delete Player;
	}
	Player = nullptr;
	PlayerUUID = 0;

	if (IsAlive(Sequence, SequenceUUID))
		delete Sequence;
	Sequence = nullptr;
	SequenceUUID = 0;
}

void FSequencer::SetWorld(UWorld* InWorld)
{
	if (World == InWorld)
		return;

	ReleasePreview();
	World = InWorld;
	ResetPlayer();
}

void FSequencer::ResetPlayer()
{
	if (!Sequence || !World)
		return;

	if (!IsAlive(Player, PlayerUUID))
	{
		Player = NewObject<ULevelSequencePlayer>();
		PlayerUUID = Player->GetUUID();
	}

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bRestoreState = true;
	Player->Initialize(Sequence, World, Settings);
	bPreviewActive = false;
}

// ---------------------------------------------------------------- 에셋

void FSequencer::NewSequence()
{
	DeletePlayerAndSequence();

	Sequence = NewObject<ULevelSequence>();
	SequenceUUID = Sequence->GetUUID();
	Path.clear();
	bDirty = false;
	Time = 0.0f;
	UndoStack.Reset();
	RedoStack.Reset();
	ClearSelection();
	SelectedBinding = -2;

	ResetPlayer();
	SetTime(0.0f);
}

bool FSequencer::OpenSequence(const FString& InPath)
{
	ULevelSequence* Loaded = ULevelSequence::LoadFromFile(InPath);
	if (!Loaded)
		return false;

	DeletePlayerAndSequence();

	Sequence = Loaded;
	SequenceUUID = Sequence->GetUUID();
	Path = InPath;
	bDirty = false;
	Time = 0.0f;
	UndoStack.Reset();
	RedoStack.Reset();
	ClearSelection();
	SelectedBinding = -2;

	ResetPlayer();
	SetTime(0.0f);
	return true;
}

bool FSequencer::SaveSequence()
{
	if (!Sequence || Path.empty())
		return false;
	if (!Sequence->SaveToFile(Path))
		return false;

	bDirty = false;
	HTR_LOG(Info, "Sequencer: saved {}", Path);
	return true;
}

bool FSequencer::SaveSequenceAs(const FString& InPath)
{
	if (!Sequence || InPath.empty())
		return false;

	const FString Previous = Path;
	Path = InPath;
	if (SaveSequence())
		return true;

	Path = Previous;
	return false;
}

// ---------------------------------------------------------------- 시간과 미리보기

void FSequencer::SetTime(float NewTime)
{
	if (!Sequence)
		return;

	Time = std::clamp(NewTime, 0.0f, Sequence->Duration);
	if (Player && World)
	{
		Player->SetPlaybackPosition(Time);
		bPreviewActive = true;
	}
}

float FSequencer::SnapTime(float InTime) const
{
	if (!bSnap || !Sequence || Sequence->DisplayRate <= 0.0f)
		return InTime;
	return std::round(InTime * Sequence->DisplayRate) / Sequence->DisplayRate;
}

void FSequencer::TogglePlay()
{
	if (!Sequence)
		return;

	bPlaying = !bPlaying;
	if (bPlaying && Time >= Sequence->Duration)
		Time = 0.0f;
}

void FSequencer::Tick(float DeltaTime)
{
	if (!bPlaying || !Sequence)
		return;

	float NewTime = Time + DeltaTime;
	if (NewTime >= Sequence->Duration)
	{
		if (bLoop && Sequence->Duration > 0.0f)
		{
			NewTime = std::fmod(NewTime, Sequence->Duration);
		}
		else
		{
			NewTime = Sequence->Duration;
			bPlaying = false;
		}
	}
	SetTime(NewTime);
}

void FSequencer::ReleasePreview()
{
	bPlaying = false;
	if (IsAlive(Player, PlayerUUID))
		Player->InvalidateBindings();
	bPreviewActive = false;
}

void FSequencer::RefreshPreview(bool bRebind)
{
	if (!bPreviewActive || !IsAlive(Player, PlayerUUID))
		return;

	if (bRebind)
		Player->InvalidateBindings();
	Player->SetPlaybackPosition(Time);
}

AActor* FSequencer::GetBoundActor(int32 BindingIndex) const
{
	if (!bPreviewActive || !Player)
		return nullptr;

	const TArray<AActor*> Actors = Player->GetBoundActors(BindingIndex);
	return Actors.IsEmpty() ? nullptr : Actors[0];
}

AActor* FSequencer::GetActiveCineCamera() const
{
	if (!Sequence)
		return nullptr;

	const int32 Index = Sequence->FindCineCameraBinding();
	if (Index == INDEX_NONE)
		return nullptr;
	if (Sequence->CineCameraEndTime >= 0.0f && Time >= Sequence->CineCameraEndTime)
		return nullptr;
	return GetBoundActor(Index);
}

FVector FSequencer::EvaluatePreviewCameraShake() const
{
	FVector Offset(0.0f, 0.0f, 0.0f);
	if (!Sequence)
		return Offset;

	const int32 CameraIndex = Sequence->FindCineCameraBinding();
	if (CameraIndex == INDEX_NONE)
		return Offset;

	// 게임에서 ACameraActor::StartCameraShake(Strength, Duration, Frequency)가 거는 웨이브 쉐이크와 같은 식
	for (const FSequenceTrack& Track : Sequence->Bindings[CameraIndex].Tracks)
	{
		if (Track.Type != ESequenceTrackType::Event)
			continue;

		for (const FSequenceSection& Section : Track.Sections)
		{
			for (const FSequenceEventKey& Event : Section.Events)
			{
				if (Event.Function != "StartCameraShake" || Event.Time > Time)
					continue;
				const json& Args = Event.Args;
				if (!Args.is_array() || Args.size() != 3 || !Args[0].is_number() || !Args[1].is_number() || !Args[2].is_number())
					continue;

				Offset += UWaveOscillatorCameraShakePattern::EvaluateOffset(
					Args[0].get<float>(), Args[1].get<float>(), Args[2].get<float>(), Time - Event.Time);
			}
		}
	}
	return Offset;
}

// ---------------------------------------------------------------- 되돌리기

std::string FSequencer::Snapshot() const
{
	json Root;
	Sequence->ToJson(Root);
	return Root.dump();
}

void FSequencer::BeginTransaction()
{
	if (!Sequence)
		return;

	UndoStack.Add(Snapshot());
	if (UndoStack.Num() > MaxUndo)
		UndoStack.RemoveAt(0);
	RedoStack.Reset();
	bDirty = true;
}

void FSequencer::RestoreFromJson(const std::string& Text)
{
	const json Root = json::parse(Text, nullptr, false);
	if (Root.is_discarded() || !Sequence->FromJson(Root))
		return;

	ClearSelection();
	SelectedBinding = -2;
	SelectedTrack = -1;
	bDirty = true;
	RefreshPreview(true);
}

void FSequencer::Undo()
{
	if (!Sequence || UndoStack.IsEmpty())
		return;

	RedoStack.Add(Snapshot());
	const std::string Text = UndoStack.Last();
	UndoStack.RemoveLast();
	RestoreFromJson(Text);
}

void FSequencer::Redo()
{
	if (!Sequence || RedoStack.IsEmpty())
		return;

	UndoStack.Add(Snapshot());
	const std::string Text = RedoStack.Last();
	RedoStack.RemoveLast();
	RestoreFromJson(Text);
}

// ---------------------------------------------------------------- 데이터 접근

TArray<FSequenceTrack>* FSequencer::GetTracks(int32 BindingIndex)
{
	if (!Sequence)
		return nullptr;
	if (BindingIndex == -1)
		return &Sequence->MasterTracks;
	if (BindingIndex < 0 || BindingIndex >= Sequence->Bindings.Num())
		return nullptr;
	return &Sequence->Bindings[BindingIndex].Tracks;
}

FSequenceTrack* FSequencer::GetTrack(int32 BindingIndex, int32 TrackIndex)
{
	TArray<FSequenceTrack>* Tracks = GetTracks(BindingIndex);
	if (!Tracks || TrackIndex < 0 || TrackIndex >= Tracks->Num())
		return nullptr;
	return &(*Tracks)[TrackIndex];
}

FSequenceKey* FSequencer::GetKey(const FSequencerKeyHandle& Handle)
{
	FSequenceTrack* Track = GetTrack(Handle.Binding, Handle.Track);
	if (!Track || Handle.IsEvent() || Handle.Section < 0 || Handle.Section >= Track->Sections.Num())
		return nullptr;

	FSequenceSection& Section = Track->Sections[Handle.Section];
	if (Handle.Channel >= Section.Channels.Num())
		return nullptr;

	FSequenceChannel& Channel = Section.Channels[Handle.Channel];
	if (Handle.Key < 0 || Handle.Key >= Channel.Keys.Num())
		return nullptr;
	return &Channel.Keys[Handle.Key];
}

FSequenceEventKey* FSequencer::GetEvent(const FSequencerKeyHandle& Handle)
{
	FSequenceTrack* Track = GetTrack(Handle.Binding, Handle.Track);
	if (!Track || !Handle.IsEvent() || Handle.Section < 0 || Handle.Section >= Track->Sections.Num())
		return nullptr;

	FSequenceSection& Section = Track->Sections[Handle.Section];
	if (Handle.Key < 0 || Handle.Key >= Section.Events.Num())
		return nullptr;
	return &Section.Events[Handle.Key];
}

float* FSequencer::GetKeyTime(const FSequencerKeyHandle& Handle)
{
	if (Handle.IsEvent())
	{
		FSequenceEventKey* Event = GetEvent(Handle);
		return Event ? &Event->Time : nullptr;
	}
	FSequenceKey* Key = GetKey(Handle);
	return Key ? &Key->Time : nullptr;
}

// ---------------------------------------------------------------- 선택

bool FSequencer::IsSelected(const FSequencerKeyHandle& Handle) const
{
	return Selection.Find(Handle) != INDEX_NONE;
}

void FSequencer::SelectKey(const FSequencerKeyHandle& Handle, bool bAdditive)
{
	if (!bAdditive)
		Selection.Reset();
	if (!IsSelected(Handle))
		Selection.Add(Handle);
}

void FSequencer::ToggleKey(const FSequencerKeyHandle& Handle)
{
	const int32 Index = Selection.Find(Handle);
	if (Index == INDEX_NONE)
		Selection.Add(Handle);
	else
		Selection.RemoveAt(Index);
}

void FSequencer::DeleteSelectedKeys()
{
	if (Selection.IsEmpty())
		return;

	BeginTransaction();

	// 같은 배열에서 뒤쪽 인덱스부터 지워야 앞쪽 핸들이 그대로 맞는다
	TArray<FSequencerKeyHandle> Sorted = Selection;
	std::sort(Sorted.begin(), Sorted.end(), [](const FSequencerKeyHandle& A, const FSequencerKeyHandle& B) { return A.Key > B.Key; });
	for (const FSequencerKeyHandle& Handle : Sorted)
	{
		FSequenceTrack* Track = GetTrack(Handle.Binding, Handle.Track);
		if (!Track || Handle.Section < 0 || Handle.Section >= Track->Sections.Num())
			continue;

		FSequenceSection& Section = Track->Sections[Handle.Section];
		if (Handle.IsEvent())
		{
			if (Handle.Key >= 0 && Handle.Key < Section.Events.Num())
				Section.Events.RemoveAt(Handle.Key);
		}
		else if (Handle.Channel < Section.Channels.Num())
		{
			TArray<FSequenceKey>& Keys = Section.Channels[Handle.Channel].Keys;
			if (Handle.Key >= 0 && Handle.Key < Keys.Num())
				Keys.RemoveAt(Handle.Key);
		}
	}

	ClearSelection();
	RefreshPreview(false);
}

void FSequencer::SortKeys()
{
	if (!Sequence)
		return;

	for (int32 b = -1; b < Sequence->Bindings.Num(); ++b)
	{
		TArray<FSequenceTrack>& Tracks = *GetTracks(b);
		for (int32 t = 0; t < Tracks.Num(); ++t)
		{
			for (int32 s = 0; s < Tracks[t].Sections.Num(); ++s)
			{
				FSequenceSection& Section = Tracks[t].Sections[s];
				for (int32 c = 0; c < Section.Channels.Num(); ++c)
				{
					SortWithRemap(Section.Channels[c].Keys,
						[](const FSequenceKey& Key) { return Key.Time; },
						[&](const FSequencerKeyHandle& H) { return H.Binding == b && H.Track == t && H.Section == s && H.Channel == c; },
						Selection);
				}
				SortWithRemap(Section.Events,
					[](const FSequenceEventKey& Event) { return Event.Time; },
					[&](const FSequencerKeyHandle& H) { return H.Binding == b && H.Track == t && H.Section == s && H.IsEvent(); },
					Selection);
			}
		}
	}
}

// ---------------------------------------------------------------- 만들기

int32 FSequencer::AddBindingForActor(AActor* Actor, bool bSpawnable)
{
	if (!Sequence || !Actor)
		return INDEX_NONE;

	BeginTransaction();

	FSequenceBinding& Binding = Sequence->AddBinding(Actor->GetName());
	if (bSpawnable)
	{
		Binding.Template = ULevelSequence::CaptureTemplate(Actor);
		Binding.Name = Actor->GetName() + " (Spawnable)";
	}
	else
	{
		// 액터 이름은 실행마다 바뀌어 저장할 수 없으므로 태그를 붙인다. 이미 쓰는 태그면 번호를 붙인다
		if (Actor->BindingTag.empty())
		{
			FString Tag = Actor->GetName();
			TArray<AActor*> Existing;
			for (int32 Suffix = 2; ; ++Suffix)
			{
				Existing.Reset();
				ULevelSequencePlayer::FindActorsByTag(World, Tag, Existing);
				if (Existing.IsEmpty())
					break;
				Tag = Actor->GetName() + "_" + std::to_string(Suffix);
			}
			Actor->BindingTag = Tag;
			HTR_LOG(Info, "Sequencer: BindingTag '{}' set on '{}'. Save the scene to keep it.", Tag, Actor->GetName());
		}
		Binding.Tag = Actor->BindingTag;
	}

	FSequenceTrack& Transform = Binding.FindOrAddTrack(ESequenceTrackType::Transform);
	Transform.AddSection(0.0f, Sequence->Duration);

	const int32 Index = Sequence->Bindings.Num() - 1;
	SelectedBinding = Index;
	SelectedTrack = -1;
	RefreshPreview(true);
	return Index;
}

int32 FSequencer::AddCineCameraBinding(const FVector& Location, const FRotator& Rotation, float FieldOfView)
{
	if (!Sequence)
		return INDEX_NONE;

	BeginTransaction();

	FSequenceBinding& Binding = Sequence->AddBinding("CineCamera");
	Binding.Template = {
		{"Class", "ACameraActor"},
		{"Properties", json::object()},
		{"Components", json::array({ {
			{"Name", "CameraComponent"},
			{"Class", "UCameraComponent"},
			{"Parent", ""},
			{"Properties", {
				{"FieldOfView", FieldOfView},
				{"Transform", {
					{"Location", { {"X", Location.X}, {"Y", Location.Y}, {"Z", Location.Z} }},
					{"Rotation", { {"Pitch", Rotation.Pitch}, {"Yaw", Rotation.Yaw}, {"Roll", Rotation.Roll} }},
					{"Scale", { {"X", 1.0f}, {"Y", 1.0f}, {"Z", 1.0f} }},
				}},
			}},
		} })},
	};

	for (FSequenceBinding& Other : Sequence->Bindings)
		Other.bIsCineCamera = false;
	Binding.bIsCineCamera = true;

	Binding.FindOrAddTrack(ESequenceTrackType::Transform).AddSection(0.0f, Sequence->Duration);
	Binding.FindOrAddTrack(ESequenceTrackType::Float, "CameraComponent.FieldOfView").AddSection(0.0f, Sequence->Duration);

	// 템플릿 카메라는 게임에서 WASD 자유 비행을 하므로 시작할 때 끈다 (리플렉션 이벤트)
	FSequenceSection& Events = Binding.FindOrAddTrack(ESequenceTrackType::Event).AddSection(0.0f, Sequence->Duration);
	FSequenceEventKey& Event = Events.Events.AddDefaulted_GetRef();
	Event.Time = 0.0f;
	Event.Function = "CameraComponent.SetExternalInputManaged";
	Event.Args = json::array({ true });

	const int32 Index = Sequence->Bindings.Num() - 1;
	SelectedBinding = Index;
	SelectedTrack = -1;
	RefreshPreview(true);
	return Index;
}

void FSequencer::RemoveBinding(int32 BindingIndex)
{
	if (!Sequence || BindingIndex < 0 || BindingIndex >= Sequence->Bindings.Num())
		return;

	BeginTransaction();
	Sequence->Bindings.RemoveAt(BindingIndex);
	ClearSelection();
	SelectedBinding = -2;
	SelectedTrack = -1;
	RefreshPreview(true);
}

int32 FSequencer::AddTrack(int32 BindingIndex, ESequenceTrackType Type, const FString& PropertyPath)
{
	TArray<FSequenceTrack>* Tracks = GetTracks(BindingIndex);
	if (!Tracks)
		return INDEX_NONE;

	for (int32 i = 0; i < Tracks->Num(); ++i)
	{
		if ((*Tracks)[i].Type == Type && (*Tracks)[i].PropertyPath == PropertyPath)
			return i;
	}

	BeginTransaction();
	FSequenceTrack& Track = Tracks->AddDefaulted_GetRef();
	Track.Type = Type;
	Track.PropertyPath = PropertyPath;
	Track.AddSection(0.0f, Sequence->Duration);

	SelectedBinding = BindingIndex;
	SelectedTrack = Tracks->Num() - 1;
	RefreshPreview(true);
	return SelectedTrack;
}

void FSequencer::RemoveTrack(int32 BindingIndex, int32 TrackIndex)
{
	TArray<FSequenceTrack>* Tracks = GetTracks(BindingIndex);
	if (!Tracks || TrackIndex < 0 || TrackIndex >= Tracks->Num())
		return;

	BeginTransaction();
	Tracks->RemoveAt(TrackIndex);
	ClearSelection();
	SelectedTrack = -1;
	RefreshPreview(true);
}

void FSequencer::SetCineCamera(int32 BindingIndex, bool bEnable)
{
	if (!Sequence || BindingIndex < 0 || BindingIndex >= Sequence->Bindings.Num())
		return;

	BeginTransaction();
	if (bEnable)
	{
		for (FSequenceBinding& Other : Sequence->Bindings)
			Other.bIsCineCamera = false;
	}
	Sequence->Bindings[BindingIndex].bIsCineCamera = bEnable;
}

FSequenceSection& FSequencer::EnsureSection(FSequenceTrack& Track, float AtTime)
{
	if (Track.Sections.IsEmpty())
		return Track.AddSection(0.0f, std::max(Sequence->Duration, AtTime));

	for (FSequenceSection& Section : Track.Sections)
	{
		if (Section.Contains(AtTime))
			return Section;
	}

	// 가장 가까운 섹션을 늘린다
	FSequenceSection* Nearest = &Track.Sections[0];
	float Best = FLT_MAX;
	for (FSequenceSection& Section : Track.Sections)
	{
		const float Distance = AtTime < Section.StartTime ? Section.StartTime - AtTime : AtTime - Section.EndTime;
		if (Distance < Best)
		{
			Best = Distance;
			Nearest = &Section;
		}
	}
	if (AtTime < Nearest->StartTime)
		Nearest->StartTime = AtTime;
	else
		Nearest->EndTime = AtTime;
	return *Nearest;
}

USceneComponent* FSequencer::FindTransformTarget(AActor* Actor, const FSequenceTrack& Track)
{
	if (!Actor)
		return nullptr;
	if (Track.PropertyPath.empty())
		return Actor->GetRootComponent();
	return Cast<USceneComponent>(Actor->FindComponentByName(FName(Track.PropertyPath)));
}

bool FSequencer::ReadCurrentValue(int32 BindingIndex, int32 TrackIndex, int32 Channel, float& OutValue)
{
	const FSequenceTrack* Track = GetTrack(BindingIndex, TrackIndex);
	AActor* Actor = BindingIndex >= 0 ? GetBoundActor(BindingIndex) : nullptr;
	if (!Track || !Actor)
		return false;

	switch (Track->Type)
	{
	case ESequenceTrackType::Transform:
	{
		USceneComponent* Component = FindTransformTarget(Actor, *Track);
		if (!Component || Channel < 0 || Channel >= 9)
			return false;

		const FTransform& T = Component->GetTransform();
		const float Values[9] =
		{
			T.Location.X, T.Location.Y, T.Location.Z,
			T.Rotation.Pitch, T.Rotation.Yaw, T.Rotation.Roll,
			T.Scale.X, T.Scale.Y, T.Scale.Z,
		};
		OutValue = Values[Channel];
		return true;
	}

	case ESequenceTrackType::Float:
	case ESequenceTrackType::Bool:
	{
		const bool bIsBool = Track->Type == ESequenceTrackType::Bool;
		UObject* Target = nullptr;
		const FProperty* Property = nullptr;
		if (!ULevelSequencePlayer::FindTargetProperty(Actor, Track->PropertyPath, bIsBool ? EPropertyType::Bool : EPropertyType::Float, Target, Property))
			return false;

		const void* Ptr = reinterpret_cast<const uint8*>(Target) + Property->Offset;
		OutValue = bIsBool ? (*static_cast<const bool*>(Ptr) ? 1.0f : 0.0f) : *static_cast<const float*>(Ptr);
		return true;
	}

	case ESequenceTrackType::Spawn:
		OutValue = 1.0f;
		return true;

	case ESequenceTrackType::Event:
		return false;
	}
	return false;
}

void FSequencer::AddKey(int32 BindingIndex, int32 TrackIndex, int32 Channel, float AtTime)
{
	FSequenceTrack* Track = GetTrack(BindingIndex, TrackIndex);
	if (!Track || Track->Type == ESequenceTrackType::Event)
		return;

	// 지금 값을 읽으려면 바인딩이 해석돼 있어야 한다
	if (!bPreviewActive)
		SetTime(Time);

	const ESequenceTrackType Type = Track->Type;
	float Value = 0.0f;
	if (!ReadCurrentValue(BindingIndex, TrackIndex, Channel, Value))
		Value = (Type == ESequenceTrackType::Transform && Channel >= 6) || IsStepTrack(Type) ? 1.0f : 0.0f;

	BeginTransaction();
	Track = GetTrack(BindingIndex, TrackIndex);
	FSequenceSection& Section = EnsureSection(*Track, AtTime);
	if (Channel < 0 || Channel >= Section.Channels.Num())
		return;

	FSequenceChannel& Curve = Section.Channels[Channel];
	if (Curve.HasKeys())
	{
		const float Evaluated = Curve.Evaluate(AtTime, IsStepTrack(Type));
		// Spawn은 찍을 때마다 켜고 끄기를 바꾼다. 나머지는 커브 위에 그대로 찍는다
		Value = Type == ESequenceTrackType::Spawn ? (Evaluated >= 0.5f ? 0.0f : 1.0f) : Evaluated;
	}

	const int32 KeyIndex = Curve.AddKey(AtTime, Value, IsStepTrack(Type) ? ESequenceInterp::Constant : ESequenceInterp::Cubic);
	const int32 SectionIndex = static_cast<int32>(&Section - Track->Sections.GetData());
	SelectKey({ BindingIndex, TrackIndex, SectionIndex, Channel, KeyIndex }, false);
	RefreshPreview(false);
}

void FSequencer::KeyTrack(int32 BindingIndex, int32 TrackIndex, float AtTime)
{
	FSequenceTrack* Track = GetTrack(BindingIndex, TrackIndex);
	if (!Track || Track->Type == ESequenceTrackType::Event)
		return;

	if (!bPreviewActive)
		SetTime(Time);

	// 값을 먼저 모두 읽는다. 키를 찍고 다시 평가하면 아직 안 읽은 채널 값이 바뀔 수 있다
	const int32 ChannelCount = FSequenceTrack::GetChannelCount(Track->Type);
	TArray<float> Values;
	TArray<uint8> bRead;
	for (int32 c = 0; c < ChannelCount; ++c)
	{
		float Value = 0.0f;
		bRead.Add(ReadCurrentValue(BindingIndex, TrackIndex, c, Value) ? 1 : 0);
		Values.Add(Value);
	}

	BeginTransaction();
	Track = GetTrack(BindingIndex, TrackIndex);
	FSequenceSection& Section = EnsureSection(*Track, AtTime);
	const int32 SectionIndex = static_cast<int32>(&Section - Track->Sections.GetData());
	ClearSelection();
	for (int32 c = 0; c < ChannelCount && c < Section.Channels.Num(); ++c)
	{
		if (!bRead[c])
			continue;
		const ESequenceInterp Interp = IsStepTrack(Track->Type) ? ESequenceInterp::Constant : ESequenceInterp::Cubic;
		const int32 KeyIndex = Section.Channels[c].AddKey(AtTime, Values[c], Interp);
		Selection.Add({ BindingIndex, TrackIndex, SectionIndex, c, KeyIndex });
	}
	RefreshPreview(false);
}

void FSequencer::AddEvent(int32 BindingIndex, int32 TrackIndex, float AtTime)
{
	FSequenceTrack* Track = GetTrack(BindingIndex, TrackIndex);
	if (!Track || Track->Type != ESequenceTrackType::Event)
		return;

	BeginTransaction();
	FSequenceSection& Section = EnsureSection(*Track, AtTime);
	const int32 SectionIndex = static_cast<int32>(&Section - Track->Sections.GetData());
	FSequenceEventKey& Event = Section.Events.AddDefaulted_GetRef();
	Event.Time = AtTime;

	ClearSelection();
	Selection.Add({ BindingIndex, TrackIndex, SectionIndex, -1, Section.Events.Num() - 1 });
	SortKeys();
}
