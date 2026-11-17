#include "EnginePCH.h"
#include "ParticleSystemEditorTab.h"

#include "Panels/ParticleSystemCurveEditorPanel.h"
#include "Panels/ParticleSystemDetailsPanel.h"
#include "Panels/ParticleSystemEmitterPanel.h"
#include "Panels/ParticleSystemViewportPanel.h"
#include "Particles/ParticleSystem.h"

namespace
{
	constexpr float LODTabWidth = 44.0f;
	constexpr float LODCloseWidth = 16.0f;
	constexpr float LODInsertGapWidth = 18.0f;

	void DrawCloseMarker(const ImVec2& Min, const ImVec2& Max, bool bHovered, bool bHeld)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
		if (bHovered)
		{
			DrawList->AddCircleFilled(Center, 7.0f, ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
		}
		constexpr float Arm = 3.5f;
		const ImU32 CrossColor = ImGui::GetColorU32(bHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		DrawList->AddLine(ImVec2(Center.x - Arm, Center.y - Arm), ImVec2(Center.x + Arm, Center.y + Arm), CrossColor, 1.5f);
		DrawList->AddLine(ImVec2(Center.x - Arm, Center.y + Arm), ImVec2(Center.x + Arm, Center.y - Arm), CrossColor, 1.5f);
	}

	void DrawInsertMarker(const ImVec2& Min, const ImVec2& Max, bool bHovered, bool bHeld)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
		if (!bHovered)
		{
			DrawList->AddCircleFilled(Center, 2.0f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			return;
		}

		const float Radius = (std::min)(Max.x - Min.x, Max.y - Min.y) * 0.5f;
		DrawList->AddCircleFilled(Center, Radius, ImGui::GetColorU32(bHeld ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
		const float Arm = Radius * 0.5f;
		const ImU32 PlusColor = ImGui::GetColorU32(ImGuiCol_Text);
		DrawList->AddLine(ImVec2(Center.x - Arm, Center.y), ImVec2(Center.x + Arm, Center.y), PlusColor, 2.0f);
		DrawList->AddLine(ImVec2(Center.x, Center.y - Arm), ImVec2(Center.x, Center.y + Arm), PlusColor, 2.0f);
	}
}

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

	const ImGuiStyle& Style = ImGui::GetStyle();
	const float LeftPadding = Style.WindowPadding.x;
	const float VerticalPadding = Style.WindowPadding.y;
	ImGui::Indent(LeftPadding);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + VerticalPadding);

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("LOD");
	ImGui::SameLine();

	DrawLODTabs(*System);

	DrawLODDistance(*System, LeftPadding);

	ImGui::Unindent(LeftPadding);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (std::max)(VerticalPadding - Style.ItemSpacing.y, 0.0f));
	ImGui::PopID();
	ImGui::Separator();
}

void FParticleSystemEditorTab::DrawLODDistance(const UParticleSystem& System, float RightPadding) const
{
	constexpr float SliderWidth = 160.0f;
	const char* DistanceText = "Distance";
	const char* BaseText = "0 (Base LOD)";

	const int32 LOD = ParticleEditor.GetCurrentLOD();
	const int32 NumLODs = ParticleEditor.GetNumLODs();
	const bool bBase = LOD == 0;
	const bool bLast = LOD + 1 >= NumLODs;
	const float MinDistance = bBase ? 0.0f : System.GetLODDistance(LOD - 1);
	const float MaxDistance = bLast ? FLT_MAX : System.GetLODDistance(LOD + 1);

	char MinLabel[32] = "";
	char MaxLabel[32] = "";
	snprintf(MinLabel, sizeof(MinLabel), "%.0f", MinDistance);
	if (!bLast)
	{
		snprintf(MaxLabel, sizeof(MaxLabel), "%.0f", MaxDistance);
	}

	// 오른쪽 정렬
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float GroupWidth = ImGui::CalcTextSize(DistanceText).x + Spacing;
	if (bBase)
	{
		GroupWidth += ImGui::CalcTextSize(BaseText).x;
	}
	else
	{
		GroupWidth += ImGui::CalcTextSize(MinLabel).x + Spacing + SliderWidth;
		if (!bLast)
		{
			GroupWidth += Spacing + ImGui::CalcTextSize(MaxLabel).x;
		}
	}

	ImGui::SameLine(0.0f, 16.0f);
	const float TargetX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - GroupWidth - RightPadding;
	if (TargetX > ImGui::GetCursorPosX())
	{
		ImGui::SetCursorPosX(TargetX);
	}

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(DistanceText);
	ImGui::SameLine();

	if (bBase)
	{
		ImGui::TextDisabled("%s", BaseText);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("LOD 0 is always used from distance 0");
		}
		return;
	}

	ImGui::TextDisabled("%s", MinLabel);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Lower bound: LOD %d distance", LOD - 1);
	}
	ImGui::SameLine();

	float Distance = System.GetLODDistance(LOD);
	ImGui::SetNextItemWidth(SliderWidth);
	const bool bChanged = bLast
		? ImGui::DragFloat("##LODDistance", &Distance, 10.0f, MinDistance, FLT_MAX, "%.0f", ImGuiSliderFlags_AlwaysClamp)
		: ImGui::SliderFloat("##LODDistance", &Distance, MinDistance, MaxDistance, "%.0f", ImGuiSliderFlags_AlwaysClamp);
	if (bChanged)
	{
		ParticleEditor.SetLODDistance(LOD, Distance);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("LOD %d starts at this distance (Ctrl+Click to type)", LOD);
	}

	if (bLast)
	{
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", MaxLabel);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Upper bound: LOD %d distance", LOD + 1);
	}
}

