#include "EnginePCH.h"
#include "SkeletalMeshViewerPanel.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Asset/AssetManager.h"
#include "Component/BillboardComponent.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Math/Rotator.h"
#include "Input/InputSystem.h"
#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Component/SkeletalMeshComponent.h"
#include "Asset/FbxImporter/FbxImporter.h"
#include "Asset/FbxExporter/FbxPoseExporter.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return std::isfinite(Value.X)
			&& std::isfinite(Value.Y)
			&& std::isfinite(Value.Z);
	}

	bool IsValidEditableTransform(const FTransform& Transform)
	{
		return IsFiniteVector(Transform.Location)
			&& std::isfinite(Transform.Rotation.Pitch)
			&& std::isfinite(Transform.Rotation.Yaw)
			&& std::isfinite(Transform.Rotation.Roll)
			&& IsFiniteVector(Transform.Scale)
			&& std::fabs(Transform.Scale.X) > SMALL_NUMBER
			&& std::fabs(Transform.Scale.Y) > SMALL_NUMBER
			&& std::fabs(Transform.Scale.Z) > SMALL_NUMBER;
	}

	FTransform MatrixToTransform(const FMatrix& Matrix)
	{
		FTransform Transform;
		Transform.Location = FVector(Matrix[3][0], Matrix[3][1], Matrix[3][2]);

		FVector XAxis(Matrix[0][0], Matrix[0][1], Matrix[0][2]);
		FVector YAxis(Matrix[1][0], Matrix[1][1], Matrix[1][2]);
		FVector ZAxis(Matrix[2][0], Matrix[2][1], Matrix[2][2]);
		Transform.Scale = FVector(XAxis.Size(), YAxis.Size(), ZAxis.Size());
		if (Matrix.Determinant() < 0.0f)
		{
			Transform.Scale.X = -Transform.Scale.X;
			XAxis = -XAxis;
		}

		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.SetAxis(0, XAxis.Normalized());
		RotationMatrix.SetAxis(1, YAxis.Normalized());
		RotationMatrix.SetAxis(2, ZAxis.Normalized());
		Transform.Rotation = MatrixToRotator(RotationMatrix);
		return Transform;
	}

	void AddWireCircle(FLineBatcher& LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB, 
		float Radius, int32 Segments, const FVector4& Color)
	{
		if (Radius <= 0.0f || Segments < 3)
		{
			return;
		}

		const float AngleStep = 2.0f * PI / static_cast<float>(Segments);

		for (int32 SegmentIndex = 0; SegmentIndex < Segments; ++SegmentIndex)
		{
			const float CurrentAngle = AngleStep * static_cast<float>(SegmentIndex);
			const float NextAngle = AngleStep * static_cast<float>(SegmentIndex + 1);

			const FVector CurrentPoint = Center + (AxisA * std::cosf(CurrentAngle) + AxisB * std::sinf(CurrentAngle)) * Radius;
			const FVector NextPoint = Center + (AxisA * std::cosf(NextAngle) + AxisB * std::sinf(NextAngle)) * Radius;

			LineBatcher.AddLine(CurrentPoint, NextPoint, Color);
		}
	}

	void AddWireSphere(FLineBatcher& LineBatcher, const FVector& Center, float Radius, int32 Segments, const FVector4& Color)
	{
		const FVector AxisX(1.0f, 0.0f, 0.0f);
		const FVector AxisY(0.0f, 1.0f, 0.0f);
		const FVector AxisZ(0.0f, 0.0f, 1.0f);
		AddWireCircle(LineBatcher, Center, AxisX, AxisY, Radius, Segments, Color);
		AddWireCircle(LineBatcher, Center, AxisX, AxisZ, Radius, Segments, Color);
		AddWireCircle(LineBatcher, Center, AxisY, AxisZ, Radius, Segments, Color);
	}
}

bool FSkeletalMeshViewerPanel::Init()
{
	BoneGizmo = MakeUnique<FGizmo>();
	BoneGizmoTarget = MakeUnique<FBoneGizmoTarget>(this);
	BoneGizmo->SetTarget(BoneGizmoTarget.get());
	BoneGizmo->SetMode(EGizmoMode::Location);
	BoneGizmo->SetSpace(EGizmoSpace::Local);

	return true;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();
	if (!Document)
	{
		return;
	}

	if (Document->bFitCameraPending)
	{
		FitCameraToMesh();
	}
	
	if (Document->bPoseDirty)
	{
		RefreshSkeletalPreviewMesh(INDEX_NONE);
	}
}

