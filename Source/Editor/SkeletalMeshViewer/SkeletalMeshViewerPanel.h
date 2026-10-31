#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/Viewport.h"

class AStaticMeshActor;
class ADirectionalLightActor;
class AAmbientLightActor;
class FLineBatcher;

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void DrawBoneDebug(FLineBatcher& LineBatcher) const;
	FVector GetBoneGlobalPosition(int32 BoneIndex) const;

	const char* GetPanelName() const override { return "SkeletalMesh Viewer"; }

	bool InitializeSkeletalPreview(UWorld* InSkeletalMeshWorld);
	void ShutdownSkeletalPreview();
	bool CanRenderSkeletalPreview() const;

	FViewport* GetSkeletalPreviewViewport() const { return SkeletalPreviewViewport.get(); }
	FEditorViewportClient* GetSkeletalPreviewClient() const { return SkeletalPreviewClient; }
	UWorld* GetSkeletalPreviWorld() const { return SkeletalPreviewWorld; }

	bool IsSkeletalPreviewHovered() const;
	bool IsSkeletalPreviewCaptured() const;
	void UpdateSkeletalPreviewInput();
	void RouteSkeletalPreviewInput(FViewportInput Input);
	void ReleaseSkeletalPreviewInput();

	void RebuildGlobalPose();

private:
	// 임시 Skeleton 데이터
	// 나중에 Bone 데이터랑 연결
	struct FMockBone
	{
		FString Name;
		int32 ParentIndex = -1;
		FTransform ReferenceLocalTransform;
	};
	void CreateMockSkeleton();
	void AddMockBone(const char* Name, int32 ParentIndex, const FVector& LocalLocation);
	TArray<FMockBone> MockBones;

	TArray<FTransform> EditableLocalPose; // 수정할 현재 자세
	TArray<FMatrix> GlobalPose;
	int32 SelectedBoneIndex = -1;         // 현재 선택된 BoneIndex
	bool bPoseDirty = true;

	void BuildMockBoneHierarchy();
	void DrawBoneNode(int32 BoneIndex);
	TArray<TArray<int32>> MockBoneChildren;
	TArray<int32> MockRootBones;

	TUniquePtr<FViewport> SkeletalPreviewViewport;
	FEditorViewportClient* SkeletalPreviewClient = nullptr;
	UWorld* SkeletalPreviewWorld = nullptr;

	void CreateDefaultSkeletalPreviewScene();
	AStaticMeshActor* SkeletalPreviewFloor = nullptr;
	ADirectionalLightActor* SkeletalPreviewDirectionalLight = nullptr;
	AAmbientLightActor* SkeletalPreviewAmbientLight = nullptr;

	FRect SkeletalPreviewScreenRect{};
	bool bSkeletalPreviewCaptured = false;
};