#include "EnginePCH.h"
#include "Editor/Sequencer/SequencerPanel.h"

#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/World.h"
#include "Engine/Viewport.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "CameraTypes.h"
#include "UObject/Class.h"
#include "UObject/Property.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	constexpr float LabelColumnWidth = 260.0f;
	constexpr float RulerHeight = 26.0f;
	constexpr float InspectorHeight = 128.0f;
	constexpr float KeyRadius = 5.0f;
	constexpr float SectionEdge = 5.0f;
	constexpr float SameTimeTolerance = 1.0e-4f;

	// 트랙 종류마다 같은 색을 쓴다 (정리 문서와 같은 색)
	ImU32 TrackColor(ESequenceTrackType Type)
	{
		switch (Type)
		{
		case ESequenceTrackType::Transform: return IM_COL32(96, 165, 250, 255);
		case ESequenceTrackType::Float:     return IM_COL32(74, 222, 128, 255);
		case ESequenceTrackType::Bool:      return IM_COL32(167, 139, 250, 255);
		case ESequenceTrackType::Event:     return IM_COL32(251, 191, 36, 255);
		case ESequenceTrackType::Spawn:     return IM_COL32(45, 212, 191, 255);
		}
		return IM_COL32(200, 200, 200, 255);
	}
	constexpr ImU32 PlayheadColor = IM_COL32(251, 146, 60, 255);
	constexpr ImU32 SelectedKeyColor = IM_COL32(251, 146, 60, 255);
	constexpr ImU32 CineColor = IM_COL32(251, 113, 133, 255);

	ImU32 WithAlpha(ImU32 Color, int Alpha)
	{
		return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
	}

	void DrawDiamond(ImDrawList* DrawList, ImVec2 Center, float Radius, ImU32 Fill, ImU32 Outline)
	{
		const ImVec2 Top(Center.x, Center.y - Radius), Right(Center.x + Radius, Center.y);
		const ImVec2 Bottom(Center.x, Center.y + Radius), Left(Center.x - Radius, Center.y);
		DrawList->AddQuadFilled(Top, Right, Bottom, Left, Fill);
		DrawList->AddQuad(Top, Right, Bottom, Left, Outline, 1.0f);
	}

	const char* TrackLabel(const FSequenceTrack& Track, char* Buffer, size_t Size)
	{
		const char* TypeName = FSequenceTrack::GetTypeName(Track.Type);
		if (Track.PropertyPath.empty())
			std::snprintf(Buffer, Size, "%s", TypeName);
		else
			std::snprintf(Buffer, Size, "%s  %s", TypeName, Track.PropertyPath.c_str());
		return Buffer;
	}

	void CopyToBuffer(char* Buffer, size_t Size, const FString& Text)
	{
		std::snprintf(Buffer, Size, "%s", Text.c_str());
	}

	// 리플렉션 프로퍼티 중 Type인 것을 모은다. 컴포넌트면 "Component.Prop"으로 적는다
	void CollectProperties(AActor* Actor, EPropertyType Type, TArray<FString>& Out)
	{
		const auto CollectFrom = [&](UObject* Object, const FString& Prefix)
		{
			for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
			{
				for (const FProperty& Property : Class->GetProperties())
				{
					if (Property.Type == Type)
						Out.Add(Prefix + Property.Name);
				}
			}
		};

		CollectFrom(Actor, "");
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
				CollectFrom(Component, Component->GetName() + ".");
		}
	}
}

bool FSequencerPanel::Init()
{
	return true;
}

void FSequencerPanel::Tick(float DeltaTime)
{
	// 재생 중 Spawn 트랙이 액터를 지울 수 있다
	Sequencer.Tick(DeltaTime);
	ClearSelectionIfDestroyed();
	PilotViewport();
}

// ---------------------------------------------------------------- 그리기

void FSequencerPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(1100, 420), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Sequencer"))
	{
		bFocused = false;
		ImGui::End();
		return;
	}
	bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	RowHeight = ImGui::GetFrameHeight() + 2.0f;

	DrawToolbar();

	if (!Sequencer.GetSequence())
	{
		ImGui::Spacing();
		ImGui::TextDisabled("Create a new sequence or open a .sequence file to start.");
		ImGui::End();
		return;
	}

	DrawTimeline();
	DrawInspector();
	HandleShortcuts();

	ImGui::End();
	FlushDeferredActions();
	ClearSelectionIfDestroyed();
}

void FSequencerPanel::ClearSelectionIfDestroyed()
{
	AActor* Selected = GetSelectedActor ? GetSelectedActor() : nullptr;
	if (Selected && Selected->IsPendingKill() && ResetSelection)
		ResetSelection();
}

void FSequencerPanel::FlushDeferredActions()
{
	// 실행 중에 또 미룰 수 있으므로 꺼낸 뒤 돈다
	TArray<std::function<void()>> Actions = std::move(DeferredActions);
	DeferredActions.Reset();
	for (const std::function<void()>& Action : Actions)
		Action();
}

void FSequencerPanel::ShiftExpandedAfterRemove(int32 Binding, int32 Track)
{
	std::unordered_set<uint64> Shifted;
	for (uint64 Key : ExpandedRows)
	{
		int32 B = static_cast<int32>(Key >> 32) - 2;
		int32 T = static_cast<int32>(Key & 0xFFFFFFFFull) - 1;

		if (Track < 0)
		{
			// 바인딩을 지웠다: 그 바인딩 줄은 버리고 뒤쪽 바인딩을 당긴다
			if (B == Binding)
				continue;
			if (B > Binding)
				--B;
		}
		else if (B == Binding)
		{
			// 트랙을 지웠다: 같은 바인딩 안에서 뒤쪽 트랙을 당긴다
			if (T == Track)
				continue;
			if (T > Track)
				--T;
		}
		Shifted.insert((static_cast<uint64>(B + 2) << 32) | static_cast<uint32>(T + 1));
	}
	ExpandedRows = std::move(Shifted);
}

