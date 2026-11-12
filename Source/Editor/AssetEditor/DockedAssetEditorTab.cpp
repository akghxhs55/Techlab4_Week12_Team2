#include "EnginePCH.h"
#include "DockedAssetEditorTab.h"
#include "AssetEditor.h"

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
    if (ImGui::BeginMenu("Viewer"))
    {
        if (ImGui::MenuItem("Close Viewer"))
        {
            Editor.Close();
        }
        ImGui::EndMenu();
    }

    EditorDock::DrawWindowMenu(Panels);
}

void FDockedAssetEditorTab::OnRender(bool bActive)
{
    BeforeRender();
    if (Editor.IsCloseRequested())
    {
        return;
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        const ImVec2 Size = ImGui::GetContentRegionAvail();
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
            ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
        }
    }

    for (auto& Panel : Panels)
    {
        if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
        {
            Panel->OnRender();
        }
    }
}