void FSkeletalMeshViewerPanel::OnRender()
{
	FSkeletalMeshDocument* Document = GetActiveDocument();
	if (!Document)
	{
		return;
	}

	const TArray<FMatrix>& GlobalPose = GetGlobalPose();
	bool bOpen = true;
	const bool bVisible = ImGui::Begin(GetPanelName(), &bOpen);

	if (!bOpen)
	{
		SetOpen(false);
		SkeletalPreviewScreenRect = {};
	}

	if (!bVisible)
	{
		SkeletalPreviewScreenRect = {};
		if (SkeletalPreviewViewport)
		{
			SkeletalPreviewViewport->SetOutputSize(0, 0);
		}
		ImGui::End();
		return;
	}

	int32 DocumentToClose = INDEX_NONE;

	if (ImGui::BeginTabBar("##SkeletalMeshResourceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
	{
		for (int32 Index = 0; Index < Documents.Num(); ++Index)
		{
			FSkeletalMeshDocument& Document = Documents[Index];
			FString VisibleName = std::filesystem::path(Document.SkeletalMesh->GetPath()).stem().string();

			if (Document.bModified)
			{
				VisibleName += "*";
			}

			const FString TabLabel = VisibleName + "###SkeletalMeshDocument_" + std::to_string(Index);

			bool bTabOpen = true;

			if (ImGui::BeginTabItem(TabLabel.c_str(), &bTabOpen))
			{
				if (ActiveDocumentIndex != Index)
				{
					ActivateDocument(Index);
				}

				ImGui::EndTabItem();
			}

			if (!bTabOpen)
			{
				DocumentToClose = Index;
			}
		}

		ImGui::EndTabBar();
	}

	if (DocumentToClose != INDEX_NONE)
	{
		CloseDocument(DocumentToClose);
	}

	Document = GetActiveDocument();
	if (!Document)
	{
		SkeletalPreviewScreenRect = {};
		if (SkeletalPreviewViewport)
		{
			SkeletalPreviewViewport->SetOutputSize(0, 0);
		}
		ImGui::End();
		return;
	}

	const float BottomAreaHeight = 35.0f;
	const ImGuiTableFlags TableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;

	if (ImGui::BeginTable("##SkeletalMeshViewerLayout",
		3, TableFlags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - BottomAreaHeight)))
	{
		ImGui::TableSetupColumn("Bone Tree", ImGuiTableColumnFlags_WidthFixed, 240.0f);
		ImGui::TableSetupColumn("Bone Transform", ImGuiTableColumnFlags_WidthFixed, 280.0f);
		ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Bone Hierarchy");
		ImGui::Separator();
		if (ImGui::BeginTabBar("##SkeletalMeshViewer"))
		{
			if (ImGui::BeginTabItem("Skeleton"))
			{
				if (Document->RootBones.IsEmpty())
				{
					ImGui::TextDisabled("No root bone.");
				}
				else
				{
					for (int32 RootBoneIndex : Document->RootBones)
					{
						DrawBoneNode(RootBoneIndex);
					}
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Bone Transform");
		ImGui::Separator();
		if (!Document->SkeletalMesh || !Document->SkeletalMesh->MeshData.RefBones.IsValidIndex(Document->SelectedBoneIndex))
		{
			ImGui::TextDisabled("No bone is selected.");
		}
		else
		{
			const FSkeletalMeshRefBone& SelectedBone = Document->SkeletalMesh->MeshData.RefBones[Document->SelectedBoneIndex];
			FTransform EditedTransform = Document->EditableLocalPose[Document->SelectedBoneIndex];

			const FString SelectedBoneName = SelectedBone.Name.ToString();
			ImGui::Text("Name: %s", SelectedBoneName.c_str());
			ImGui::Text("Index: %d", Document->SelectedBoneIndex);
			if (Document->SkeletalMesh->MeshData.RefBones.IsValidIndex(SelectedBone.ParentIndex))
			{
				const FString ParentBoneName = Document->SkeletalMesh->MeshData.RefBones[SelectedBone.ParentIndex].Name.ToString();
				ImGui::Text("Parent: %s", ParentBoneName.c_str());
			}
			else
			{
				ImGui::TextUnformatted("Parent: None");
			}

			ImGui::Separator();
			ImGui::TextDisabled("Transform");
			bool bTransformChanged = false;

			bTransformChanged |= ImGui::DragFloat3("Location", &EditedTransform.Location.X, 0.1f);
			bTransformChanged |= ImGui::DragFloat3("Rotation", &EditedTransform.Rotation.Pitch, 0.1f);
			bTransformChanged |= ImGui::DragFloat3("Scale", &EditedTransform.Scale.X, 0.01f, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			if (bTransformChanged && IsValidEditableTransform(EditedTransform))
			{
				SetBoneLocalTransform(Document->SelectedBoneIndex, EditedTransform);
				RefreshSkeletalPreviewMesh(Document->SelectedBoneIndex);
			}

			if (GlobalPose.IsValidIndex(Document->SelectedBoneIndex))
			{
				const FMatrix& GlobalMatrix = GlobalPose[Document->SelectedBoneIndex];
				ImGui::Separator();
				ImGui::TextDisabled("Global Location");
				ImGui::Text("X %.2f  Y %.2f  Z %.2f", GlobalMatrix[3][0], GlobalMatrix[3][1], GlobalMatrix[3][2]);
			}

			ImGui::Separator();
			if (ImGui::Button("Reset Bone"))
			{
				Document->EditableLocalPose[Document->SelectedBoneIndex] = MatrixToTransform(SelectedBone.RefLocalMatrix);
				Document->LocalPoseMatrices[Document->SelectedBoneIndex] = SelectedBone.RefLocalMatrix;
				if (SkeletalPreviewComponent)
				{
					SkeletalPreviewComponent->SetBoneLocalTransform(Document->SelectedBoneIndex, SelectedBone.RefLocalMatrix);
				}
				Document->bPoseDirty = true;
				RefreshSkeletalPreviewMesh(Document->SelectedBoneIndex);
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Reset All"))
		{
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Document->SkeletalMesh->MeshData.RefBones.Num()); ++BoneIndex)
			{
				Document->EditableLocalPose[BoneIndex] = MatrixToTransform(Document->SkeletalMesh->MeshData.RefBones[BoneIndex].RefLocalMatrix);
				Document->LocalPoseMatrices[BoneIndex] = Document->SkeletalMesh->MeshData.RefBones[BoneIndex].RefLocalMatrix;
			}
			if (SkeletalPreviewComponent)
			{
				SkeletalPreviewComponent->ResetToReferencePose();
			}
			Document->bPoseDirty = true;
			RefreshSkeletalPreviewMesh();
			Document->bModified = false;
		}
		if (ImGui::Button("Export"))
		{
			ExportReboundFbx();
		}

		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Preview Viewport");
		ImGui::Separator();

		ImVec2 PreviewSize = ImGui::GetContentRegionAvail();
		PreviewSize.x = std::max(0.0f, PreviewSize.x);
		PreviewSize.y = std::max(0.0f, PreviewSize.y);
		const uint32 PreviewWidth = static_cast<uint32>(std::floor(PreviewSize.x));
		const uint32 PreviewHeight = static_cast<uint32>(std::floor(PreviewSize.y));
		if (SkeletalPreviewViewport)
		{
			SkeletalPreviewViewport->SetOutputSize(PreviewWidth, PreviewHeight);
			if (Document->bFitCameraPending)
			{
				FitCameraToMesh();
			}
		}
		if (SkeletalPreviewViewport && SkeletalPreviewViewport->GetColorTarget() && PreviewWidth > 0 && PreviewHeight > 0)
		{
			const ImVec2 PreviewMin = ImGui::GetCursorScreenPos();
			SkeletalPreviewScreenRect = { PreviewMin.x, PreviewMin.y, PreviewSize.x, PreviewSize.y };
			ImGui::Image((ImTextureID)SkeletalPreviewViewport->GetColorTarget()->GetSRV(), PreviewSize);
		}
		else
		{
			ImGui::Dummy(PreviewSize);
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::BuildBoneHierarchy(FSkeletalMeshDocument& Document)
{
	Document.BoneChildren.Reset();
	Document.RootBones.Reset();
	Document.BoneChildren.SetNum(Document.SkeletalMesh->MeshData.RefBones.Num());
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Document.SkeletalMesh->MeshData.RefBones.Num()); ++BoneIndex)
	{
		const int32 ParentIndex = Document.SkeletalMesh->MeshData.RefBones[BoneIndex].ParentIndex;
		if (ParentIndex < 0)
		{
			Document.RootBones.Add(BoneIndex);
			continue;
		}
		if (Document.SkeletalMesh->MeshData.RefBones.IsValidIndex(ParentIndex))
		{
			Document.BoneChildren[ParentIndex].Add(BoneIndex);
		}
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();

	if (!Document->SkeletalMesh->MeshData.RefBones.IsValidIndex(BoneIndex))
	{
		return;
	}
	const FSkeletalMeshRefBone& Bone = Document->SkeletalMesh->MeshData.RefBones[BoneIndex];
	const bool bHasChildren = !Document->BoneChildren[BoneIndex].IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (Document->SelectedBoneIndex == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (Bone.ParentIndex < 0)
	{
		Flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
		Flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(BoneIndex);
	const FString BoneName = Bone.Name.ToString();
	const bool bOpen = ImGui::TreeNodeEx("##BoneNode", Flags, "%s", BoneName.c_str());
	if (ImGui::IsItemClicked())
	{
		Document->SelectedBoneIndex = BoneIndex;
	}

	if (bOpen && bHasChildren)
	{
		for (int32 ChildIndex : Document->BoneChildren[BoneIndex])
		{
			DrawBoneNode(ChildIndex);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

bool FSkeletalMeshViewerPanel::InitializeSkeletalPreview(UWorld* InSkeletalMeshWorld)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();

	if (!InSkeletalMeshWorld || !InSkeletalMeshWorld->GetMainCamera())
	{
		return false;
	}

	SkeletalPreviewWorld = InSkeletalMeshWorld;
	CreateDefaultSkeletalPreviewScene();
	SkeletalPreviewViewport = MakeUnique<FViewport>();
	TUniquePtr<FEditorViewportClient> Client = MakeUnique<FEditorViewportClient>();

	SkeletalPreviewClient = Client.get();
	SkeletalPreviewClient->SetWorld(SkeletalPreviewWorld);

	SkeletalPreviewClient->InitializeCamera(*SkeletalPreviewWorld->GetMainCamera()->GetCameraComponent(), 0, FVector2(1.0f, 1.0f));
	FEditorCameraState Camera = SkeletalPreviewClient->GetCamera();
	Camera.Location = FVector(-5.0f, 0.0f, 2.5f);
	const FVector Target(0.0f, 0.0f, 1.5f);
	Camera.Rotation = FRotator::FindLookAtRotation(Camera.Location, Target).Quaternion();
	SkeletalPreviewClient->SetCamera(Camera, FVector2(1.0f, 1.0f));

	SkeletalPreviewViewport->SetEditorClient(std::move(Client));
	if (Document && Document->SkeletalMesh)
		SetSkeletalMesh(Document->SkeletalMesh);
	return true;
}

void FSkeletalMeshViewerPanel::ShutdownSkeletalPreview()
{
	if (SkeletalPreviewComponent)
		SkeletalPreviewComponent->SetSkeletalMesh(nullptr);
	SkeletalPreviewComponent = nullptr;
	SkeletalPreviewActor = nullptr;
	SkeletalPreviewFloor = nullptr;
	SkeletalPreviewDirectionalLight = nullptr;
	SkeletalPreviewAmbientLight = nullptr;
	SkeletalPreviewClient = nullptr;
	SkeletalPreviewViewport.reset();
	SkeletalPreviewWorld = nullptr;

	if (BoneGizmo)
	{
		BoneGizmo->SetTarget(nullptr);
	}
	if (BoneGizmoTarget)
	{
		BoneGizmoTarget->SetViewer(nullptr);
	}
	BoneGizmoTarget.reset();
	BoneGizmo.reset();
}


bool FSkeletalMeshViewerPanel::CanRenderSkeletalPreview() const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();
	return IsOpen()
		&& SkeletalPreviewWorld && SkeletalPreviewViewport && SkeletalPreviewClient
		&& SkeletalPreviewViewport->IsVisible()
		&& SkeletalPreviewClient->HasCamera()
		&& Document;
}

void FSkeletalMeshViewerPanel::CreateDefaultSkeletalPreviewScene()
{
	if (!SkeletalPreviewWorld)
	{
		return;
	}

	SkeletalPreviewActor = SkeletalPreviewWorld->SpawnActor<AActor>(FName("SkeletalPreviewActor"));
	SkeletalPreviewComponent = 
		Cast<USkeletalMeshComponent>(SkeletalPreviewActor->AddComponent(USkeletalMeshComponent::StaticClass(), FName("SkeletalPreviewComponent")));
	
	UStaticMesh* PlaneMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Plane");
	if (PlaneMesh)
	{
		SkeletalPreviewFloor = SkeletalPreviewWorld->SpawnActor<AStaticMeshActor>(FName("SkeletalPreviewFloor"));
		SkeletalPreviewFloor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		SkeletalPreviewFloor->GetStaticMeshComponent()->SetCastShadow(false);
		SkeletalPreviewFloor->GetStaticMeshComponent()->SetReceiveShadow(true);
		SkeletalPreviewFloor->GetRootComponent()->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		SkeletalPreviewFloor->GetRootComponent()->SetRelativeScale3D(FVector(40.0f, 40.0f, 1.0f));
	}

	SkeletalPreviewDirectionalLight = SkeletalPreviewWorld->SpawnActor<ADirectionalLightActor>(FName("SkeletalPreviewDirectionalLight"));
	SkeletalPreviewDirectionalLight->GetRootComponent()->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	SkeletalPreviewDirectionalLight->GetRootComponent()->SetRelativeRotation(FRotator(45.0f, -45.0f, 0.0f));
	SkeletalPreviewDirectionalLight->GetLightComponent()->SetIntensity(1.5f);
	SkeletalPreviewDirectionalLight->GetLightComponent()->SetCastShadow(false);
	for (UActorComponent* Component : SkeletalPreviewDirectionalLight->GetComponents())
	{
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component)) Billboard->SetVisible(false);
	}

	SkeletalPreviewAmbientLight = SkeletalPreviewWorld->SpawnActor<AAmbientLightActor>(FName("SkeletalPreviewAmbientLight"));
	SkeletalPreviewAmbientLight->GetLightComponent()->SetIntensity(0.2f);
	for (UActorComponent* Component : SkeletalPreviewAmbientLight->GetComponents())
	{
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component)) Billboard->SetVisible(false);
	}
}

bool FSkeletalMeshViewerPanel::IsSkeletalPreviewHovered() const
{
	POINT Mouse{};
	if (!::GetCursorPos(&Mouse) || !SkeletalPreviewScreenRect.IsValid())
	{
		return false;
	}

	return Mouse.x >= SkeletalPreviewScreenRect.X 
		&& Mouse.x < SkeletalPreviewScreenRect.X + SkeletalPreviewScreenRect.Width 
		&& Mouse.y >= SkeletalPreviewScreenRect.Y 
		&& Mouse.y < SkeletalPreviewScreenRect.Y + SkeletalPreviewScreenRect.Height;
}

bool FSkeletalMeshViewerPanel::IsSkeletalPreviewCaptured() const
{
	return bSkeletalPreviewCaptured;
}

void FSkeletalMeshViewerPanel::UpdateSkeletalPreviewInput()
{
	if (!FInputSystem::IsMouseDown(EMouseButton::Right))
	{
		bSkeletalPreviewCaptured = false;
	}

	if (!bSkeletalPreviewCaptured
		&& IsSkeletalPreviewHovered()
		&& FInputSystem::IsMouseDown(EMouseButton::Right))
	{
		bSkeletalPreviewCaptured = true;
	}

}

void FSkeletalMeshViewerPanel::RouteSkeletalPreviewInput(FViewportInput Input)
{
	if (!SkeletalPreviewViewport)
	{
		return;
	}
	
	POINT Mouse{};
	::GetCursorPos(&Mouse);

	Input.bCaptured = bSkeletalPreviewCaptured;
	Input.bPointerOver = IsSkeletalPreviewHovered();
	Input.MousePosition = 
		{ static_cast<float>(Mouse.x) - SkeletalPreviewScreenRect.X, 
		static_cast<float>(Mouse.y) - SkeletalPreviewScreenRect.Y };
	SkeletalPreviewViewport->UpdateInput(Input);
}

void FSkeletalMeshViewerPanel::ReleaseSkeletalPreviewInput()
{
	bSkeletalPreviewCaptured = false;

	if (SkeletalPreviewViewport)
	{
		SkeletalPreviewViewport->UpdateInput({});
	}

	if (BoneGizmo && BoneGizmo->IsUsing())
	{
		BoneGizmo->EndDrag();
	}
}

const TArray<FMatrix>& FSkeletalMeshViewerPanel::GetGlobalPose() const
{
	static const TArray<FMatrix> EmptyPose{};
	return SkeletalPreviewComponent
		? SkeletalPreviewComponent->GetComponentSpaceTransforms()
		: EmptyPose;
}

void FSkeletalMeshViewerPanel::DrawBoneDebug(FLineBatcher& LineBatcher) const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();
	if (!Document)
	{
		return;
	}

	const TArray<FMatrix>& GlobalPose = GetGlobalPose();
	if (!Document->SkeletalMesh || GlobalPose.Num() != Document->SkeletalMesh->MeshData.RefBones.Num())
	{
		return;
	}

	constexpr float BoneRadius = 0.03f;
	constexpr int32 SphereSegments = 16;

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Document->SkeletalMesh->MeshData.RefBones.Num()); ++BoneIndex)
	{
		const FVector BonePosition = GetBoneGlobalPosition(BoneIndex);
		if (!GlobalPose.IsValidIndex(BoneIndex) || !IsFiniteVector(BonePosition))
		{
			continue;
		}

		const bool bJointSelected = BoneIndex == Document->SelectedBoneIndex;
		const FVector4 JointColor = bJointSelected
			? FVector4(1.0f, 0.8f, 0.1f, 1.0f)
			: FVector4(0.5f, 0.8f, 1.0f, 1.0f);
		AddWireSphere(LineBatcher, BonePosition, BoneRadius, SphereSegments, JointColor);

		const int32 ParentIndex = Document->SkeletalMesh->MeshData.RefBones[BoneIndex].ParentIndex;
		const FVector ParentPosition = GetBoneGlobalPosition(ParentIndex);
		if (!GlobalPose.IsValidIndex(ParentIndex) || !IsFiniteVector(ParentPosition))
		{
			continue;
		}

		const bool bBoneSelected = ParentIndex == Document->SelectedBoneIndex;
		const FVector4 BoneColor = bBoneSelected
			? FVector4(1.0f, 0.8f, 0.1f, 1.0f)
			: FVector4(0.5f, 0.8f, 1.0f, 1.0f);

		const FVector BoneVector = BonePosition - ParentPosition;
		const float BoneLength = BoneVector.Size();
		if (BoneLength <= SMALL_NUMBER)
			continue;
		const FVector BoneDirection = BoneVector / BoneLength;

		const FVector ReferenceAxis = std::abs(BoneDirection.Z) < 0.99f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
		const FVector BasisAxisA = FVector::Cross(ReferenceAxis, BoneDirection).GetSafeNormal();
		const FVector BasisAxisB = FVector::Cross(BoneDirection, BasisAxisA).GetSafeNormal();

		const FVector Corners[4] =
		{
			ParentPosition + BasisAxisA * BoneRadius,
			ParentPosition + BasisAxisB * BoneRadius,
			ParentPosition - BasisAxisA * BoneRadius,
			ParentPosition - BasisAxisB * BoneRadius
		};

		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			LineBatcher.AddLine(Corners[CornerIndex], BonePosition, BoneColor);
		}
	}
}


FVector FSkeletalMeshViewerPanel::GetBoneGlobalPosition(int32 BoneIndex) const
{
	const TArray<FMatrix>& GlobalPose = GetGlobalPose();
	if (!GlobalPose.IsValidIndex(BoneIndex))
	{
		return FVector::ZeroVector;
	}

	const FMatrix& GlobalMatrix = GlobalPose[BoneIndex];
	float GlobalTranslationX = GlobalMatrix[3][0];
	float GlobalTranslationY = GlobalMatrix[3][1];
	float GlobalTranslationZ = GlobalMatrix[3][2];

	return FVector{ GlobalTranslationX, GlobalTranslationY, GlobalTranslationZ };
}

bool FSkeletalMeshViewerPanel::IsBoneValid(int32 BoneIndex) const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();

	const TArray<FMatrix>& GlobalPose = GetGlobalPose();
	return Document->SkeletalMesh && Document->SkeletalMesh->MeshData.RefBones.IsValidIndex(BoneIndex)
		&& Document->EditableLocalPose.IsValidIndex(BoneIndex)
		&& GlobalPose.IsValidIndex(BoneIndex);
}

int32 FSkeletalMeshViewerPanel::GetBoneParentIndex(int32 BoneIndex) const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();

	return Document->SkeletalMesh && Document->SkeletalMesh->MeshData.RefBones.IsValidIndex(BoneIndex)
		? Document->SkeletalMesh->MeshData.RefBones[BoneIndex].ParentIndex
		: -1;
}