void FSequencerPanel::DrawToolbar()
{
	ULevelSequence* Sequence = Sequencer.GetSequence();

	if (ImGui::Button("New"))
	{
		Sequencer.NewSequence();
		bFitPending = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Open"))
	{
		const FString Path = FEditorFileUtils::OpenSequenceFileDialog(false);
		if (!Path.empty() && Sequencer.OpenSequence(Path))
			bFitPending = true;
	}
	ImGui::BeginDisabled(!Sequence);
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		if (Sequencer.GetPath().empty())
		{
			const FString Path = FEditorFileUtils::OpenSequenceFileDialog(true);
			if (!Path.empty())
				Sequencer.SaveSequenceAs(Path);
		}
		else
		{
			Sequencer.SaveSequence();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Save As"))
	{
		const FString Path = FEditorFileUtils::OpenSequenceFileDialog(true);
		if (!Path.empty())
			Sequencer.SaveSequenceAs(Path);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!Sequencer.CanUndo());
	if (ImGui::Button("Undo"))
		Sequencer.Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!Sequencer.CanRedo());
	if (ImGui::Button("Redo"))
		Sequencer.Redo();
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (Sequence)
	{
		const FString& Path = Sequencer.GetPath();
		ImGui::TextDisabled("%s%s", Path.empty() ? "(unsaved)" : Path.c_str(), Sequencer.IsDirty() ? " *" : "");
	}

	if (!Sequence)
		return;

	// 재생 줄
	if (ImGui::Button("|<"))
		Sequencer.SetTime(0.0f);
	ImGui::SameLine();
	if (ImGui::Button(Sequencer.IsPlaying() ? "Pause" : "Play"))
		Sequencer.TogglePlay();
	ImGui::SameLine();
	if (ImGui::Button("Restore"))
		Sequencer.ReleasePreview();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Stops the preview and puts every animated value back.\nScrub the timeline to preview again.");

	ImGui::SameLine();
	ImGui::Checkbox("Loop", &Sequencer.bLoop);
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &Sequencer.bSnap);
	ImGui::SameLine();
	ImGui::Checkbox("Cine Cam View", &Sequencer.bPilotCineCamera);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("While playing or scrubbing, moves the active editor viewport to the cine camera.\nWhen paused you can fly the viewport freely; scrub again to snap back.");

	ImGui::SameLine();
	float Time = Sequencer.GetTime();
	ImGui::SetNextItemWidth(90.0f);
	if (ImGui::DragFloat("##Time", &Time, 0.01f, 0.0f, Sequence->Duration, "%.2f s"))
		Sequencer.SetTime(Time);
	ImGui::SameLine();
	ImGui::TextDisabled("frame %d / %.0f fps", static_cast<int>(std::round(Sequencer.GetTime() * Sequence->DisplayRate)), Sequence->DisplayRate);

	ImGui::SameLine();
	if (ImGui::Button("Fit"))
		FitView();

	// 만들기
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	AActor* Selected = GetSelectedActor ? GetSelectedActor() : nullptr;
	ImGui::BeginDisabled(!Selected);
	if (ImGui::Button("+ Actor"))
		Sequencer.AddBindingForActor(Selected, false);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Bind the actor selected in the Outliner by its BindingTag (adds a tag if it has none).");
	ImGui::SameLine();
	if (ImGui::Button("+ Spawnable"))
		Sequencer.AddBindingForActor(Selected, true);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Copy the selected actor into the sequence as a template. The sequence spawns its own copy.");
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("+ Cine Camera"))
	{
		// 지금 편집 뷰포트 시점에서 시작한다
		FVector Location(0.0f, 0.0f, 0.0f);
		FRotator Rotation;
		float Fov = 60.0f;
		if (Viewports)
		{
			if (FEditorViewportClient* Client = Viewports->GetEditorClient(Viewports->GetActiveViewIndex()))
			{
				Location = Client->GetCamera().Location;
				Rotation = Client->GetCamera().Rotation.ToFRotator();
				Fov = Client->GetCamera().Projection.FovDegrees;
			}
		}
		Sequencer.AddCineCameraBinding(Location, Rotation, Fov);
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Master Events"))
		Sequencer.AddTrack(-1, ESequenceTrackType::Event, "");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Event track for the director: PlaySound, StopSound, SpawnEffect.");
}

void FSequencerPanel::BuildRows(TArray<FRow>& OutRows) const
{
	ULevelSequence* Sequence = Sequencer.GetSequence();

	if (!Sequence->MasterTracks.IsEmpty())
	{
		OutRows.Add({ ERowType::Master, -1, -1, -1, 0 });
		if (IsExpanded(-1, -1))
		{
			for (int32 t = 0; t < Sequence->MasterTracks.Num(); ++t)
				OutRows.Add({ ERowType::Track, -1, t, -1, 1 });
		}
	}

	for (int32 b = 0; b < Sequence->Bindings.Num(); ++b)
	{
		OutRows.Add({ ERowType::Binding, b, -1, -1, 0 });
		if (!IsExpanded(b, -1))
			continue;

		const TArray<FSequenceTrack>& Tracks = Sequence->Bindings[b].Tracks;
		for (int32 t = 0; t < Tracks.Num(); ++t)
		{
			OutRows.Add({ ERowType::Track, b, t, -1, 1 });
			if (Tracks[t].Type == ESequenceTrackType::Transform && IsExpanded(b, t))
			{
				for (int32 c = 0; c < 9; ++c)
					OutRows.Add({ ERowType::Channel, b, t, c, 2 });
			}
		}
	}
}

void FSequencerPanel::DrawTimeline()
{
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float TableHeight = std::max(80.0f, Avail.y - InspectorHeight);

	const ImGuiTableFlags Flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV
		| ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg;
	if (!ImGui::BeginTable("##SequencerTable", 2, Flags, ImVec2(0.0f, TableHeight)))
		return;

	ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth);
	ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupScrollFreeze(0, 1);

	bTimelineHovered = false;

	// 눈금자 줄 (고정)
	ImGui::TableNextRow(ImGuiTableRowFlags_None, RulerHeight);
	ImGui::TableSetColumnIndex(0);
	ImGui::TextDisabled("%d bindings", Sequencer.GetSequence()->Bindings.Num());
	ImGui::TableSetColumnIndex(1);
	TimelineX0 = ImGui::GetCursorScreenPos().x;
	TimelineWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
	if (bFitPending)
	{
		FitView();
		bFitPending = false;
	}
	DrawRuler();

	TArray<FRow> Rows;
	BuildRows(Rows);

	ImGuiListClipper Clipper;
	Clipper.Begin(Rows.Num(), RowHeight);
	while (Clipper.Step())
	{
		for (int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
		{
			const FRow& Row = Rows[i];
			ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight);
			ImGui::PushID(i);

			ImGui::TableSetColumnIndex(0);
			DrawRowLabel(Row);

			ImGui::TableSetColumnIndex(1);
			DrawRowTimeline(Row);

			ImGui::PopID();
		}
	}

	HandleZoomAndPan();
	ImGui::EndTable();

	// 드래그가 끝났으면 상태를 지운다 (아이템이 잘려 IsItemDeactivated를 못 받은 경우까지)
	if (Drag != EDrag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Drag = EDrag::None;
}

void FSequencerPanel::DrawRuler()
{
	ULevelSequence* Sequence = Sequencer.GetSequence();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const float Bottom = Origin.y + RulerHeight;

	ImGui::InvisibleButton("##Ruler", ImVec2(TimelineWidth, RulerHeight));
	if (ImGui::IsItemHovered())
		bTimelineHovered = true;
	if (ImGui::IsItemActive())
	{
		Drag = EDrag::Scrub;
		Sequencer.SetTime(Sequencer.SnapTime(XToTime(ImGui::GetIO().MousePos.x)));
	}

	DrawList->PushClipRect(ImVec2(TimelineX0, Origin.y), ImVec2(TimelineX0 + TimelineWidth, Bottom), true);

	// 시퀀스 밖은 어둡게
	const float EndX = TimeToX(Sequence->Duration);
	if (EndX < TimelineX0 + TimelineWidth)
		DrawList->AddRectFilled(ImVec2(EndX, Origin.y), ImVec2(TimelineX0 + TimelineWidth, Bottom), IM_COL32(0, 0, 0, 60));

	// 눈금 간격: 글자가 겹치지 않도록 70px 이상 벌어지는 가장 작은 간격
	static const float Steps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f };
	float Major = Steps[IM_ARRAYSIZE(Steps) - 1];
	for (float Step : Steps)
	{
		if (Step * PixelsPerSecond >= 70.0f)
		{
			Major = Step;
			break;
		}
	}
	const float Minor = Major / 5.0f;
	const ImU32 TickColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const float FirstTick = std::floor(XToTime(TimelineX0) / Minor) * Minor;
	for (float T = FirstTick; TimeToX(T) <= TimelineX0 + TimelineWidth; T += Minor)
	{
		const float X = TimeToX(T);
		const bool bMajor = std::fabs(std::remainder(T, Major)) < Minor * 0.5f;
		DrawList->AddLine(ImVec2(X, Bottom - (bMajor ? 10.0f : 5.0f)), ImVec2(X, Bottom), TickColor);
		if (bMajor)
		{
			char Label[32];
			std::snprintf(Label, sizeof(Label), Major < 1.0f ? "%.2f" : "%.0fs", T);
			DrawList->AddText(ImVec2(X + 3.0f, Origin.y + 2.0f), TickColor, Label);
		}
	}

	// 카메라 컷 끝
	if (Sequence->CineCameraEndTime >= 0.0f)
	{
		const float X = TimeToX(Sequence->CineCameraEndTime);
		DrawList->AddLine(ImVec2(X, Origin.y), ImVec2(X, Bottom), CineColor, 2.0f);
	}

	// 재생 헤드
	const float HeadX = TimeToX(Sequencer.GetTime());
	DrawList->AddTriangleFilled(ImVec2(HeadX - 6.0f, Origin.y), ImVec2(HeadX + 6.0f, Origin.y), ImVec2(HeadX, Origin.y + 9.0f), PlayheadColor);
	DrawList->AddLine(ImVec2(HeadX, Origin.y), ImVec2(HeadX, Bottom), PlayheadColor, 1.5f);

	DrawList->PopClipRect();
}

