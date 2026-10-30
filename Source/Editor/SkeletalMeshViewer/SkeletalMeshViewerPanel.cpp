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

bool FSkeletalMeshViewerPanel::Init()
{
	CreateMockSkeleton();
	return true;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bool bOpen = true;
	const bool bVisible = ImGui::Begin(GetPanelName(), &bOpen);

	if (!bOpen)
	{
		SetOpen(false);
	}

	if (!bVisible)
	{
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
			if (ImGui::BeginTabItem("Mock Humanoid"))
			{
				if (MockRootBones.IsEmpty())
				{
					ImGui::TextDisabled("No root bone.");
				}
				else
				{
					for (int32 RootBoneIndex : MockRootBones)
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
		if (!MockBones.IsValidIndex(SelectedBoneIndex))
		{
			ImGui::TextDisabled("No bone is selected.");
		}
		else
		{
			const FMockBone& SelectedBone = MockBones[SelectedBoneIndex];
			const FTransform& LocalTransform = EditableLocalPose[SelectedBoneIndex];

			ImGui::Text("Name: %s", SelectedBone.Name.c_str());
			ImGui::Text("Index: %d", SelectedBoneIndex);
			if (MockBones.IsValidIndex(SelectedBone.ParentIndex))
			{
				ImGui::Text("Parent: %s", MockBones[SelectedBone.ParentIndex].Name.c_str());
			}
			else
			{
				ImGui::TextUnformatted("Parent: None");
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Local Location");
			ImGui::Text(
				"X %.2f  Y %.2f  Z %.2f",
				LocalTransform.Location.X,
				LocalTransform.Location.Y,
				LocalTransform.Location.Z);
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
		}
		if (SkeletalPreviewViewport && SkeletalPreviewViewport->GetColorTarget() && PreviewWidth > 0 && PreviewHeight > 0)
		{
			ImGui::Image((ImTextureID)SkeletalPreviewViewport->GetColorTarget()->GetSRV(), PreviewSize);
		}
		else
		{
			ImGui::Dummy(PreviewSize);
		}

		ImGui::EndTable();
	}

	ImGui::Separator();
	ImGui::TextDisabled("Display Options");
	ImGui::SameLine();
	ImGui::TextDisabled("Skeletal Mesh / Bones / Grid");

	ImGui::End();
}

void FSkeletalMeshViewerPanel::CreateMockSkeleton()
{
	MockBones.Reset();
	EditableLocalPose.Reset();
	SelectedBoneIndex = -1;
	AddMockBone("Root", -1, FVector(0.0f, 0.0f, 0.0f));
	AddMockBone("Pelvis", 0, FVector(0.0f, 0.0f, 1.0f));
	AddMockBone("Spine", 1, FVector(0.0f, 0.0f, 0.7f));
	AddMockBone("Chest", 2, FVector(0.0f, 0.0f, 0.7f));
	AddMockBone("Neck", 3, FVector(0.0f, 0.0f, 0.45f));
	AddMockBone("Head", 4, FVector(0.0f, 0.0f, 0.35f));
	AddMockBone("LeftUpperArm", 3, FVector(0.0f, -0.55f, 0.3f));
	AddMockBone("LeftLowerArm", 6, FVector(0.0f, -0.7f, 0.0f));
	AddMockBone("LeftHand", 7, FVector(0.0f, -0.6f, 0.0f));
	AddMockBone("RightUpperArm", 3, FVector(0.0f, 0.55f, 0.3f));
	AddMockBone("RightLowerArm", 9, FVector(0.0f, 0.7f, 0.0f));
	AddMockBone("RightHand", 10, FVector(0.0f, 0.6f, 0.0f));
	AddMockBone("LeftUpperLeg", 1, FVector(0.0f, -0.25f, -0.75f));
	AddMockBone("LeftLowerLeg", 12, FVector(0.0f, 0.0f, -0.85f));
	AddMockBone("LeftFoot", 13, FVector(0.3f, 0.0f, -0.75f));
	AddMockBone("RightUpperLeg", 1, FVector(0.0f, 0.25f, -0.75f));
	AddMockBone("RightLowerLeg", 15, FVector(0.0f, 0.0f, -0.85f));
	AddMockBone("RightFoot", 16, FVector(0.3f, 0.0f, -0.75f));

	BuildMockBoneHierarchy();
	if (!MockRootBones.IsEmpty())
	{
		SelectedBoneIndex = MockRootBones[0];
	}
}

void FSkeletalMeshViewerPanel::AddMockBone(const char* Name, int32 ParentIndex, const FVector& LocalLocation)
{
	FMockBone Bone;
	Bone.Name = Name;
	Bone.ParentIndex = ParentIndex;

	FTransform LocalTransform(FRotator(0.0f, 0.0f, 0.0f), LocalLocation);
	Bone.ReferenceLocalTransform = LocalTransform;
	EditableLocalPose.Add(LocalTransform);

	MockBones.Add(Bone);
}

void FSkeletalMeshViewerPanel::BuildMockBoneHierarchy()
{
	MockBoneChildren.Reset();
	MockRootBones.Reset();
	MockBoneChildren.SetNum(MockBones.Num());
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MockBones.Num()); ++BoneIndex)
	{
		const int32 ParentIndex = MockBones[BoneIndex].ParentIndex;
		if (ParentIndex < 0)
		{
			MockRootBones.Add(BoneIndex);
			continue;
		}
		if (MockBones.IsValidIndex(ParentIndex))
		{
			MockBoneChildren[ParentIndex].Add(BoneIndex);
		}
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex)
{
	if (!MockBones.IsValidIndex(BoneIndex))
		return;
	const FMockBone& Bone = MockBones[BoneIndex];
	const bool bHasChildren = !MockBoneChildren[BoneIndex].IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (SelectedBoneIndex == BoneIndex)
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
	const bool bOpen = ImGui::TreeNodeEx("##BoneNode", Flags, "%s", Bone.Name.c_str());
	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = BoneIndex;
	}

	if (bOpen && bHasChildren)
	{
		for (int32 ChildIndex : MockBoneChildren[BoneIndex])
		{
			DrawBoneNode(ChildIndex);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

bool FSkeletalMeshViewerPanel::InitializeSkeletalPreview(UWorld* InSkeletalMeshWorld)
{
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
	return true;
}

void FSkeletalMeshViewerPanel::ShutdownSkeletalPreview()
{
	SkeletalPreviewFloor = nullptr;
	SkeletalPreviewDirectionalLight = nullptr;
	SkeletalPreviewAmbientLight = nullptr;
	SkeletalPreviewClient = nullptr;
	SkeletalPreviewViewport.reset();
	SkeletalPreviewWorld = nullptr;
}


bool FSkeletalMeshViewerPanel::CanRenderSkeletalPreview() const
{
	return IsOpen() 
		&& SkeletalPreviewWorld && SkeletalPreviewViewport && SkeletalPreviewClient 
		&& SkeletalPreviewViewport->IsVisible() 
		&& SkeletalPreviewClient->HasCamera();
}

void FSkeletalMeshViewerPanel::CreateDefaultSkeletalPreviewScene()
{
	if (!SkeletalPreviewWorld)
	{
		return;
	}
	
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
	SkeletalPreviewDirectionalLight->GetRootComponent()->SetRelativeRotation(FRotator(45.0f, -45.0f, 0.0f));
	SkeletalPreviewDirectionalLight->GetLightComponent()->SetIntensity(1.5f);
	SkeletalPreviewDirectionalLight->GetLightComponent()->SetCastShadow(false);
	for (UActorComponent* Component : SkeletalPreviewDirectionalLight->GetComponents())
	{
		if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component)) Billboard->SetVisible(false);
	}

	SkeletalPreviewAmbientLight = SkeletalPreviewWorld->SpawnActor<AAmbientLightActor>(FName("SkeletalPreviewAmbientLight"));
	SkeletalPreviewAmbientLight->GetLightComponent()->SetIntensity(0.2f);
}