FTransform FSkeletalMeshViewerPanel::GetBoneLocalTransform(int32 BoneIndex) const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();

	return Document->EditableLocalPose.IsValidIndex(BoneIndex)
		? Document->EditableLocalPose[BoneIndex]
		: FTransform::Identity;
}

FMatrix FSkeletalMeshViewerPanel::GetBoneGlobalMatrix(int32 BoneIndex) const
{
	const TArray<FMatrix>& GlobalPose = GetGlobalPose();
	return GlobalPose.IsValidIndex(BoneIndex) 
		? GlobalPose[BoneIndex] 
		: FMatrix::Identity;
}

void FSkeletalMeshViewerPanel::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Transform)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();

	if (!SkeletalPreviewComponent || !Document->EditableLocalPose.IsValidIndex(BoneIndex)
		|| !Document->LocalPoseMatrices.IsValidIndex(BoneIndex) || !IsValidEditableTransform(Transform))
	{
		return;
	}

	const FTransform& Previous = Document->EditableLocalPose[BoneIndex];
	const bool bOnlyTranslation = Transform.Rotation.Pitch == Previous.Rotation.Pitch
		&& Transform.Rotation.Yaw == Previous.Rotation.Yaw
		&& Transform.Rotation.Roll == Previous.Rotation.Roll
		&& Transform.Scale == Previous.Scale;
	if (bOnlyTranslation && Transform.Location == Previous.Location)
	{
		return;
	}

	if (bOnlyTranslation)
	{
		Document->LocalPoseMatrices[BoneIndex].SetOrigin(Transform.Location);
	}
	else
	{
		Document->LocalPoseMatrices[BoneIndex] = Transform.GetLocalMatrix();
	}
	Document->EditableLocalPose[BoneIndex] = Transform;
	SkeletalPreviewComponent->SetBoneLocalTransform(BoneIndex, Document->LocalPoseMatrices[BoneIndex]);
	Document->bPoseDirty = true;
	Document->bModified = true;
}

