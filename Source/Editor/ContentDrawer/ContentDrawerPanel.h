#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

class UTexture2D;
class USkeletalMesh;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	void SetSkeletalMeshOpenCallback(std::function<void(USkeletalMesh*)> InCallback) { SkeletalMeshOpenCallback = std::move(InCallback); }

private:
	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";

	UTexture2D* FolderIcon = nullptr;
	UTexture2D* FileIcon = nullptr;
	std::function<void(USkeletalMesh*)> SkeletalMeshOpenCallback;
};
