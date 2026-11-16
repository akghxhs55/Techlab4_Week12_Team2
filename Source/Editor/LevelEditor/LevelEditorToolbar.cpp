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
		DL->AddRectFilled({ Min.x - EditorStyle::Scale(4.0f), Min.y - EditorStyle::Scale(2.0f) }, { Max.x + EditorStyle::Scale(4.0f), Max.y + EditorStyle::Scale(2.0f) }, EditorColor::BgRaised, 4.0f);
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
				{ 0, 0 }, { 1, 1 }, EditorColor::TextSecondary);
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
	// UE처럼 버튼 크기는 유지하고 툴바 안에서 위아래로 여유를 둔다.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(8.0f) });
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

	EditorStyle::FPopupAnchor AddAnchor{}, SnapAnchor{}, GridAnchor{};
	if (DropdownButton("##AddActor", "Add", nullptr))
		ImGui::OpenPopup("LevelAddPopup");
	AddAnchor = EditorStyle::LastItemAnchor();
	if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LevelAddPopup"))
		ImGui::SetTooltip("Quickly add to the project");

	ImGui::SameLine();
	if (GetLocationSnap && DrawSnapMenu)
	{
		// Grid와 같은 [아이콘][값][v] 드롭다운. 켜져 있으면 간격을, 꺼져 있으면 Off를 보여준다.
		const bool bSnapOn = GetLocationSnap();
		char SnapLabel[16];
		if (bSnapOn)
			std::snprintf(SnapLabel, sizeof(SnapLabel), "%g", GetLocationSnapSize ? GetLocationSnapSize() : 1.0f);
		else
			std::snprintf(SnapLabel, sizeof(SnapLabel), "Off");

		// 켜진 상태는 다른 토글 버튼과 같은 엔진 강조색(적갈색)으로 표시한다.
		EditorStyle::PushSelectedButton(bSnapOn);
		if (DropdownButton("##LocationSnap", "Snap", SnapLabel))
			ImGui::OpenPopup("LevelSnapPopup");
		EditorStyle::PopSelectedButton(bSnapOn);
		SnapAnchor = EditorStyle::LastItemAnchor();
		if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LevelSnapPopup"))
			ImGui::SetTooltip(bSnapOn ? "Snap: ON (G to toggle)\nLocation/Scale use the snap size, rotation snaps by 5 degrees."
				: "Snap: OFF (G to toggle)");
		ImGui::SameLine();
	}

	char GridLabel[16];
	std::snprintf(GridLabel, sizeof(GridLabel), "%d", GetGridSpacing ? GetGridSpacing() : 1);
	if (DropdownButton("##GridSnap", "Grid", GridLabel))
		ImGui::OpenPopup("LevelGridPopup");
	GridAnchor = EditorStyle::LastItemAnchor();
	if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LevelGridPopup"))
		ImGui::SetTooltip("Grid");

	EndToolbarGroup();

	if (EditorStyle::BeginButtonPopup("LevelAddPopup", AddAnchor))
	{
		if (DrawAddMenu) DrawAddMenu();
		ImGui::EndPopup();
	}
	if (EditorStyle::BeginButtonPopup("LevelSnapPopup", SnapAnchor))
	{
		if (DrawSnapMenu) DrawSnapMenu();
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
	// 20px 아이콘 + 사방 4px: 배치 드롭다운과 높이는 같고, 재생 컨트롤은 정사각형이다.
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { EditorStyle::Scale(4.0f), EditorStyle::Scale(4.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { EditorStyle::Scale(2.0f), EditorStyle::Scale(4.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Transparent);
	BeginToolbarGroup();

	// 먼저 이번 프레임의 버튼을 확정한다. Pause 클릭 직후 Resume까지 그리면 묶음 폭이 순간적으로 바뀐다.
	TArray<const FEditorCommand*> VisibleCommands;
	for (const FEditorCommand& Command : Commands)
		if (Command.IsVisibleNow()) VisibleCommands.Add(&Command);

	bool bFirst = true;
	for (const FEditorCommand* Command : VisibleCommands)
	{
		if (!bFirst) ImGui::SameLine();
		EditorCommandUI::ToolbarButton(Command->Label, *Command);
		bFirst = false;
	}
	if (!bFirst) ImGui::SameLine();
	DrawPlayModeMenu();

	EndToolbarGroup();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
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
	if (ImGui::Button("##PlayModeMenu", { EditorStyle::Scale(20.0f), ButtonSize.y }))
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