void FSkeletalMeshViewerPanel::UpdateBoneGizmoInput(bool bInputEnabled)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();
	if (!Document)
	{
		return;
	}

	if (!BoneGizmo)
	{
		return;
	}
	if (!bInputEnabled || !CanRenderSkeletalPreview())
	{
		BoneGizmo->EndDrag();
		return;
	}

	const bool bCanInteract = IsSkeletalPreviewHovered() || BoneGizmo->IsUsing();
	if (!bCanInteract)
	{
		BoneGizmo->EndDrag();
		return;
	}

	if (!BoneGizmo->IsUsing() && FInputSystem::IsKeyPressed(EKeyCode::Space))
	{
		const int NextMode = (static_cast<int>(BoneGizmo->GetMode()) + 1) % 3;
		BoneGizmo->SetMode(static_cast<EGizmoMode>(NextMode));
	}

	POINT Mouse{};
	if (!SkeletalPreviewScreenRect.IsValid() || !::GetCursorPos(&Mouse))
	{
		BoneGizmo->EndDrag();
		return;
	}

	const FVector2 PreviewMousePosition(
		static_cast<float>(Mouse.x) - SkeletalPreviewScreenRect.X,
		static_cast<float>(Mouse.y) - SkeletalPreviewScreenRect.Y);

	const FSceneView View = SkeletalPreviewViewport->CreateSceneView();
	const FRect& ViewRect = View.ViewRect;
	if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
	{
		return;
	}

	const FRay MouseRay = View.Deproject(PreviewMousePosition);
	const bool bLeftMousePressed = FInputSystem::IsMousePressed(EMouseButton::Left);
	BoneGizmo->Update(MouseRay, PreviewMousePosition, View.ViewProjection, static_cast<int>(ViewRect.Width), static_cast<int>(ViewRect.Height),
		FInputSystem::IsMouseDown(EMouseButton::Left), View.GetCameraLocation(), View.IsOrthographic());

	const bool bGizmoBlocksPick = BoneGizmo->IsUsing() || BoneGizmo->GetHoveredAxis() >= 0;
	if (bLeftMousePressed && !bGizmoBlocksPick)
	{
		const int32 PickedBoneIndex = PickBone(PreviewMousePosition, View.ViewProjection, 
			static_cast<int32>(ViewRect.Width), static_cast<int32>(ViewRect.Height));
		Document->SelectedBoneIndex = PickedBoneIndex;
	}
}