bool FSequencerPanel::IsExpanded(int32 Binding, int32 Track) const
{
	const uint64 Key = (static_cast<uint64>(Binding + 2) << 32) | static_cast<uint32>(Track + 1);
	return ExpandedRows.count(Key) > 0;
}

void FSequencerPanel::SetExpanded(int32 Binding, int32 Track, bool bExpanded)
{
	const uint64 Key = (static_cast<uint64>(Binding + 2) << 32) | static_cast<uint32>(Track + 1);
	if (bExpanded)
		ExpandedRows.insert(Key);
	else
		ExpandedRows.erase(Key);
}

bool FSequencerPanel::IsRowValid(const FRow& Row)
{
	ULevelSequence* Sequence = Sequencer.GetSequence();
	if (!Sequence)
		return false;

	switch (Row.Type)
	{
	case ERowType::Master:
		return true;
	case ERowType::Binding:
		return Row.Binding >= 0 && Row.Binding < Sequence->Bindings.Num();
	case ERowType::Track:
		return Sequencer.GetTrack(Row.Binding, Row.Track) != nullptr;
	case ERowType::Channel:
	{
		const FSequenceTrack* Track = Sequencer.GetTrack(Row.Binding, Row.Track);
		return Track && Row.Channel >= 0 && Row.Channel < FSequenceTrack::GetChannelCount(Track->Type);
	}
	}
	return false;
}

void FSequencerPanel::DrawRowLabel(const FRow& Row)
{
	if (!IsRowValid(Row))
		return;

	ULevelSequence* Sequence = Sequencer.GetSequence();
	ImGui::Indent(Row.Depth * 14.0f);

	char Label[300];
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
	bool bExpandable = false;
	bool bSelected = false;

	switch (Row.Type)
	{
	case ERowType::Master:
		std::snprintf(Label, sizeof(Label), "Master");
		bExpandable = true;
		bSelected = Sequencer.SelectedBinding == -1 && Sequencer.SelectedTrack < 0;
		break;

	case ERowType::Binding:
	{
		const FSequenceBinding& Binding = Sequence->Bindings[Row.Binding];
		const char* Kind = Binding.bIsCineCamera ? "  [cine cam]" : (!Binding.Tag.empty() ? "" : (Binding.HasTemplate() ? "  [spawnable]" : "  [unbound]"));
		std::snprintf(Label, sizeof(Label), "%s%s", Binding.Name.c_str(), Kind);
		bExpandable = true;
		bSelected = Sequencer.SelectedBinding == Row.Binding && Sequencer.SelectedTrack < 0;
		break;
	}

	case ERowType::Track:
	{
		const FSequenceTrack* Track = Sequencer.GetTrack(Row.Binding, Row.Track);
		TrackLabel(*Track, Label, sizeof(Label));
		bExpandable = Track->Type == ESequenceTrackType::Transform;
		bSelected = Sequencer.SelectedBinding == Row.Binding && Sequencer.SelectedTrack == Row.Track;

		// 지금 시간에 키 찍기 (Event 트랙은 이벤트 추가)
		if (ImGui::SmallButton(Track->Type == ESequenceTrackType::Event ? "+##AddEvent" : "+##Key"))
		{
			if (Track->Type == ESequenceTrackType::Event)
				Sequencer.AddEvent(Row.Binding, Row.Track, Sequencer.SnapTime(Sequencer.GetTime()));
			else
				Sequencer.KeyTrack(Row.Binding, Row.Track, Sequencer.SnapTime(Sequencer.GetTime()));
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(Track->Type == ESequenceTrackType::Event ? "Add an event at the playhead" : "Key the current value at the playhead");
		ImGui::SameLine();
		break;
	}

	case ERowType::Channel:
	{
		const FSequenceTrack* Track = Sequencer.GetTrack(Row.Binding, Row.Track);
		std::snprintf(Label, sizeof(Label), "%s", FSequenceTrack::GetChannelName(Track->Type, Row.Channel));
		Flags |= ImGuiTreeNodeFlags_Leaf;
		break;
	}
	}

	if (!bExpandable)
		Flags |= ImGuiTreeNodeFlags_Leaf;
	if (bSelected)
		Flags |= ImGuiTreeNodeFlags_Selected;

	const bool bWasExpanded = IsExpanded(Row.Binding, Row.Type == ERowType::Track ? Row.Track : -1);
	if (bExpandable)
		ImGui::SetNextItemOpen(bWasExpanded);
	const bool bOpen = ImGui::TreeNodeEx("##Node", Flags, "%s", Label);
	if (bExpandable && bOpen != bWasExpanded)
		SetExpanded(Row.Binding, Row.Type == ERowType::Track ? Row.Track : -1, bOpen);

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && Row.Type != ERowType::Channel)
	{
		Sequencer.SelectedBinding = Row.Binding;
		Sequencer.SelectedTrack = Row.Type == ERowType::Track ? Row.Track : -1;
	}

	DrawLabelContextMenu(Row);
	ImGui::Unindent(Row.Depth * 14.0f);
}

