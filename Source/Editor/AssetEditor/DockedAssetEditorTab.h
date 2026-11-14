#pragma once

#include "AssetEditorTab.h"
#include "AssetEditorPanel.h"
#include "Containers/Array.h"

class FDockedAssetEditorTab : public FAssetEditorTab
{
public:
    FDockedAssetEditorTab(FAssetEditor& InEditor, const char* DockSpaceName, const char* PanelClassName);

    void DrawMenuBar() override;
    void OnRender(bool bActive) override;

protected:
    void RegisterPanel(TUniquePtr<FAssetEditorPanelBase> Panel);
    virtual void BeforeRender()
    {
    }
    virtual void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) = 0;
    // File 메뉴의 Close Editor 위에 들어갈 항목(Save 등). 항목을 그렸으면 true를 돌려 구분선을 넣는다.
    virtual bool DrawFileMenuItems() { return false; }
    // Undo/Redo가 있는 에디터만 Edit 메뉴를 그린다.
    virtual void DrawEditMenu() {}

    TArray<TUniquePtr<FAssetEditorPanelBase>> Panels;

private:
    FAssetEditor& Editor;
    ImGuiID DockSpaceId = 0;
    ImGuiID PanelClassId = 0;
    bool bLayoutInitialized = false;
};