int32 FSkeletalMeshViewerPanel::PickBone(const FVector2& MousePosition, const FMatrix& ViewProjection, int32 ScreenWidth, int32 ScreenHeight) const
{
	const FSkeletalMeshDocument* Document = GetActiveDocument();
	if (!Document->SkeletalMesh)
	{
		return -1;
	}
	constexpr float PickBoneRadius = 7.0f;
	constexpr float PickjointRadius = 15.0f;
	int32 PickedBoneIndex = -1;
	float NearestDistance = PickjointRadius;

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Document->SkeletalMesh->MeshData.RefBones.Num()); ++BoneIndex)
	{
		const FVector2 JointScreenPosition = WorldToScreen(GetBoneGlobalPosition(BoneIndex), ViewProjection, ScreenWidth, ScreenHeight);
		const FVector2 Difference = MousePosition - JointScreenPosition;
		const float Distance = sqrtf(Difference.X * Difference.X + Difference.Y * Difference.Y);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			PickedBoneIndex = BoneIndex;
		}
	}
	if (PickedBoneIndex >= 0)
	{
		return PickedBoneIndex;
	}

	NearestDistance = PickBoneRadius;

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Document->SkeletalMesh->MeshData.RefBones.Num()); ++BoneIndex)
	{
		const int32 ParentIndex = Document->SkeletalMesh->MeshData.RefBones[BoneIndex].ParentIndex;
		if (ParentIndex < 0)
		{
			continue;
		}

		const FVector2 ParentScreenPosition = WorldToScreen(GetBoneGlobalPosition(ParentIndex), ViewProjection, ScreenWidth, ScreenHeight);
		const FVector2 BoneScreenPosition = WorldToScreen(GetBoneGlobalPosition(BoneIndex), ViewProjection, ScreenWidth, ScreenHeight);

		const float Distance = DistanceToSegment(MousePosition, ParentScreenPosition, BoneScreenPosition);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			PickedBoneIndex = BoneIndex;
		}
	}
	return PickedBoneIndex;
}
void FSkeletalMeshViewerPanel::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();
	
	if (!Document)
	{
		Documents.Add(FSkeletalMeshDocument{});
		ActiveDocumentIndex = Documents.Num() - 1;
		Document = GetActiveDocument();
	}

	if (BoneGizmo)
	{
		BoneGizmo->EndDrag();
	}

	Document->SkeletalMesh = InSkeletalMesh;
	if (SkeletalPreviewComponent)
	{
		if (Document->SkeletalMesh 
			&& SkeletalPreviewComponent->GetSkeletalMesh() == Document->SkeletalMesh)
		{
			SkeletalPreviewComponent->ResetToReferencePose();
		}
		else
		{
			SkeletalPreviewComponent->SetSkeletalMesh(Document->SkeletalMesh);
		}
	}
	Document->EditableLocalPose.Reset();
	Document->LocalPoseMatrices.Reset();
	Document->BoneChildren.Reset();
	Document->RootBones.Reset();

	Document->SelectedBoneIndex = INDEX_NONE;
	Document->bPoseDirty = false;
	Document->bFitCameraPending = false;

	if (!Document->SkeletalMesh)
	{
		return;
	}

	for (const FSkeletalMeshRefBone& Bone : Document->SkeletalMesh->MeshData.RefBones)
	{
		Document->EditableLocalPose.Add(MatrixToTransform(Bone.RefLocalMatrix));
		Document->LocalPoseMatrices.Add(Bone.RefLocalMatrix);
	}

	BuildBoneHierarchy(*Document);
	if (!Document->RootBones.IsEmpty())
	{
		Document->SelectedBoneIndex = Document->RootBones[0];
	}
	Document->bPoseDirty = true;
	Document->bModified = true;
	Document->bFitCameraPending = true;
	RefreshSkeletalPreviewMesh(INDEX_NONE);
	FitCameraToMesh();
}