void FSequencerPanel::DrawLabelContextMenu(const FRow& Row)
{
	if (!ImGui::BeginPopupContextItem("##LabelMenu"))
		return;

	ULevelSequence* Sequence = Sequencer.GetSequence();
	switch (Row.Type)
	{
	case ERowType::Master:
		if (ImGui::MenuItem("Add Event Track"))
			Defer([this]() { Sequencer.AddTrack(-1, ESequenceTrackType::Event, ""); });
		break;

	case ERowType::Binding:
	{
		if (Row.Binding < 0 || Row.Binding >= Sequence->Bindings.Num())
			break;
		FSequenceBinding& Binding = Sequence->Bindings[Row.Binding];
		if (ImGui::BeginMenu("Add Track"))
		{
			DrawAddTrackMenu(Row.Binding);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Cine Camera", nullptr, Binding.bIsCineCamera))
			Sequencer.SetCineCamera(Row.Binding, !Binding.bIsCineCamera);

		AActor* Selected = GetSelectedActor ? GetSelectedActor() : nullptr;
		if (ImGui::MenuItem("Capture Template From Selected Actor", nullptr, false, Selected != nullptr))
		{
			Sequencer.BeginTransaction();
			Binding.Template = ULevelSequence::CaptureTemplate(Selected);
			Sequencer.RefreshPreview(true);
		}
		if (ImGui::MenuItem("Clear Template", nullptr, false, Binding.HasTemplate()))
		{
			Sequencer.BeginTransaction();
			Binding.Template = json(nullptr);
			Sequencer.RefreshPreview(true);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Binding"))
		{
			const int32 BindingIndex = Row.Binding;
			Defer([this, BindingIndex]()
			{
				Sequencer.RemoveBinding(BindingIndex);
				ShiftExpandedAfterRemove(BindingIndex, -1);
			});
		}
		break;
	}

	case ERowType::Track:
	{
		const FSequenceTrack* Track = Sequencer.GetTrack(Row.Binding, Row.Track);
		if (!Track)
			break;
		if (Track->Type == ESequenceTrackType::Event)
		{
			if (ImGui::MenuItem("Add Event At Playhead"))
				Sequencer.AddEvent(Row.Binding, Row.Track, Sequencer.SnapTime(Sequencer.GetTime()));
		}
		else if (ImGui::MenuItem("Key At Playhead"))
		{
			Sequencer.KeyTrack(Row.Binding, Row.Track, Sequencer.SnapTime(Sequencer.GetTime()));
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Track"))
		{
			const int32 BindingIndex = Row.Binding;
			const int32 TrackIndex = Row.Track;
			Defer([this, BindingIndex, TrackIndex]()
			{
				Sequencer.RemoveTrack(BindingIndex, TrackIndex);
				ShiftExpandedAfterRemove(BindingIndex, TrackIndex);
			});
		}
		break;
	}

	case ERowType::Channel:
		if (ImGui::MenuItem("Key At Playhead"))
			Sequencer.AddKey(Row.Binding, Row.Track, Row.Channel, Sequencer.SnapTime(Sequencer.GetTime()));
		break;
	}

	ImGui::EndPopup();
}

void FSequencerPanel::DrawAddTrackMenu(int32 BindingIndex)
{
	ULevelSequence* Sequence = Sequencer.GetSequence();
	const FSequenceBinding& Binding = Sequence->Bindings[BindingIndex];

	// 컴포넌트·프로퍼티 목록은 지금 바인딩된 액터에서 읽는다
	if (!Sequencer.IsPreviewActive())
		Sequencer.SetTime(Sequencer.GetTime());
	AActor* Actor = Sequencer.GetBoundActor(BindingIndex);

	// 트랙 추가는 이번 프레임의 줄 목록이 끝난 뒤에 한다
	const auto AddTrackLater = [this, BindingIndex](ESequenceTrackType Type, const FString& Path)
	{
		Defer([this, BindingIndex, Type, Path]() { Sequencer.AddTrack(BindingIndex, Type, Path); });
	};

	if (ImGui::MenuItem("Transform (Root)"))
		AddTrackLater(ESequenceTrackType::Transform, "");

	if (ImGui::BeginMenu("Transform (Component)", Actor != nullptr))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Cast<USceneComponent>(Component) && Component != Actor->GetRootComponent())
			{
				if (ImGui::MenuItem(Component->GetName().c_str()))
					AddTrackLater(ESequenceTrackType::Transform, Component->GetName());
			}
		}
		ImGui::EndMenu();
	}

	const auto PropertyMenu = [&](const char* MenuName, ESequenceTrackType TrackType, EPropertyType PropertyType)
	{
		if (!ImGui::BeginMenu(MenuName, Actor != nullptr))
			return;
		TArray<FString> Paths;
		CollectProperties(Actor, PropertyType, Paths);
		if (Paths.IsEmpty())
			ImGui::TextDisabled("No reflected properties of this type");
		for (const FString& Path : Paths)
		{
			if (ImGui::MenuItem(Path.c_str()))
				AddTrackLater(TrackType, Path);
		}
		ImGui::EndMenu();
	};
	PropertyMenu("Float", ESequenceTrackType::Float, EPropertyType::Float);
	PropertyMenu("Bool", ESequenceTrackType::Bool, EPropertyType::Bool);

	if (ImGui::MenuItem("Event"))
		AddTrackLater(ESequenceTrackType::Event, "");
	if (ImGui::MenuItem("Spawn", nullptr, false, Binding.HasTemplate()))
		AddTrackLater(ESequenceTrackType::Spawn, "");

	if (!Actor)
		ImGui::TextDisabled("Bind an actor to list its components and properties.");
}

