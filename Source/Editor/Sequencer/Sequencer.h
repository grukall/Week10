#pragma once

#include "LevelSequence/LevelSequence.h"
#include "LevelSequence/LevelSequencePlayer.h"

#include <string>

class UWorld;
class AActor;
class USceneComponent;

// 시퀀서에서 키 하나를 가리킨다. 인덱스라서 키를 정렬하면 SortKeys가 다시 맞춘다.
struct FSequencerKeyHandle
{
	int32 Binding = -1;   // -1이면 마스터 트랙
	int32 Track = -1;
	int32 Section = -1;
	int32 Channel = -1;   // -1이면 Event 키
	int32 Key = -1;

	bool IsEvent() const { return Channel < 0; }
	bool operator==(const FSequencerKeyHandle& Other) const = default;
};

// 시퀀서의 편집 상태 (UE FSequencer). 화면은 FSequencerPanel이 그린다.
//   에셋: 하나를 열어 고치고 저장한다. 고칠 때마다 BeginTransaction으로 되돌리기 지점을 남긴다.
//   미리보기: 에디터 월드에 플레이어를 하나 두고 SetTime으로 평가한다. 에디터 월드는 플레이어를 Tick하지 않는다.
//             미리보기가 바꾼 값은 ReleasePreview가 되돌린다 (씬 저장·PIE 전에 엔진이 부른다).
class FSequencer
{
public:
	FSequencer() = default;
	~FSequencer();

	FSequencer(const FSequencer&) = delete;
	FSequencer& operator=(const FSequencer&) = delete;

	void SetWorld(UWorld* InWorld);
	UWorld* GetWorld() const { return World; }

	// ---------- 에셋 ----------
	void NewSequence();
	bool OpenSequence(const FString& Path);
	bool SaveSequence();
	bool SaveSequenceAs(const FString& Path);
	ULevelSequence* GetSequence() const { return Sequence; }
	const FString& GetPath() const { return Path; }
	bool IsDirty() const { return bDirty; }

	// 미리보기를 되돌리고 플레이어와 에셋을 지운다. 에디터가 종료되기 전에 부른다.
	void Shutdown();

	// ---------- 시간과 미리보기 ----------
	float GetTime() const { return Time; }
	// 시간을 옮기고 미리보기를 평가한다
	void SetTime(float NewTime);
	float SnapTime(float InTime) const;

	void TogglePlay();
	bool IsPlaying() const { return bPlaying; }
	void Tick(float DeltaTime);

	// 미리보기가 바꾼 값을 되돌리고, 미리보기로 스폰한 액터를 지운다. 다음 SetTime에서 다시 적용된다.
	void ReleasePreview();
	bool IsPreviewActive() const { return bPreviewActive; }
	// 편집 뒤 미리보기를 다시 평가한다. bRebind면 바인딩을 다시 찾는다 (바인딩·트랙을 더하거나 뺐을 때)
	void RefreshPreview(bool bRebind);

	bool bLoop = true;
	bool bSnap = true;
	// 미리보기 중 에디터 뷰포트를 시네 카메라 시점에 맞춘다 (패널이 처리). 켜면 뷰포트를 붙잡으므로 기본은 끈다
	bool bPilotCineCamera = false;

	// 바인딩이 지금 미리보기에서 붙잡은 첫 액터. 아직 평가하지 않았거나 못 찾았으면 nullptr
	AActor* GetBoundActor(int32 BindingIndex) const;
	// CineCameraEndTime 전이고 시네 카메라 바인딩이 해석됐으면 그 액터
	AActor* GetActiveCineCamera() const;

	// 에디터에서는 이벤트가 불리지 않고 PlayerCameraManager도 없어서 카메라 쉐이크가 실행되지 않는다.
	// 대신 시네 카메라 바인딩의 StartCameraShake 이벤트들이 지금 시간에 만드는 흔들림을 직접 더해 돌려준다
	// (카메라 로컬 공간). 쉐이크는 시작 후 경과 시간만으로 정해지므로 스크럽해도 게임과 같은 모양이 나온다.
	FVector EvaluatePreviewCameraShake() const;