void FSkeletalMeshViewerPanel::FitCameraToMesh()
{
	FSkeletalMeshDocument* Document = GetActiveDocument();
	
	if (!Document->SkeletalMesh || !SkeletalPreviewClient || !SkeletalPreviewViewport || !SkeletalPreviewViewport->IsVisible())
	{
		return;
	}

	const FBox& Bounds = Document->SkeletalMesh->GetMeshData().ReferenceBounds;
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max((Bounds.Max - Bounds.Min).Size() * 0.5f, 0.1f);
	const FVector2 Size = SkeletalPreviewViewport->GetOutputSize();
	FEditorCameraState Camera = SkeletalPreviewClient->GetCamera();

	const float Distance = Radius * 3.0f * std::max(1.0f, Size.Y / Size.X);
	Camera.Location = Center + FVector(-1.0f, 0.0f, 0.3f).GetSafeNormal() * Distance;
	Camera.Rotation = FRotator::FindLookAtRotation(Camera.Location, Center).Quaternion();

	SkeletalPreviewClient->SetCamera(Camera, Size);
	Document->CameraState = Camera;
	Document->bHasCameraState = true;
	Document->bFitCameraPending = false;
	if (SkeletalPreviewFloor)
	{
		SkeletalPreviewFloor->GetRootComponent()->SetRelativeLocation(FVector(Center.X, Center.Y, Bounds.Min.Z - Radius * 0.02f));
	}
}