void FSequencerPanel::DrawRowTimeline(const FRow& Row)
{
	if (!IsRowValid(Row))
		return;

	ULevelSequence* Sequence = Sequencer.GetSequence();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const float RowTop = Origin.y;
	const float CenterY = RowTop + RowHeight * 0.5f;
	const float Right = TimelineX0 + TimelineWidth;

	// 배경 버튼: 빈 곳 클릭은 선택 해제, 오른쪽 클릭은 메뉴. 위에 오는 섹션·키가 hover를 받도록 겹침을 허용한다
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##Background", ImVec2(TimelineWidth, RowHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (ImGui::IsItemHovered())
	{
		bTimelineHovered = true;
		const ImGuiIO& IO = ImGui::GetIO();
		// Ctrl·Shift + 휠은 확대·이동에 쓰므로 표의 세로 스크롤이 가져가지 않게 한다
		if (IO.KeyCtrl || IO.KeyShift)
			ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl)
		Sequencer.ClearSelection();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		ContextTime = Sequencer.SnapTime(std::max(0.0f, XToTime(ImGui::GetIO().MousePos.x)));
	DrawTimelineContextMenu(Row);

	DrawList->PushClipRect(ImVec2(TimelineX0, RowTop), ImVec2(Right, RowTop + RowHeight), true);

	const float EndX = TimeToX(Sequence->Duration);
	if (EndX < Right)
		DrawList->AddRectFilled(ImVec2(EndX, RowTop), ImVec2(Right, RowTop + RowHeight), IM_COL32(0, 0, 0, 50));

	const auto KeyVisible = [&](float Time) { const float X = TimeToX(Time); return X >= TimelineX0 - 8.0f && X <= Right + 8.0f; };

	switch (Row.Type)
	{
	case ERowType::Master:
	case ERowType::Binding:
	{
		// 접힌 줄: 아래 트랙의 키를 작게 모아 보여 준다 (편집은 펼쳐서)
		TArray<FSequenceTrack>* Tracks = Sequencer.GetTracks(Row.Binding);
		for (const FSequenceTrack& Track : *Tracks)
		{
			const ImU32 Color = WithAlpha(TrackColor(Track.Type), 170);
			for (const FSequenceSection& Section : Track.Sections)
			{
				if (Track.Type == ESequenceTrackType::Spawn && !Section.Channels.IsEmpty())
				{
					// Spawn 트랙은 존재하는 구간을 얇은 띠로 보여 준다
					const FSequenceChannel& Channel = Section.Channels[0];
					for (int32 k = 0; k < Channel.Keys.Num(); ++k)
					{
						if (Channel.Keys[k].Value < 0.5f)
							continue;
						const float End = k + 1 < Channel.Keys.Num() ? Channel.Keys[k + 1].Time : Section.EndTime;
						DrawList->AddRectFilled(ImVec2(TimeToX(Channel.Keys[k].Time), CenterY + 4.0f), ImVec2(TimeToX(End), CenterY + 7.0f), WithAlpha(Color, 120));
					}
				}
				for (const FSequenceChannel& Channel : Section.Channels)
				{
					for (const FSequenceKey& Key : Channel.Keys)
					{
						if (KeyVisible(Key.Time))
							DrawDiamond(DrawList, ImVec2(TimeToX(Key.Time), CenterY), 3.0f, Color, Color);
					}
				}
				for (const FSequenceEventKey& Event : Section.Events)
				{
					if (KeyVisible(Event.Time))
						DrawDiamond(DrawList, ImVec2(TimeToX(Event.Time), CenterY), 3.0f, Color, Color);
				}
			}
		}
		if (Row.Type == ERowType::Binding && Sequence->Bindings[Row.Binding].bIsCineCamera && Sequence->CineCameraEndTime >= 0.0f)
		{
			const float X = TimeToX(Sequence->CineCameraEndTime);
			DrawList->AddLine(ImVec2(X, RowTop), ImVec2(X, RowTop + RowHeight), CineColor, 2.0f);
		}
		break;
	}

	case ERowType::Track:
	case ERowType::Channel:
	{
		FSequenceTrack* Track = Sequencer.GetTrack(Row.Binding, Row.Track);
		const ImU32 Color = TrackColor(Track->Type);

		for (int32 s = 0; s < Track->Sections.Num(); ++s)
			SectionItems(Row.Binding, Row.Track, s, RowTop, Color, Row.Type == ERowType::Track);

		char Id[64];
		TArray<FSequencerKeyHandle> Handles;
		for (int32 s = 0; s < Track->Sections.Num(); ++s)
		{
			FSequenceSection& Section = Track->Sections[s];

			if (Track->Type == ESequenceTrackType::Event)
			{
				for (int32 k = 0; k < Section.Events.Num(); ++k)
				{
					if (!KeyVisible(Section.Events[k].Time))
						continue;
					Handles.Reset();
					Handles.Add({ Row.Binding, Row.Track, s, -1, k });
					std::snprintf(Id, sizeof(Id), "##E%d_%d", s, k);
					KeyItem(Id, Section.Events[k].Time, CenterY, Color, Handles, true);
				}
				continue;
			}

			// 채널 하나를 직접 보여 주는 줄: 채널 줄, 또는 채널이 하나뿐인 트랙 줄
			const bool bSingleChannel = Row.Type == ERowType::Channel || Section.Channels.Num() == 1;
			if (bSingleChannel)
			{
				const int32 c = Row.Type == ERowType::Channel ? Row.Channel : 0;
				if (c >= Section.Channels.Num())
					continue;
				const TArray<FSequenceKey>& Keys = Section.Channels[c].Keys;
				for (int32 k = 0; k < Keys.Num(); ++k)
				{
					if (!KeyVisible(Keys[k].Time))
						continue;
					Handles.Reset();
					Handles.Add({ Row.Binding, Row.Track, s, c, k });
					std::snprintf(Id, sizeof(Id), "##K%d_%d_%d", s, c, k);
					KeyItem(Id, Keys[k].Time, CenterY, Color, Handles, false);
				}
				continue;
			}

			// Transform 트랙 줄: 같은 시간의 채널 키를 하나로 묶어 보여 준다 (잡으면 묶음 전체가 움직인다)
			TArray<float> Times;
			for (const FSequenceChannel& Channel : Section.Channels)
			{
				for (const FSequenceKey& Key : Channel.Keys)
				{
					bool bFound = false;
					for (float T : Times)
						bFound = bFound || std::fabs(T - Key.Time) < SameTimeTolerance;
					if (!bFound && KeyVisible(Key.Time))
						Times.Add(Key.Time);
				}
			}
			for (int32 g = 0; g < Times.Num(); ++g)
			{
				Handles.Reset();
				for (int32 c = 0; c < Section.Channels.Num(); ++c)
				{
					const TArray<FSequenceKey>& Keys = Section.Channels[c].Keys;
					for (int32 k = 0; k < Keys.Num(); ++k)
					{
						if (std::fabs(Keys[k].Time - Times[g]) < SameTimeTolerance)
							Handles.Add({ Row.Binding, Row.Track, s, c, k });
					}
				}
				std::snprintf(Id, sizeof(Id), "##G%d_%d", s, g);
				KeyItem(Id, Times[g], CenterY, Color, Handles, false);
			}
		}
		break;
	}
	}

	// 재생 헤드
	const float HeadX = TimeToX(Sequencer.GetTime());
	DrawList->AddLine(ImVec2(HeadX, RowTop), ImVec2(HeadX, RowTop + RowHeight), PlayheadColor, 1.0f);

	DrawList->PopClipRect();
}

void FSequencerPanel::SectionItems(int32 BindingIndex, int32 TrackIndex, int32 SectionIndex, float RowTop, ImU32 Color, bool bInteractive)
{
	FSequenceTrack* Track = Sequencer.GetTrack(BindingIndex, TrackIndex);
	FSequenceSection& Section = Track->Sections[SectionIndex];
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const float X0 = std::max(TimeToX(Section.StartTime), TimelineX0 - 2.0f);
	const float X1 = std::min(TimeToX(Section.EndTime), TimelineX0 + TimelineWidth + 2.0f);
	const float Top = RowTop + 3.0f;
	const float Bottom = RowTop + RowHeight - 3.0f;
	DrawList->AddRectFilled(ImVec2(X0, Top), ImVec2(X1, Bottom), WithAlpha(Color, bInteractive ? 45 : 22), 3.0f);
	if (bInteractive)
		DrawList->AddRect(ImVec2(X0, Top), ImVec2(X1, Bottom), WithAlpha(Color, 110), 3.0f);

	if (!bInteractive || X1 - X0 < 2.0f)
		return;

	ImGuiIO& IO = ImGui::GetIO();
	ImGui::PushID(SectionIndex);

	const auto BeginSectionDrag = [&](EDrag Kind)
	{
		Drag = Kind;
		DragStartMouseX = IO.MousePos.x;
		DragBinding = BindingIndex;
		DragTrack = TrackIndex;
		DragSection = SectionIndex;
		DragSectionOriginal = Section;
		bDragMoved = false;
	};

	const auto UpdateSectionDrag = [&]()
	{
		const float Delta = (IO.MousePos.x - DragStartMouseX) / PixelsPerSecond;
		if (!bDragMoved && std::fabs(IO.MousePos.x - DragStartMouseX) < 2.0f)
			return;
		if (!bDragMoved)
		{
			// 실제로 움직였을 때만 되돌리기 지점을 남긴다 (아직 아무것도 바꾸지 않은 상태)
			Sequencer.BeginTransaction();
			bDragMoved = true;
		}

		FSequenceSection& Target = Sequencer.GetTrack(DragBinding, DragTrack)->Sections[DragSection];
		const FSequenceSection& Original = DragSectionOriginal;
		if (Drag == EDrag::SectionStart)
		{
			Target.StartTime = std::min(Sequencer.SnapTime(std::max(0.0f, Original.StartTime + Delta)), Original.EndTime);
		}
		else if (Drag == EDrag::SectionEnd)
		{
			Target.EndTime = std::max(Sequencer.SnapTime(Original.EndTime + Delta), Original.StartTime);
		}
		else
		{
			// 몸통을 옮기면 안의 키와 이벤트도 같이 옮긴다 (스냅은 섹션 시작에 맞춘다)
			const float Snapped = Sequencer.SnapTime(std::max(0.0f, Original.StartTime + Delta)) - Original.StartTime;
			Target = Original;
			Target.StartTime += Snapped;
			Target.EndTime += Snapped;
			for (FSequenceChannel& Channel : Target.Channels)
				for (FSequenceKey& Key : Channel.Keys)
					Key.Time += Snapped;
			for (FSequenceEventKey& Event : Target.Events)
				Event.Time += Snapped;
		}
		Sequencer.RefreshPreview(false);
	};

	// 몸통
	ImGui::SetCursorScreenPos(ImVec2(X0 + SectionEdge, Top));
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##Body", ImVec2(std::max(1.0f, X1 - X0 - SectionEdge * 2.0f), Bottom - Top));
	if (ImGui::IsItemActivated())
		BeginSectionDrag(EDrag::SectionMove);
	if (ImGui::IsItemActive() && Drag == EDrag::SectionMove)
		UpdateSectionDrag();

	// 양 끝
	const char* EdgeIds[2] = { "##Start", "##End" };
	const float EdgeX[2] = { X0, X1 - SectionEdge };
	const EDrag EdgeDrag[2] = { EDrag::SectionStart, EDrag::SectionEnd };
	for (int32 e = 0; e < 2; ++e)
	{
		ImGui::SetCursorScreenPos(ImVec2(EdgeX[e], Top));
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton(EdgeIds[e], ImVec2(SectionEdge, Bottom - Top));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActivated())
			BeginSectionDrag(EdgeDrag[e]);
		if (ImGui::IsItemActive() && Drag == EdgeDrag[e])
			UpdateSectionDrag();
	}

	ImGui::PopID();
}

