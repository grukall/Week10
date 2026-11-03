#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/Viewport.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/BoneGizmoTarget.h"

class AActor;
class USkeletalMesh;
class USkeletalMeshComponent;
class AStaticMeshActor;
class ADirectionalLightActor;
class AAmbientLightActor;
class FLineBatcher;

struct FSkeletalMeshDocument
{
	USkeletalMesh* SkeletalMesh = nullptr;

	TArray<FTransform> EditableLocalPose;
	TArray<FMatrix> LocalPoseMatrices;

	TArray<TArray<int32>> BoneChildren;
	TArray<int32> RootBones;

	int32 SelectedBoneIndex = INDEX_NONE;

	bool bPoseDirty = false;
	bool bModified = false;
	bool bFitCameraPending = false;

	FEditorCameraState CameraState;
	bool bHasCameraState = false;
};

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
	void InitializeDocument(FSkeletalMeshDocument& Document, USkeletalMesh* SkeletalMesh);
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

	int32 GetSelectedBoneIndex() const;
	bool IsBoneValid(int32 BoneIndex) const;
	int32 GetBoneParentIndex(int32 BoneIndex) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	FMatrix GetBoneGlobalMatrix(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Transform);

	FGizmo* GetBoneGizmo() const { return BoneGizmo.get(); }
	void UpdateBoneGizmoInput(bool bInputEnabled);

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	void RefreshSkeletalPreviewMesh(int32 BoneIndex = INDEX_NONE);

	FSkeletalMeshDocument* GetActiveDocument();
	const FSkeletalMeshDocument* GetActiveDocument() const;

	void OpenSkeletalMesh(USkeletalMesh* SkeletalMesh);
	void ActivateDocument(int32 DocumentIndex);
	void CloseDocument(int32 Index);

private:
	const TArray<FMatrix>& GetGlobalPose() const;
	void BuildBoneHierarchy(FSkeletalMeshDocument& Document);
	void DrawBoneNode(int32 BoneIndex);
	void CreateDefaultSkeletalPreviewScene();

	void FitCameraToMesh();

	int32 PickBone(const FVector2& MousePosition, const FMatrix& ViewProjection, int32 ScreenWidth, int32 ScreenHeight) const;

	void ExportReboundFbx();

private:
	AActor* SkeletalPreviewActor = nullptr;
	USkeletalMeshComponent* SkeletalPreviewComponent = nullptr;

	TUniquePtr<FViewport> SkeletalPreviewViewport;
	FEditorViewportClient* SkeletalPreviewClient = nullptr;
	UWorld* SkeletalPreviewWorld = nullptr;

	AStaticMeshActor* SkeletalPreviewFloor = nullptr;
	ADirectionalLightActor* SkeletalPreviewDirectionalLight = nullptr;
	AAmbientLightActor* SkeletalPreviewAmbientLight = nullptr;

	FRect SkeletalPreviewScreenRect{};
	bool bSkeletalPreviewCaptured = false;

	TUniquePtr<FGizmo> BoneGizmo;
	TUniquePtr<FBoneGizmoTarget> BoneGizmoTarget;

	TArray<FSkeletalMeshDocument> Documents;
	int32 ActiveDocumentIndex = INDEX_NONE;
};