void FParticleSystemEditorTab::DrawLODTabs(const UParticleSystem& System) const
{
	const int32 NumLODs = ParticleEditor.GetNumLODs();
	const int32 Current = ParticleEditor.GetCurrentLOD();
	const float Height = ImGui::GetFrameHeight();

	const ImVec2 StripMin = ImGui::GetCursorScreenPos();
	const ImVec2 StripMax(StripMin.x + static_cast<float>(NumLODs) * (LODTabWidth + LODInsertGapWidth), StripMin.y + Height);
	const bool bStripHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(StripMin, StripMax);

	int32 InsertIndex = -1;
	bool bRemoveCurrent = false;
	for (int32 Index = 0; Index < NumLODs; ++Index)
	{
		ImGui::PushID(Index);
		if (Index > 0)
		{
			ImGui::SameLine(0.0f, 0.0f);
		}

		char Label[8];
		snprintf(Label, sizeof(Label), "%d", Index);
		const bool bSelected = Index == Current;
		const bool bDeletable = bSelected && Index > 0;
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (bDeletable)
		{
			ImGui::SetNextItemAllowOverlap();
		}
		if (ImGui::Button(Label, ImVec2(LODTabWidth, 0.0f)) && !bSelected)
		{
			ParticleEditor.SetCurrentLOD(Index);
		}
		if (bSelected)
		{
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemHovered())
		{
			const float Start = System.GetLODDistance(Index);
			if (Index + 1 < NumLODs)
			{
				ImGui::SetTooltip("LOD %d: %.0f ~ %.0f", Index, Start, System.GetLODDistance(Index + 1));
			}
			else
			{
				ImGui::SetTooltip("LOD %d: %.0f ~", Index, Start);
			}
		}

		if (bDeletable)
		{
			const ImVec2 TabMin = ImGui::GetItemRectMin();
			const ImVec2 TabMax = ImGui::GetItemRectMax();
			const bool bTabAreaHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(TabMin, TabMax);

			ImGui::SetCursorScreenPos(ImVec2(TabMax.x - LODCloseWidth, TabMin.y));
			if (ImGui::InvisibleButton("##DeleteLOD", ImVec2(LODCloseWidth, TabMax.y - TabMin.y)))
			{
				bRemoveCurrent = true;
			}
			const bool bCloseHovered = ImGui::IsItemHovered();
			if (bTabAreaHovered || bCloseHovered)
			{
				DrawCloseMarker(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), bCloseHovered, ImGui::IsItemActive());
			}
			if (bCloseHovered)
			{
				ImGui::SetTooltip("Delete LOD %d", Index);
			}
		}

		ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::InvisibleButton("##InsertGap", ImVec2(LODInsertGapWidth, Height)))
		{
			InsertIndex = Index + 1;
		}
		const bool bGapHovered = ImGui::IsItemHovered();
		if (bGapHovered || bStripHovered)
		{
			DrawInsertMarker(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), bGapHovered, ImGui::IsItemActive());
		}
		if (bGapHovered)
		{
			if (Index + 1 < NumLODs)
			{
				ImGui::SetTooltip("Insert LOD between %d and %d (copy of LOD %d)", Index, Index + 1, Index);
			}
			else
			{
				ImGui::SetTooltip("Add LOD %d (copy of LOD %d)", Index + 1, Index);
			}
		}

		ImGui::PopID();
	}

	if (bRemoveCurrent)
	{
		ParticleEditor.RemoveCurrentLOD();
	}
	else if (InsertIndex >= 0)
	{
		ParticleEditor.InsertLOD(InsertIndex);
	}
}

void FParticleSystemEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID LeftId = InDockSpaceId;
	ImGuiID RightId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Right, 0.35f, nullptr, &LeftId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Down, 0.3f, nullptr, &LeftId);
	const ImGuiID CurveId = ImGui::DockBuilderSplitNode(RightId, ImGuiDir_Down, 0.3f, nullptr, &RightId);

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