void FSequencerPanel::KeyItem(const char* Id, float KeyTime, float CenterY, ImU32 Color, const TArray<FSequencerKeyHandle>& Handles, bool bEvent)
{
	ImGuiIO& IO = ImGui::GetIO();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const float X = TimeToX(KeyTime);

	bool bAllSelected = !Handles.IsEmpty();
	for (const FSequencerKeyHandle& Handle : Handles)
		bAllSelected = bAllSelected && Sequencer.IsSelected(Handle);

	ImGui::SetCursorScreenPos(ImVec2(X - KeyRadius - 2.0f, CenterY - RowHeight * 0.5f));
	ImGui::InvisibleButton(Id, ImVec2(KeyRadius * 2.0f + 4.0f, RowHeight));
	const bool bHovered = ImGui::IsItemHovered();

	if (ImGui::IsItemActivated())
	{
		// 누르는 순간: Ctrl이면 선택을 토글, 아니면 안 고른 키일 때만 그것만 고른다 (고른 묶음을 그대로 끌 수 있게)
		bDragAdditive = IO.KeyCtrl;
		if (IO.KeyCtrl)
		{
			for (const FSequencerKeyHandle& Handle : Handles)
			{
				if (!bAllSelected)
					Sequencer.SelectKey(Handle, true);
				else if (const int32 Index = Sequencer.Selection.Find(Handle); Index != INDEX_NONE)
					Sequencer.Selection.RemoveAt(Index);
			}
		}
		else if (!bAllSelected)
		{
			Sequencer.ClearSelection();
			for (const FSequencerKeyHandle& Handle : Handles)
				Sequencer.SelectKey(Handle, true);
		}

		Drag = EDrag::Keys;
		DragStartMouseX = IO.MousePos.x;
		bDragMoved = false;
		DragClickedKeys = Handles;
		DragOriginalTimes.Reset();
		for (const FSequencerKeyHandle& Handle : Sequencer.Selection)
		{
			const float* Time = Sequencer.GetKeyTime(Handle);
			DragOriginalTimes.Add(Time ? *Time : 0.0f);
		}
	}

	if (ImGui::IsItemActive() && Drag == EDrag::Keys)
	{
		const float Pixels = IO.MousePos.x - DragStartMouseX;
		if (bDragMoved || std::fabs(Pixels) >= 3.0f)
		{
			if (!bDragMoved)
			{
				Sequencer.BeginTransaction();
				bDragMoved = true;
			}
			const float Delta = Pixels / PixelsPerSecond;
			for (int32 i = 0; i < Sequencer.Selection.Num() && i < DragOriginalTimes.Num(); ++i)
			{
				if (float* Time = Sequencer.GetKeyTime(Sequencer.Selection[i]))
					*Time = Sequencer.SnapTime(std::max(0.0f, DragOriginalTimes[i] + Delta));
			}
			// 정렬해도 Selection 순서는 그대로라 DragOriginalTimes와 짝이 맞는다
			Sequencer.SortKeys();
			Sequencer.RefreshPreview(false);
		}
	}

	if (ImGui::IsItemDeactivated() && Drag == EDrag::Keys)
	{
		// 움직이지 않고 놓았으면 누른 키만 고른다 (여럿 고른 상태에서 하나를 클릭한 경우)
		if (!bDragMoved && !bDragAdditive)
		{
			Sequencer.ClearSelection();
			for (const FSequencerKeyHandle& Handle : DragClickedKeys)
				Sequencer.SelectKey(Handle, true);
		}
		Drag = EDrag::None;
	}

	if (bHovered && Drag == EDrag::None)
	{
		if (ImGui::BeginItemTooltip())
		{
			if (bEvent)
			{
				const FSequenceEventKey* Event = Sequencer.GetEvent(Handles[0]);
				ImGui::Text("%.3f s  %s", KeyTime, Event && !Event->Function.empty() ? Event->Function.c_str() : "(no function)");
			}
			else if (Handles.Num() == 1)
			{
				const FSequenceKey* Key = Sequencer.GetKey(Handles[0]);
				ImGui::Text("%.3f s  value %.4f", KeyTime, Key ? Key->Value : 0.0f);
			}
			else
			{
				ImGui::Text("%.3f s  %d channel keys", KeyTime, Handles.Num());
			}
			ImGui::EndTooltip();
		}
	}

	const bool bSelected = bAllSelected || (Drag == EDrag::Keys && Sequencer.IsSelected(Handles[0]));
	const ImU32 Fill = bSelected ? SelectedKeyColor : (bHovered ? WithAlpha(Color, 255) : WithAlpha(Color, 210));
	const ImU32 Outline = bSelected ? IM_COL32(255, 255, 255, 230) : IM_COL32(0, 0, 0, 160);
	if (bEvent)
	{
		// 이벤트는 위가 납작한 오각형 대신 세운 막대 + 마름모로 구분한다
		DrawList->AddLine(ImVec2(X, CenterY - RowHeight * 0.4f), ImVec2(X, CenterY + RowHeight * 0.4f), Fill, 2.0f);
		DrawDiamond(DrawList, ImVec2(X, CenterY), KeyRadius - 1.0f, Fill, Outline);
	}
	else
	{
		DrawDiamond(DrawList, ImVec2(X, CenterY), KeyRadius, Fill, Outline);
	}
}

void FSequencerPanel::DrawTimelineContextMenu(const FRow& Row)
{
	if (!ImGui::BeginPopupContextItem("##TimelineMenu"))
		return;

	ImGui::TextDisabled("%.3f s", ContextTime);
	ImGui::Separator();

	const FSequenceTrack* Track = Row.Track >= 0 ? Sequencer.GetTrack(Row.Binding, Row.Track) : nullptr;
	if (Row.Type == ERowType::Channel)
	{
		if (ImGui::MenuItem("Add Key Here"))
			Sequencer.AddKey(Row.Binding, Row.Track, Row.Channel, ContextTime);
	}
	else if (Row.Type == ERowType::Track && Track)
	{
		if (Track->Type == ESequenceTrackType::Event)
		{
			if (ImGui::MenuItem("Add Event Here"))
				Sequencer.AddEvent(Row.Binding, Row.Track, ContextTime);
		}
		else if (ImGui::MenuItem("Key Here"))
		{
			Sequencer.KeyTrack(Row.Binding, Row.Track, ContextTime);
		}
	}

	if (ImGui::MenuItem("Move Playhead Here"))
		Sequencer.SetTime(ContextTime);
	if (ImGui::MenuItem("Delete Selected Keys", "Del", false, !Sequencer.Selection.IsEmpty()))
		Sequencer.DeleteSelectedKeys();

	ImGui::EndPopup();
}

