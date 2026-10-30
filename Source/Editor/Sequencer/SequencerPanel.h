#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Sequencer/Sequencer.h"

#include <functional>
#include <unordered_set>

class UWorld;
class AActor;
class FViewportsPanel;

// 레벨 시퀀스 편집 창 (UE의 Sequencer 탭).
//   왼쪽: 바인딩 → 트랙 → 채널 트리. 오른쪽: 눈금자, 재생 헤드, 섹션 막대, 키.
//   키·섹션은 DrawList로 그리고 그 자리에 InvisibleButton을 깔아 클릭·드래그를 받는다.
//   편집 상태와 미리보기는 FSequencer가 들고, 이 패널은 그리기와 입력만 맡는다.
class FSequencerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Sequencer"; }

	void SetWorld(UWorld* InWorld) { Sequencer.SetWorld(InWorld); }
	void SetViewportsPanel(FViewportsPanel* InViewports) { Viewports = InViewports; }
	void SetSelectedActorQuery(std::function<AActor*()> InQuery) { GetSelectedActor = std::move(InQuery); }
	// 에디터 선택(아웃라이너·디테일·기즈모)을 비운다. 미리보기가 선택된 스폰 액터를 지울 때 부른다
	void SetResetSelectionCallback(std::function<void()> InCallback) { ResetSelection = std::move(InCallback); }

	// 씬 저장·PIE·씬 열기 전에 엔진이 부른다. 미리보기가 바꾼 값을 되돌린다
	void ReleasePreview() { Sequencer.ReleasePreview(); ClearSelectionIfDestroyed(); }
	// 에디터 종료 전에 부른다 (UObject 일괄 삭제보다 먼저)
	void Shutdown() { Sequencer.Shutdown(); }
	// 이 창에 포커스가 있으면 에디터의 Delete(액터 삭제)를 막는다
	bool IsFocused() const { return bFocused; }

	FSequencer& GetSequencer() { return Sequencer; }

private:
	enum class ERowType : uint8 { Master, Binding, Track, Channel };
	struct FRow
	{
		ERowType Type;
		int32 Binding;   // -1: 마스터
		int32 Track;
		int32 Channel;
		int32 Depth;
	};

	enum class EDrag : uint8 { None, Scrub, Keys, SectionMove, SectionStart, SectionEnd };

	void BuildRows(TArray<FRow>& OutRows) const;
	// 줄이 가리키는 바인딩·트랙·채널이 아직 있는지. 편집으로 인덱스가 어긋난 줄은 그리지 않는다
	bool IsRowValid(const FRow& Row);
	void DrawToolbar();
	void DrawTimeline();
	void DrawInspector();

	void DrawRuler();
	void DrawRowLabel(const FRow& Row);
	void DrawRowTimeline(const FRow& Row);
	void DrawLabelContextMenu(const FRow& Row);
	void DrawTimelineContextMenu(const FRow& Row);
	void DrawAddTrackMenu(int32 BindingIndex);

	// 키 하나(또는 같은 시간의 키 묶음)를 그리고 클릭·드래그를 처리한다
	void KeyItem(const char* Id, float KeyTime, float CenterY, ImU32 Color, const TArray<FSequencerKeyHandle>& Handles, bool bEvent);
	void SectionItems(int32 BindingIndex, int32 TrackIndex, int32 SectionIndex, float RowTop, ImU32 Color, bool bInteractive);
	void HandleZoomAndPan();
	void HandleShortcuts();
	void PilotViewport();
	void FitView();

	float TimeToX(float Time) const { return TimelineX0 + (Time - ViewStart) * PixelsPerSecond; }
	float XToTime(float X) const { return ViewStart + (X - TimelineX0) / PixelsPerSecond; }
	bool IsExpanded(int32 Binding, int32 Track) const;
	void SetExpanded(int32 Binding, int32 Track, bool bExpanded);

	// 바인딩·트랙을 더하거나 빼는 편집은 그리기가 끝난 뒤에 실행한다.
	// 그리는 도중에 바꾸면 이번 프레임에 만든 줄 목록의 인덱스가 어긋난다.
	void Defer(std::function<void()> Action) { DeferredActions.Add(std::move(Action)); }
	void FlushDeferredActions();
	// 바인딩·트랙을 지운 뒤 뒤쪽 줄의 펼침 상태를 한 칸씩 당긴다
	void ShiftExpandedAfterRemove(int32 Binding, int32 Track);

	TArray<std::function<void()>> DeferredActions;

	// 미리보기가 스폰한 액터를 지우면 Destroy 표시만 되고 다음 월드 Tick에 delete 된다.
	// 그 액터가 선택돼 있으면 에디터 패널들이 지워진 포인터를 들게 되므로, 표시된 즉시 선택을 비운다
	void ClearSelectionIfDestroyed();

	FSequencer Sequencer;
	FViewportsPanel* Viewports = nullptr;
	std::function<AActor*()> GetSelectedActor;
	std::function<void()> ResetSelection;

	// 타임라인 보기: 왼쪽 끝 시간과 1초의 픽셀 수
	float ViewStart = 0.0f;
	float PixelsPerSecond = 120.0f;
	float TimelineX0 = 0.0f;
	float TimelineWidth = 1.0f;
	float RowHeight = 22.0f;
	bool bFitPending = true;
	bool bTimelineHovered = false;
	bool bFocused = false;

	// 시네 카메라 시점을 마지막으로 맞춘 시간. 같은 시간이면 다시 맞추지 않아 뷰포트를 놓아준다
	float LastPilotTime = -1.0f;
	bool bPilotApplied = false;

	std::unordered_set<uint64> ExpandedRows;

	// 드래그 상태
	EDrag Drag = EDrag::None;
	float DragStartMouseX = 0.0f;
	bool bDragMoved = false;
	bool bDragAdditive = false;
	TArray<float> DragOriginalTimes;           // Selection과 같은 순서
	TArray<FSequencerKeyHandle> DragClickedKeys;
	int32 DragBinding = -1, DragTrack = -1, DragSection = -1;
	FSequenceSection DragSectionOriginal;

	// 오른쪽 클릭한 시간 (컨텍스트 메뉴에서 쓴다)
	float ContextTime = 0.0f;

	// 인스펙터 편집 버퍼
	FSequencerKeyHandle EditHandle;
	int32 EditBinding = -3;
	char FunctionBuffer[256] = {};
	char ArgsBuffer[1024] = {};
	char NameBuffer[256] = {};
	char TagBuffer[256] = {};
	char PropertyBuffer[256] = {};
	FString ArgsError;
};