	// ---------- 되돌리기 ----------
	// 고치기 직전에 부른다. 지금 상태를 되돌리기 목록에 남긴다
	void BeginTransaction();
	void MarkDirty() { bDirty = true; }
	void Undo();
	void Redo();
	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }

	// ---------- 데이터 접근 ----------
	// Binding이 -1이면 마스터 트랙
	TArray<FSequenceTrack>* GetTracks(int32 BindingIndex);
	FSequenceTrack* GetTrack(int32 BindingIndex, int32 TrackIndex);
	FSequenceKey* GetKey(const FSequencerKeyHandle& Handle);
	FSequenceEventKey* GetEvent(const FSequencerKeyHandle& Handle);
	// 키 또는 이벤트의 시간
	float* GetKeyTime(const FSequencerKeyHandle& Handle);

	// ---------- 선택 ----------
	TArray<FSequencerKeyHandle> Selection;
	bool IsSelected(const FSequencerKeyHandle& Handle) const;
	void SelectKey(const FSequencerKeyHandle& Handle, bool bAdditive);
	void ToggleKey(const FSequencerKeyHandle& Handle);
	void ClearSelection() { Selection.Reset(); }
	void DeleteSelectedKeys();
	// 키 시간을 바꾼 뒤 부른다. 모든 채널·이벤트를 시간순으로 다시 정렬하고 Selection 인덱스를 맞춘다
	void SortKeys();

	// 트리에서 고른 줄 (인스펙터가 보여 준다). Track이 -1이면 바인딩 줄
	int32 SelectedBinding = -2;   // -2: 없음, -1: 마스터
	int32 SelectedTrack = -1;

	// ---------- 만들기 ----------
	// bSpawnable이면 액터를 템플릿으로 적고, 아니면 BindingTag로 빌린다 (태그가 없으면 붙인다)
	int32 AddBindingForActor(AActor* Actor, bool bSpawnable);
	// 템플릿 카메라 바인딩을 만들고 시네 카메라로 지정한다
	int32 AddCineCameraBinding(const FVector& Location, const FRotator& Rotation, float FieldOfView);
	void RemoveBinding(int32 BindingIndex);
	int32 AddTrack(int32 BindingIndex, ESequenceTrackType Type, const FString& PropertyPath);
	void RemoveTrack(int32 BindingIndex, int32 TrackIndex);
	void SetCineCamera(int32 BindingIndex, bool bEnable);

	// Time이 들어가는 섹션. 없으면 가장 가까운 섹션을 늘리고, 섹션이 아예 없으면 [0, Duration]으로 만든다
	FSequenceSection& EnsureSection(FSequenceTrack& Track, float AtTime);
	// 채널 하나에 키를 찍는다. 값은 커브가 있으면 커브 값, 없으면 액터의 지금 값
	void AddKey(int32 BindingIndex, int32 TrackIndex, int32 Channel, float AtTime);
	// 트랙의 모든 채널에 지금 값으로 키를 찍는다 (Transform이면 9개)
	void KeyTrack(int32 BindingIndex, int32 TrackIndex, float AtTime);
	void AddEvent(int32 BindingIndex, int32 TrackIndex, float AtTime);

	// 액터의 지금 값 (미리보기가 적용된 값 포함). 못 읽으면 false
	bool ReadCurrentValue(int32 BindingIndex, int32 TrackIndex, int32 Channel, float& OutValue);
	// Transform 트랙이 움직이는 컴포넌트 (PropertyPath가 비었으면 루트)
	static USceneComponent* FindTransformTarget(AActor* Actor, const FSequenceTrack& Track);

private:
	void ResetPlayer();
	void DeletePlayerAndSequence();
	void RestoreFromJson(const std::string& Text);
	std::string Snapshot() const;

	UWorld* World = nullptr;
	ULevelSequence* Sequence = nullptr;
	ULevelSequencePlayer* Player = nullptr;
	// 엔진 종료 때 UObject가 일괄 삭제되므로, 지우기 전에 살아 있는지 UUID로 확인한다
	uint32 SequenceUUID = 0;
	uint32 PlayerUUID = 0;

	FString Path;
	bool bDirty = false;

	float Time = 0.0f;
	bool bPlaying = false;
	bool bPreviewActive = false;

	// 직렬화한 JSON 문자열. 큰 시퀀스도 문자열이면 메모리를 덜 쓴다
	TArray<std::string> UndoStack;
	TArray<std::string> RedoStack;
	static constexpr int32 MaxUndo = 64;
};