void FSequencerPanel::HandleZoomAndPan()
{
	if (!bTimelineHovered)
		return;

	ImGuiIO& IO = ImGui::GetIO();
	if (IO.KeyCtrl && IO.MouseWheel != 0.0f)
	{
		// 마우스 아래 시간이 그대로 있도록 확대한다
		const float MouseTime = XToTime(IO.MousePos.x);
		PixelsPerSecond = std::clamp(PixelsPerSecond * std::pow(1.2f, IO.MouseWheel), 4.0f, 4000.0f);
		ViewStart = MouseTime - (IO.MousePos.x - TimelineX0) / PixelsPerSecond;
	}
	else if (IO.KeyShift && IO.MouseWheel != 0.0f)
	{
		ViewStart -= IO.MouseWheel * 80.0f / PixelsPerSecond;
	}

	if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
		ViewStart -= IO.MouseDelta.x / PixelsPerSecond;

	ViewStart = std::max(ViewStart, -1.0f);
}

void FSequencerPanel::FitView()
{
	ULevelSequence* Sequence = Sequencer.GetSequence();
	if (!Sequence)
		return;

	const float Duration = std::max(Sequence->Duration, 0.1f);
	PixelsPerSecond = std::max(4.0f, (TimelineWidth - 24.0f) / Duration);
	ViewStart = -12.0f / PixelsPerSecond;
}

void FSequencerPanel::HandleShortcuts()
{
	if (!bFocused || ImGui::GetIO().WantTextInput)
		return;

	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z))
		Sequencer.Undo();
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
		Sequencer.Redo();
	if (ImGui::Shortcut(ImGuiKey_Delete))
		Sequencer.DeleteSelectedKeys();
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S) && !Sequencer.GetPath().empty())
		Sequencer.SaveSequence();
}

void FSequencerPanel::PilotViewport()
{
	if (!Sequencer.bPilotCineCamera || !Sequencer.IsPreviewActive() || !Viewports)
	{
		bPilotApplied = false;
		return;
	}

	// 재생 중이거나 시간이 바뀌었을 때(스크럽)만 맞춘다. 멈춰 있으면 사용자가 뷰포트를 자유롭게 움직일 수 있다
	const float Time = Sequencer.GetTime();
	if (bPilotApplied && !Sequencer.IsPlaying() && Time == LastPilotTime)
		return;

	ACameraActor* Camera = Cast<ACameraActor>(Sequencer.GetActiveCineCamera());
	if (!Camera)
		return;
	bPilotApplied = true;
	LastPilotTime = Time;

	const int32 ViewIndex = Viewports->GetActiveViewIndex();
	FEditorViewportClient* Client = Viewports->GetEditorClient(ViewIndex);
	FViewport* Viewport = Viewports->GetViewport(ViewIndex);
	if (!Client || !Viewport)
		return;

	UCameraComponent* Component = Camera->GetCameraComponent();

	// 게임과 같이 카메라 로컬 흔들림을 카메라 기저(Roll 제거)로 돌려 위치에 더한다 (UCameraShakeBase와 같은 계산)
	FMinimalViewInfo POV;
	POV.Location = Component->GetWorldLocation();
	POV.Rotation = Component->GetWorldRotation();
	const FVector Shake = Sequencer.EvaluatePreviewCameraShake();

	FEditorCameraState State;
	State.Location = POV.Location + POV.GetCameraRotationMatrix().TransformVector(Shake);
	State.Rotation = POV.Rotation.Quaternion();
	State.Projection = Component->GetProjectionSettings();
	Client->SetCamera(State, Viewport->GetOutputSize());
}

// ---------------------------------------------------------------- 인스펙터

