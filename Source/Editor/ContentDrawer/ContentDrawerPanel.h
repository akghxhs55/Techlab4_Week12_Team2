#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/BlendSpaceEditor/BlendSpaceAssetCreator.h"
#include "Editor/AnimBlueprintEditor/AnimBlueprintAssetCreator.h"
#include "Editor/ContentDrawer/MeshThumbnailRenderer.h"
#include "Asset/FbxImporter/FbxMeshBake.h"

#include <filesystem>
#include "Core/Misc/Paths.h"
#include <functional>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Shutdown();
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderContents();
	void RenderPendingThumbnails(class UEditorEngine& Engine, class FRenderer* Renderer);
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);

	// .widget 파일을 더블클릭하면 불린다 (UI 에디터가 연다).
	void SetOpenWidgetCallback(std::function<void(const FString&)> InCallback) { OnOpenWidget = std::move(InCallback); }
	void SetCreateWidgetCallback(std::function<void(const FString&)> InCallback) { OnCreateWidget = std::move(InCallback); }
	// OBJ 또는 베이크된 FBX 메시를 더블클릭하면 Viewer를 엽니다.
	void SetOpenMeshCallback(std::function<void(const FString&)> InCallback) { OnOpenMesh = std::move(InCallback); }
	// .scene 파일을 더블클릭하면 레벨 에디터가 그 씬을 연다.
	void SetOpenSceneCallback(std::function<void(const FString&)> InCallback) { OnOpenScene = std::move(InCallback); }
	void SetImportFbxCallback(std::function<void(EFbxImportMode)> InCallback) { OnImportFbx = std::move(InCallback); }
	void SetBlendSpaceCreatedCallback(std::function<void(const FString&)> InCallback)
	{
		BlendSpaceCreator.SetCreatedCallback(std::move(InCallback));
	}

    void SetAnimBlueprintCreatedCallback(std::function<void(const FString&)> InCallback)
    {
        AnimBlueprintCreator.SetCreatedCallback(std::move(InCallback));
    }

private:
	// 상단 바: 임포트 버튼 + 이동 히스토리(이전/이후) + 클릭 가능한 폴더 경로 브레드크럼
	void RenderTopToolbar();
	void RenderNavigationBar();
	void RenderAssetModals();
	void RequestRenameItem(const fs::path& TargetPath, bool bIsDirectory, bool bInTree);
	void RequestDeleteItem(const fs::path& TargetPath, bool bIsDirectory);
	void TryCommitInlineRename();
	void CancelInlineRename();
	bool DoesSubtreeMatchPathFilter(const fs::path& Path, std::string_view LowerFilter) const;
	void NavigateTo(const fs::path& NewPath);
	bool CanNavigateBack() const;
	bool CanNavigateForward() const;
	void NavigateBack();
	void NavigateForward();

	std::function<void(const FString&)> OnOpenWidget;
	std::function<void(const FString&)> OnCreateWidget;
	std::function<void(const FString&)> OnOpenMesh;
	std::function<void(const FString&)> OnOpenScene;
	std::function<void(EFbxImportMode)> OnImportFbx;
	char NewAssetName[64] = "NewWidget";
	char RenameBuffer[128] = {};
	char AssetSearchBuf[128] = {};
	char PathSearchBuf[128] = {};
	bool bShowPathSearch = false;
	bool bFocusPathSearch = false;
	bool bOpenCreateWidget = false;
	bool bOpenCreateFolder = false;
	bool bOpenDeleteModal = false;
	float FolderTreeWidth = 0.0f;
	fs::path SelectedAssetPath;
	bool bSelectedAssetIsDirectory = false;
	fs::path InlineRenameTarget;
	bool bInlineRenameIsDirectory = false;
	bool bInlineRenameInTree = false;
	bool bFocusInlineRename = false;
	bool bInlineRenameWasActive = false;
	bool bPendingCommitInlineRename = false;
	fs::path PendingItemTarget;
	bool PendingItemIsDirectory = false;
	std::string FileOpErrorMessage;

	fs::path AssetRootPath = FPaths::ProjectContentDir();
	fs::path CurrentPath = AssetRootPath;
	std::vector<fs::path> PathHistory{ AssetRootPath };
	size_t PathHistoryIndex = 0;

	const FString FolderIconPath = "Engine/Icons/Folder.png";
	const FString FileIconPath = "Engine/Icons/File.png";
	const FString ParticleSystemIconPath = "Engine/Icons/ParticleSystem.png";
	const FString SceneIconPath = "Engine/Icons/Scene.png";

	FBlendSpaceAssetCreator BlendSpaceCreator;
    FAnimBlueprintAssetCreator AnimBlueprintCreator;
	FMeshThumbnailRenderer MeshThumbnailRenderer;
};