void FSkeletalMeshViewerPanel::RefreshSkeletalPreviewMesh(int32 BoneIndex)
{
	FSkeletalMeshDocument* Document = GetActiveDocument();

	if (!SkeletalPreviewComponent || !Document->bPoseDirty
		|| (BoneIndex != INDEX_NONE && !IsBoneValid(BoneIndex)))
	{
		return;
	}

	SkeletalPreviewComponent->RefreshMesh(BoneIndex);
	Document->bPoseDirty = false;
}

void FSkeletalMeshViewerPanel::ExportReboundFbx()
{
	FSkeletalMeshDocument* Document = GetActiveDocument();

	if (!Document->SkeletalMesh
		|| Document->LocalPoseMatrices.Num() != Document->SkeletalMesh->MeshData.RefBones.Num())
	{
		return;
	}

	FString SourceKey = Document->SkeletalMesh->GetPath();
	const size_t GroupPosition = SourceKey.rfind("#Group");
	if (GroupPosition == FString::npos)
	{
		HTR_LOG(Error, "[FBX Export] Invalid asset key: {}", SourceKey);
		return;
	}
	const FString GroupSuffix = SourceKey.substr(GroupPosition);

	SourceKey.resize(GroupPosition);
	const FString SourcePath = (std::filesystem::path("Assets") / SourceKey).generic_string();
	const FString DestinationPath = FEditorFileUtils::OpenFbxSaveDialog(SourcePath);
	if (DestinationPath.empty())
	{
		return;
	}

	std::error_code Error;

	if (!FbxPoseExporter::ExportReboundFbx(SourcePath, DestinationPath, Document->SkeletalMesh->MeshData, Document->LocalPoseMatrices))
	{
		HTR_LOG(Error, "[FBX Export] Failed: {}", DestinationPath);
		return;
	}

	HTR_LOG(Info, "[FBX Export] Saved: {}", DestinationPath);
	Document->bModified = false;

	const std::filesystem::path AssetRootPath = std::filesystem::absolute("Assets").lexically_normal();
	const std::filesystem::path ExportedAbsolutePath = std::filesystem::absolute(DestinationPath).lexically_normal();

	std::error_code RelativePathError;
	const std::filesystem::path ExportedRelativePath = std::filesystem::relative(ExportedAbsolutePath, AssetRootPath, RelativePathError);
	if (RelativePathError || ExportedRelativePath.empty() || *ExportedRelativePath.begin() == "..")
	{
		HTR_LOG(Warning, "[FBX Export] Saved outside Assets, so it cannot be opened automatically: {}", DestinationPath);
		return;
	}

	const FString ExportedKey = ExportedRelativePath.generic_string();
	UAssetManager::Get().LoadAsset(ExportedKey, ExportedAbsolutePath.generic_string());
	USkeletalMesh* ExportedMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(ExportedKey + GroupSuffix);
	if (!ExportedMesh)
	{
		HTR_LOG(Error, "[FBX Export] Saved FBX could not be imported: {}{}", ExportedKey, GroupSuffix);
		return;
	}

	SetSkeletalMesh(ExportedMesh);
	HTR_LOG(Info, "[FBX Export] Opened exported mesh: {}{}", ExportedKey, GroupSuffix);
}

