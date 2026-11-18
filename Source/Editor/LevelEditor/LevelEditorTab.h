#pragma once

#include "Core/Delegate/Delegate.h"
#include "Editor/EditorUI/BottomDrawerBar.h"
#include "Editor/EditorUI/EditorTab.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"
#include "Runtime/Asset/FbxImporter/FbxMeshBake.h"

class FContentDrawerPanel;
class FOutputLogPanel;

class FLevelEditorTab : public IEditorTab
{
public:
	explicit FLevelEditorTab();

	const char* GetTabName() const override { return TabName.c_str(); }
	bool CanDetach() const override { return false; }
	bool HasFocusedSubWindow() const override { return BottomBar.IsFocused() || EditorDock::HasFocusedPanel(Panels); }
	void DrawMenuBar() override;

	TDelegate<> OnNewScene;
	TDelegate<> OnOpenScene;
	TDelegate<> OnSaveScene;
	TDelegate<> OnSaveSceneAs;
	TDelegate<> OnUndo;
	TDelegate<> OnRedo;
	TDelegate<EFbxImportMode> OnImportFbx;
	TDelegate<> OnPackageGame;
	TDelegate<const FString&> OnLevelLoaded;

	void SetIsPackagingQuery(std::function<bool()> InQuery) { IsPackaging = std::move(InQuery); }
	void SetUndoQueries(std::function<bool()> InCanUndo, std::function<bool()> InCanRedo,
		std::function<const char*()> InUndoLabel = {}, std::function<const char*()> InRedoLabel = {})
	{
		CanUndoQuery = std::move(InCanUndo);
		CanRedoQuery = std::move(InCanRedo);
		GetUndoLabelQuery = std::move(InUndoLabel);
		GetRedoLabelQuery = std::move(InRedoLabel);
	}

	FLevelEditorToolbar* GetToolbar() { return &Toolbar; }
	TArray<TUniquePtr<IEditorPanel>>& GetPanels() { return Panels; }

	template <typename T>
	T* AddPanel()
	{
		TUniquePtr<T> Panel = MakeUnique<T>();
		T* Result = Panel.get();
		Result->Init();
		Panels.Add(std::move(Panel));
		return Result;
	}

	void Tick(float DeltaTime);

	void OnRender(bool bActive) override;
	
	void SetDockSpaceOptions(bool bEnabled, bool bPassthru);

	void UpdateTabName(std::string_view InPath);
	void SetDrawerPanels(FContentDrawerPanel* InContentDrawer, FOutputLogPanel* InOutputLog)
	{
		BottomBar.SetPanels(InContentDrawer, InOutputLog);
	}

private:
	std::function<bool()> IsPackaging;
	std::function<bool()> CanUndoQuery;
	std::function<bool()> CanRedoQuery;
	std::function<const char*()> GetUndoLabelQuery;
	std::function<const char*()> GetRedoLabelQuery;
	FLevelEditorToolbar Toolbar;
	TArray<TUniquePtr<IEditorPanel>> Panels;
	FBottomDrawerBar BottomBar;
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;

	FString TabName;
};
