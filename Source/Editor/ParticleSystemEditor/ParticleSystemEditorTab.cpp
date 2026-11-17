#include "EnginePCH.h"
#include "ParticleSystemEditorTab.h"

#include "Panels/ParticleSystemCurveEditorPanel.h"
#include "Panels/ParticleSystemDetailsPanel.h"
#include "Panels/ParticleSystemEmitterPanel.h"
#include "Panels/ParticleSystemViewportPanel.h"
#include "Particles/ParticleSystem.h"

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

void FParticleSystemEditorTab::DrawToolbar()
{
	const UParticleSystem* System = ParticleEditor.GetWorkingCopy();
	if (!System)
	{
		return;
	}

	ImGui::PushID("ParticleLODToolbar");

	const int32 NumLODs = ParticleEditor.GetNumLODs();
	const int32 Current = ParticleEditor.GetCurrentLOD();

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("LOD");
	ImGui::SameLine();

	ImGui::BeginDisabled(Current <= 0);
	if (ImGui::ArrowButton("##HigherLOD", ImGuiDir_Left))
	{
		ParticleEditor.SetCurrentLOD(Current - 1);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	char Preview[32];
	snprintf(Preview, sizeof(Preview), "LOD %d / %d", Current, NumLODs - 1);
	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::BeginCombo("##CurrentLOD", Preview))
	{
		for (int32 Index = 0; Index < NumLODs; ++Index)
		{
			char Label[16];
			snprintf(Label, sizeof(Label), "LOD %d", Index);
			if (ImGui::Selectable(Label, Index == Current))
			{
				ParticleEditor.SetCurrentLOD(Index);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();

	ImGui::BeginDisabled(Current >= NumLODs - 1);
	if (ImGui::ArrowButton("##LowerLOD", ImGuiDir_Right))
	{
		ParticleEditor.SetCurrentLOD(Current + 1);
	}
	ImGui::EndDisabled();

	const int32 LOD = ParticleEditor.GetCurrentLOD();
	ImGui::SameLine(0.0f, 16.0f);
	ImGui::TextUnformatted("Distance");
	ImGui::SameLine();
	float Distance = System->GetLODDistance(LOD);
	const float MinDistance = LOD > 0 ? System->GetLODDistance(LOD - 1) : 0.0f;
	const float MaxDistance = LOD + 1 < System->GetNumLODs() ? System->GetLODDistance(LOD + 1) : FLT_MAX;
	ImGui::SetNextItemWidth(120.0f);
	ImGui::BeginDisabled(LOD == 0);
	if (ImGui::DragFloat("##LODDistance", &Distance, 10.0f, MinDistance, MaxDistance, "%.0f", ImGuiSliderFlags_AlwaysClamp))
	{
		ParticleEditor.SetLODDistance(LOD, Distance);
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 16.0f);
	if (ImGui::Button("Add LOD"))
	{
		ParticleEditor.AddLOD();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Copy the current LOD and insert it right after");
	}
	ImGui::SameLine();

	ImGui::BeginDisabled(ParticleEditor.GetCurrentLOD() == 0);
	if (ImGui::Button("Delete LOD"))
	{
		ParticleEditor.RemoveCurrentLOD();
	}
	ImGui::EndDisabled();

	ImGui::PopID();
	ImGui::Separator();
}

void FParticleSystemEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID LeftId = InDockSpaceId;
	ImGuiID RightId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Right, 0.4f, nullptr, &LeftId);
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