void FSequencerPanel::DrawInspector()
{
	ULevelSequence* Sequence = Sequencer.GetSequence();
	if (!ImGui::BeginChild("##Inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	TArray<FSequencerKeyHandle>& Selection = Sequencer.Selection;

	// 편집을 시작하는 순간 되돌리기 지점을 남기고, 끝나면 정렬과 미리보기를 갱신한다
	const auto TrackEdit = [&](bool bChanged, bool bResort)
	{
		if (ImGui::IsItemActivated())
			Sequencer.BeginTransaction();
		if (bChanged)
			Sequencer.RefreshPreview(false);
		if (ImGui::IsItemDeactivatedAfterEdit() && bResort)
		{
			Sequencer.SortKeys();
			Sequencer.RefreshPreview(false);
		}
	};

	if (Selection.Num() == 1 && Selection[0].IsEvent())
	{
		FSequenceEventKey* Event = Sequencer.GetEvent(Selection[0]);
		if (Event)
		{
			if (!(EditHandle == Selection[0]))
			{
				EditHandle = Selection[0];
				CopyToBuffer(FunctionBuffer, sizeof(FunctionBuffer), Event->Function);
				CopyToBuffer(ArgsBuffer, sizeof(ArgsBuffer), Event->Args.dump());
				ArgsError.clear();
			}

			ImGui::TextDisabled("Event");
			ImGui::SetNextItemWidth(120.0f);
			TrackEdit(ImGui::DragFloat("Time", &Event->Time, 0.01f, 0.0f, Sequence->Duration, "%.3f s"), true);

			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputText("Function", FunctionBuffer, sizeof(FunctionBuffer));
			if (ImGui::IsItemActivated())
				Sequencer.BeginTransaction();
			if (ImGui::IsItemDeactivatedAfterEdit())
				Event->Function = FunctionBuffer;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Reflected FUNCTION on the bound actor (\"Func\" or \"Component.Func\").\nIf none matches, the actor's Lua function with that name is called.");

			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputText("Args (JSON)", ArgsBuffer, sizeof(ArgsBuffer));
			if (ImGui::IsItemActivated())
				Sequencer.BeginTransaction();
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const json Parsed = json::parse(ArgsBuffer, nullptr, false);
				if (Parsed.is_discarded() || !Parsed.is_array())
				{
					ArgsError = "Args must be a JSON array, e.g. [1.5, \"Name\", [0, 0, 1]]";
				}
				else
				{
					Event->Args = Parsed;
					ArgsError.clear();
				}
			}
			if (!ArgsError.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", ArgsError.c_str());
		}
	}
	else if (Selection.Num() == 1)
	{
		FSequenceKey* Key = Sequencer.GetKey(Selection[0]);
		const FSequenceTrack* Track = Sequencer.GetTrack(Selection[0].Binding, Selection[0].Track);
		if (Key && Track)
		{
			char Label[300];
			ImGui::TextDisabled("%s  /  %s", TrackLabel(*Track, Label, sizeof(Label)), FSequenceTrack::GetChannelName(Track->Type, Selection[0].Channel));

			ImGui::SetNextItemWidth(120.0f);
			TrackEdit(ImGui::DragFloat("Time", &Key->Time, 0.01f, 0.0f, Sequence->Duration, "%.3f s"), true);
			ImGui::SameLine();

			if (Track->Type == ESequenceTrackType::Bool || Track->Type == ESequenceTrackType::Spawn)
			{
				bool bValue = Key->Value >= 0.5f;
				const bool bChanged = ImGui::Checkbox(Track->Type == ESequenceTrackType::Spawn ? "Exists" : "Value", &bValue);
				if (bChanged)
				{
					Sequencer.BeginTransaction();
					Key->Value = bValue ? 1.0f : 0.0f;
					Sequencer.RefreshPreview(false);
				}
			}
			else
			{
				ImGui::SetNextItemWidth(140.0f);
				TrackEdit(ImGui::DragFloat("Value", &Key->Value, 0.01f, 0.0f, 0.0f, "%.4f"), false);
			}

			ImGui::SameLine();
			static const char* InterpNames[] = { "Constant", "Linear", "Cubic" };
			int Interp = static_cast<int>(Key->Interp);
			ImGui::SetNextItemWidth(110.0f);
			if (ImGui::Combo("Interp", &Interp, InterpNames, IM_ARRAYSIZE(InterpNames)))
			{
				Sequencer.BeginTransaction();
				Key->Interp = static_cast<ESequenceInterp>(Interp);
				Sequencer.RefreshPreview(false);
			}
		}
	}
	else if (Selection.Num() > 1)
	{
		ImGui::Text("%d keys selected", Selection.Num());
		static const char* InterpNames[] = { "Constant", "Linear", "Cubic" };
		static int Interp = 2;
		ImGui::SetNextItemWidth(110.0f);
		ImGui::Combo("##MultiInterp", &Interp, InterpNames, IM_ARRAYSIZE(InterpNames));
		ImGui::SameLine();
		if (ImGui::Button("Set Interp"))
		{
			Sequencer.BeginTransaction();
			for (const FSequencerKeyHandle& Handle : Selection)
			{
				if (FSequenceKey* Key = Sequencer.GetKey(Handle))
					Key->Interp = static_cast<ESequenceInterp>(Interp);
			}
			Sequencer.RefreshPreview(false);
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete"))
			Sequencer.DeleteSelectedKeys();
	}
	else if (Sequencer.SelectedBinding >= 0 && Sequencer.SelectedBinding < Sequence->Bindings.Num() && Sequencer.SelectedTrack < 0)
	{
		FSequenceBinding& Binding = Sequence->Bindings[Sequencer.SelectedBinding];
		if (EditBinding != Sequencer.SelectedBinding)
		{
			EditBinding = Sequencer.SelectedBinding;
			CopyToBuffer(NameBuffer, sizeof(NameBuffer), Binding.Name);
			CopyToBuffer(TagBuffer, sizeof(TagBuffer), Binding.Tag);
		}

		ImGui::TextDisabled("Binding");
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer));
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Sequencer.BeginTransaction();
			Binding.Name = NameBuffer;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputText("Tag", TagBuffer, sizeof(TagBuffer));
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Sequencer.BeginTransaction();
			Binding.Tag = TagBuffer;
			Sequencer.RefreshPreview(true);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Borrows scene actors whose BindingTag matches. Leave empty to always spawn from the template.");

		bool bCine = Binding.bIsCineCamera;
		if (ImGui::Checkbox("Cine Camera", &bCine))
			Sequencer.SetCineCamera(Sequencer.SelectedBinding, bCine);
		ImGui::SameLine();
		ImGui::TextDisabled("Template: %s", Binding.HasTemplate() ? Binding.Template.value("Class", FString("?")).c_str() : "none");
		ImGui::SameLine();
		AActor* Bound = Sequencer.GetBoundActor(Sequencer.SelectedBinding);
		ImGui::TextDisabled("Bound: %s", Bound ? Bound->GetName().c_str() : "(not resolved)");
	}
	else if (Sequencer.SelectedTrack >= 0)
	{
		FSequenceTrack* Track = Sequencer.GetTrack(Sequencer.SelectedBinding, Sequencer.SelectedTrack);
		if (Track)
		{
			ImGui::TextDisabled("%s track", FSequenceTrack::GetTypeName(Track->Type));
			if (Track->Type != ESequenceTrackType::Event && Track->Type != ESequenceTrackType::Spawn)
			{
				if (EditBinding != -100 - Sequencer.SelectedTrack)
				{
					EditBinding = -100 - Sequencer.SelectedTrack;
					CopyToBuffer(PropertyBuffer, sizeof(PropertyBuffer), Track->PropertyPath);
				}
				ImGui::SetNextItemWidth(260.0f);
				ImGui::InputText(Track->Type == ESequenceTrackType::Transform ? "Component" : "Property", PropertyBuffer, sizeof(PropertyBuffer));
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Sequencer.BeginTransaction();
					Track->PropertyPath = PropertyBuffer;
					Sequencer.RefreshPreview(true);
				}
			}
			for (int32 s = 0; s < Track->Sections.Num(); ++s)
			{
				ImGui::PushID(s);
				FSequenceSection& Section = Track->Sections[s];
				ImGui::Text("Section %d", s);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(110.0f);
				TrackEdit(ImGui::DragFloat("Start", &Section.StartTime, 0.01f, 0.0f, Section.EndTime, "%.3f s"), false);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(110.0f);
				TrackEdit(ImGui::DragFloat("End", &Section.EndTime, 0.01f, Section.StartTime, 1000.0f, "%.3f s"), false);
				ImGui::PopID();
			}
		}
	}
	else
	{
		ImGui::TextDisabled("Sequence");
		ImGui::SetNextItemWidth(100.0f);
		TrackEdit(ImGui::DragFloat("Duration", &Sequence->Duration, 0.05f, 0.1f, 600.0f, "%.2f s"), false);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		TrackEdit(ImGui::DragFloat("Display Rate", &Sequence->DisplayRate, 1.0f, 1.0f, 240.0f, "%.0f fps"), false);
		ImGui::SameLine();
		bool bCut = Sequence->CineCameraEndTime >= 0.0f;
		if (ImGui::Checkbox("Camera Cut End", &bCut))
		{
			Sequencer.BeginTransaction();
			Sequence->CineCameraEndTime = bCut ? Sequence->Duration : -1.0f;
		}
		if (bCut)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			TrackEdit(ImGui::DragFloat("##CutEnd", &Sequence->CineCameraEndTime, 0.01f, 0.0f, Sequence->Duration, "%.2f s"), false);
		}

		ImGui::SetNextItemWidth(80.0f);
		TrackEdit(ImGui::DragFloat("Blend In", &Sequence->CameraBlendInTime, 0.01f, 0.0f, 30.0f, "%.2f s"), false);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		TrackEdit(ImGui::DragFloat("Blend Out", &Sequence->CameraBlendOutTime, 0.01f, 0.0f, 30.0f, "%.2f s"), false);
		ImGui::SameLine();
		static const char* BlendNames[] = { "Linear", "Cubic", "EaseIn", "EaseOut", "EaseInOut" };
		int BlendIndex = 1;
		for (int i = 0; i < IM_ARRAYSIZE(BlendNames); ++i)
			if (Sequence->CameraBlendFunction == BlendNames[i])
				BlendIndex = i;
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::Combo("Blend", &BlendIndex, BlendNames, IM_ARRAYSIZE(BlendNames)))
		{
			Sequencer.BeginTransaction();
			Sequence->CameraBlendFunction = BlendNames[BlendIndex];
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		TrackEdit(ImGui::DragFloat("Exp", &Sequence->CameraBlendExp, 0.05f, 0.1f, 10.0f, "%.2f"), false);

		ImGui::TextDisabled("Effects presets: %d (edit in the .sequence file).  Ctrl+Wheel zoom, Shift+Wheel or middle drag pan, Del deletes keys.",
			static_cast<int>(Sequence->Effects.is_object() ? Sequence->Effects.size() : 0));
	}

	if (Selection.IsEmpty())
		EditHandle = FSequencerKeyHandle();
	if (Sequencer.SelectedBinding < 0 && Sequencer.SelectedTrack < 0)
		EditBinding = -3;

	ImGui::EndChild();
}
