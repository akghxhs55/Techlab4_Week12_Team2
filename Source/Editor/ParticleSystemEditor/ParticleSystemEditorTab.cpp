#include "EnginePCH.h"
#include "ParticleSystemEditorTab.h"

#include "Panels/ParticleSystemCurveEditorPanel.h"
#include "Panels/ParticleSystemDetailsPanel.h"
#include "Panels/ParticleSystemEmitterPanel.h"
#include "Panels/ParticleSystemViewportPanel.h"

FParticleSystemEditorTab::FParticleSystemEditorTab(FParticleSystemEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "ParticleDockSpace", "ParticlePanelClass")
	, ParticleEditor(InEditor)
{
    RegisterPanel(MakeUnique<FParticleSystemViewportPanel>(InEditor));
    RegisterPanel(MakeUnique<FParticleSystemEmitterPanel>(InEditor));
    RegisterPanel(MakeUnique<FParticleSystemDetailsPanel>(InEditor));
    RegisterPanel(MakeUnique<FParticleSystemCurveEditorPanel>(InEditor));
}

void FParticleSystemEditorTab::OnRender(bool bActive)
{
    FDockedAssetEditorTab::OnRender(bActive);

    if (ParticleEditor.IsCloseRequested())
    {
        return;
    }

    const ImGuiIO& IO = ImGui::GetIO();
    const bool bTabFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy) ||
        HasFocusedSubWindow();
    if (bActive && bTabFocused && IO.KeyCtrl && !IO.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && ParticleEditor.IsDirty())
        {
            ParticleEditor.Save();
        }
    }
}

void FParticleSystemEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID LeftId = InDockSpaceId;
	ImGuiID RightId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Right, 0.62f, nullptr, &LeftId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Down, 0.5f, nullptr, &LeftId);
	const ImGuiID CurveId = ImGui::DockBuilderSplitNode(RightId, ImGuiDir_Down, 0.4f, nullptr, &RightId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), RightId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), CurveId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

bool FParticleSystemEditorTab::DrawFileMenuItems()
{
    if (ImGui::MenuItem("Save", "Ctrl+S", false, ParticleEditor.IsDirty()))
    {
        ParticleEditor.Save();
    }
    if (ImGui::MenuItem("Revert to Saved", nullptr, false, ParticleEditor.IsDirty()))
    {
        ParticleEditor.RevertToSaved();
    }
    return true;
}