FSkeletalMeshDocument* FSkeletalMeshViewerPanel::GetActiveDocument()
{
	return Documents.IsValidIndex(ActiveDocumentIndex)
		? &Documents[ActiveDocumentIndex]
		: nullptr;
}

const FSkeletalMeshDocument* FSkeletalMeshViewerPanel::GetActiveDocument() const
{
	return Documents.IsValidIndex(ActiveDocumentIndex)
		? &Documents[ActiveDocumentIndex]
		: nullptr;
}

int32 FSkeletalMeshViewerPanel::GetSelectedBoneIndex() const
{
	 const FSkeletalMeshDocument* Document = GetActiveDocument();

	return Document
		? Document->SelectedBoneIndex
		: INDEX_NONE;
}


void FSkeletalMeshViewerPanel::InitializeDocument(FSkeletalMeshDocument& Document, USkeletalMesh* SkeletalMesh)
{
	Document.SkeletalMesh = SkeletalMesh;
	Document.EditableLocalPose.Reset();
	Document.LocalPoseMatrices.Reset();
	Document.BoneChildren.Reset();
	Document.RootBones.Reset();
	Document.SelectedBoneIndex = INDEX_NONE;

	for (const FSkeletalMeshRefBone& Bone : SkeletalMesh->MeshData.RefBones)
	{
		Document.EditableLocalPose.Add(MatrixToTransform(Bone.RefLocalMatrix));
		Document.LocalPoseMatrices.Add(Bone.RefLocalMatrix);
	}

	BuildBoneHierarchy(Document);

	if (!Document.RootBones.IsEmpty())
	{
		Document.SelectedBoneIndex = Document.RootBones[0];
	}

	Document.bPoseDirty = true;
	Document.bModified = false;
	Document.bFitCameraPending = true;
}

void FSkeletalMeshViewerPanel::OpenSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	if (!SkeletalMesh)
	{
		return;
	}

	// 이미 열린 메시인지 확인
	for (int32 Index = 0; Index < Documents.Num(); ++Index)
	{
		if (Documents[Index].SkeletalMesh == SkeletalMesh)
		{
			ActivateDocument(Index);
			return;
		}
	}

	// 새로운 문서 생성
	FSkeletalMeshDocument& NewDocument =
		Documents.AddDefaulted_GetRef();

	InitializeDocument(NewDocument, SkeletalMesh);
	ActivateDocument(Documents.Num() - 1);
}

void FSkeletalMeshViewerPanel::ActivateDocument(int32 DocumentIndex)
{
	if (!Documents.IsValidIndex(DocumentIndex))
	{
		return;
	}

	if (BoneGizmo)
	{
		BoneGizmo->EndDrag();
	}

	if (ActiveDocumentIndex != DocumentIndex)
	{
		FSkeletalMeshDocument* PreviousDocument = GetActiveDocument();
		if (PreviousDocument && SkeletalPreviewClient)
		{
			PreviousDocument->CameraState = SkeletalPreviewClient->GetCamera();
			PreviousDocument->bHasCameraState = true;
		}
	}

	ActiveDocumentIndex = DocumentIndex;
	FSkeletalMeshDocument& Document = Documents[DocumentIndex];

	SkeletalPreviewComponent->SetSkeletalMesh(Document.SkeletalMesh);

	for (int32 BoneIndex = 0; BoneIndex < Document.LocalPoseMatrices.Num(); ++BoneIndex)
	{
		SkeletalPreviewComponent->SetBoneLocalTransform(BoneIndex, Document.LocalPoseMatrices[BoneIndex]);
	}

	SkeletalPreviewComponent->RefreshMesh(INDEX_NONE);

	if (Document.bHasCameraState)
	{
		const FVector2 Size = SkeletalPreviewViewport->GetOutputSize();
		SkeletalPreviewClient->SetCamera(Document.CameraState, Size);
	}
	else if (Document.bFitCameraPending)
	{
		FitCameraToMesh();
	}
}

void FSkeletalMeshViewerPanel::CloseDocument(int32 Index)
{
	if (!Documents.IsValidIndex(Index))
	{
		return;
	}

	const bool bWasActive = ActiveDocumentIndex == Index;
	Documents.RemoveAt(Index);

	if (Documents.IsEmpty())
	{
		ActiveDocumentIndex = INDEX_NONE;

		if (SkeletalPreviewComponent)
		{
			SkeletalPreviewComponent->SetSkeletalMesh(nullptr);
		}
		return;
	}

	if (Index < ActiveDocumentIndex)
	{
		--ActiveDocumentIndex;
	}

	if (bWasActive)
	{
		ActiveDocumentIndex = INDEX_NONE;
		const int32 NewIndex = std::min(Index, Documents.Num() - 1);
		ActivateDocument(NewIndex);
	}
	else if (Index < ActiveDocumentIndex)
	{
		--ActiveDocumentIndex;
	}
}