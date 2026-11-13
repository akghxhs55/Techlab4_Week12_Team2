#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "LevelEditorToolbar.h"

#include "Asset/AssetManager.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

namespace
{
	void BeginToolbarGroup()
	{
		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->ChannelsSplit(2);
		DL->ChannelsSetCurrent(1);
		ImGui::BeginGroup();
	}

	void EndToolbarGroup()
	{
		ImGui::EndGroup();
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();

		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->ChannelsSetCurrent(0);
		DL->AddRectFilled({ Min.x - EditorStyle::Scale(4.0f), Min.y - EditorStyle::Scale(2.0f) }, { Max.x + EditorStyle::Scale(4.0f), Max.y + EditorStyle::Scale(2.0f) }, IM_COL32(45, 45, 45, 255), 4.0f);
		DL->ChannelsMerge();
	}

	// [아이콘][라벨][v] 드롭다운 버튼. 높이는 Play 그룹 버튼과 맞춘다.
	bool DropdownButton(const char* Id, const char* Icon, const char* Label)
	{
		const float Height = EditorCommandUI::GetToolbarButtonSize().y;
		const float IconSide = EditorStyle::Scale(20.0f);
		const float Pad = ImGui::GetStyle().FramePadding.x;
		const float Gap = EditorStyle::Scale(4.0f);
		const float Chevron = ViewportToolbarWidgets::ChevronSize();
		float Width = Pad * 2.0f + IconSide + Gap + Chevron;
		if (Label) Width += Gap + ImGui::CalcTextSize(Label).x;

		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const bool bClicked = ImGui::Button(Id, { Width, Height });
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float CenterY = Pos.y + Height * 0.5f;
		float X = Pos.x + Pad;
		if (const ImTextureID Texture = ViewportToolbarWidgets::GetIcon(Icon); Texture != ImTextureID_Invalid)
			DL->AddImage(Texture, { X, CenterY - IconSide * 0.5f }, { X + IconSide, CenterY + IconSide * 0.5f },
				{ 0, 0 }, { 1, 1 }, IM_COL32(220, 220, 220, 255));
		X += IconSide;
		const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
		if (Label)
		{
			X += Gap;
			DL->AddText({ X, CenterY - ImGui::GetTextLineHeight() * 0.5f }, TextColor, Label);
			X += ImGui::CalcTextSize(Label).x;
		}
		ViewportToolbarWidgets::DrawChevron(DL, { X + Gap + Chevron * 0.5f, CenterY }, Chevron, TextColor);
		return bClicked;
	}

}

void FLevelEditorToolbar::Draw()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(4.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { EditorStyle::Scale(4.0f), EditorStyle::Scale(4.0f) });
	const float ToolbarHeight = EditorCommandUI::GetToolbarButtonSize().y + ImGui::GetStyle().WindowPadding.y * 2.0f;
	const bool bOpen = ImGui::BeginChild("##LevelEditorToolbar", ImVec2(0.0f, ToolbarHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (bOpen)
	{
		DrawPlacementGroup();
		ImGui::SameLine(0.0f, EditorStyle::Scale(16.0f));
		DrawPlayGroup();
	}
	ImGui::EndChild();                  // Begin 결과와 관계없이 항상 EndChild
	ImGui::PopStyleVar(2);
}

void FLevelEditorToolbar::DrawPlacementGroup()
{
	BeginToolbarGroup();

	EditorStyle::FPopupAnchor AddAnchor{}, GridAnchor{};
	if (DropdownButton("##AddActor", "Add", nullptr))
		ImGui::OpenPopup("LevelAddPopup");
	AddAnchor = EditorStyle::LastItemAnchor();
	if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LevelAddPopup"))
		ImGui::SetTooltip("Quickly add to the project");

	ImGui::SameLine();
	char GridLabel[16];
	std::snprintf(GridLabel, sizeof(GridLabel), "%d", GetGridSpacing ? GetGridSpacing() : 1);
	if (DropdownButton("##GridSnap", "Grid", GridLabel))
		ImGui::OpenPopup("LevelGridPopup");
	GridAnchor = EditorStyle::LastItemAnchor();
	if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LevelGridPopup"))
		ImGui::SetTooltip("Grid spacing");

	EndToolbarGroup();

	if (EditorStyle::BeginButtonPopup("LevelAddPopup", AddAnchor))
	{
		if (DrawAddMenu) DrawAddMenu();
		ImGui::EndPopup();
	}
	if (EditorStyle::BeginButtonPopup("LevelGridPopup", GridAnchor))
	{
		if (DrawGridMenu) DrawGridMenu();
		ImGui::EndPopup();
	}
}

void FLevelEditorToolbar::DrawPlayGroup()
{
	BeginToolbarGroup();

	// 같은 자리에 Play/Pause/Resume을 모두 넘긴다. IsVisible이 참인 하나만 그려진다.
	// SameLine은 "그려졌을 때만" 붙여야 빈 간격이 생기지 않는다.
	bool bFirst = true;
	auto Button = [&](const char* Id, const FEditorCommand& Command)
		{
			if (!Command.IsVisibleNow())
				return;
			if (!bFirst) ImGui::SameLine();
			EditorCommandUI::ToolbarButton(Id, Command);
			bFirst = false;
		};

	for (auto Command : Commands)
	{
		Button(Command.Label, Command);
	}
	if (!bFirst) ImGui::SameLine();
	DrawPlayModeMenu();

	EndToolbarGroup();
}

void FLevelEditorToolbar::DrawPlayModeMenu()
{
	if (!GetPlayMode || !SetPlayMode)
		return;

	// 실행 중에는 모드를 바꿔도 현재 세션에 반영되지 않으므로 막는다.
	const bool bCanChange = !CanChangePlayMode || CanChangePlayMode();
	ImGui::BeginDisabled(!bCanChange);
	// UE처럼 Play 그룹 끝의 ⋮ 버튼으로 실행 위치를 고른다.
	const ImVec2 ButtonSize = EditorCommandUI::GetToolbarButtonSize();
	if (ImGui::Button("##PlayModeMenu", { EditorStyle::Scale(16.0f), ButtonSize.y }))
		ImGui::OpenPopup("PlayModePopup");
	{
		const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
		const ImVec2 Center{ (Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f };
		const float Step = EditorStyle::Scale(5.0f), Radius = EditorStyle::Scale(1.6f);
		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);
		for (int Dot = -1; Dot <= 1; ++Dot)
			ImGui::GetWindowDrawList()->AddCircleFilled({ Center.x, Center.y + Step * Dot }, Radius, Color);
	}
	const auto PopupAnchor = EditorStyle::LastItemAnchor();
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Play Mode: %s",
			GetPlayMode() == EPlayModeType::InViewport ? "Selected Viewport" : "New Editor Window (PIE)");

	if (EditorStyle::BeginButtonPopup("PlayModePopup", PopupAnchor))
	{
		const EPlayModeType Current = GetPlayMode();
		if (ImGui::MenuItem("Selected Viewport", nullptr, Current == EPlayModeType::InViewport))
			SetPlayMode(EPlayModeType::InViewport);
		if (ImGui::MenuItem("New Editor Window (PIE)", nullptr, Current == EPlayModeType::InEditorFloating))
			SetPlayMode(EPlayModeType::InEditorFloating);
		ImGui::EndPopup();
	}
}
