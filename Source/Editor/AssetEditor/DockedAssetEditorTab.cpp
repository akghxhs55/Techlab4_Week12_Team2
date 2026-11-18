#include "EnginePCH.h"
#include "DockedAssetEditorTab.h"
#include "AssetEditor.h"
#include <algorithm>

FDockedAssetEditorTab::FDockedAssetEditorTab(FAssetEditor& InEditor, const char* DockSpaceName, const char* PanelClassName)
    : FAssetEditorTab(InEditor)
    , Editor(InEditor)
{
    const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
    DockSpaceId = ImHashStr(DockSpaceName, 0, AssetId);
    PanelClassId = ImHashStr(PanelClassName, 0, AssetId);
}

void FDockedAssetEditorTab::RegisterPanel(TUniquePtr<FAssetEditorPanelBase> Panel)
{
    Panel->SetDockClass(PanelClassId);
    Panel->Init();
    Panels.Add(std::move(Panel));
}

void FDockedAssetEditorTab::DrawMenuBar()
{
    // 레벨 에디터와 같은 순서: File → (Edit) → Window
    if (ImGui::BeginMenu("File"))
    {
        if (DrawFileMenuItems())
        {
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Close Editor"))
        {
            Editor.Close();
        }
        ImGui::EndMenu();
    }

    DrawEditMenu();

    EditorDock::DrawWindowMenu(Panels);
}

void FDockedAssetEditorTab::SetBottomDrawerPanels(FContentDrawerPanel* InContentDrawer, FOutputLogPanel* InOutputLog)
{
    BottomBar.SetPanels(InContentDrawer, InOutputLog);
    BottomBar.SetAllowDocking(false);
}

void FDockedAssetEditorTab::OnRender(bool bActive)
{
    BeforeRender();
    if (Editor.IsCloseRequested())
    {
        return;
    }

    const bool bShowBottomBar = BottomBar.HasPanels();
    if (bShowBottomBar)
    {
        BottomBar.BeginFrame(bActive);
    }

    if (bActive)
    {
        DrawToolbar();
    }

    const ImVec2 TabWinPos = ImGui::GetWindowPos();

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImVec2 Size = ImGui::GetContentRegionAvail();
        if (bShowBottomBar)
        {
            Size.y = std::max(1.0f, Size.y - FBottomDrawerBar::GetHeight() - ImGui::GetStyle().ItemSpacing.y);
        }
        if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
            (!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
        {
            ImGui::DockBuilderRemoveNode(DockSpaceId);
            BuildDefaultLayout(DockSpaceId, Size);
            bLayoutInitialized = true;
        }

        const ImGuiWindowClass WindowClass = EditorDock::MakeDockSpaceClass(PanelClassId);
        const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
        if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
        {
            const float DockHeight = (bActive && bShowBottomBar) ? Size.y : 0.0f;
            ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, DockHeight), Flags, &WindowClass);
        }
    }

    for (auto& Panel : Panels)
    {
        if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
        {
            Panel->OnRender();
        }
    }

    if (bActive && bShowBottomBar)
    {
        BottomBar.Draw(TabWinPos);
    }
}
