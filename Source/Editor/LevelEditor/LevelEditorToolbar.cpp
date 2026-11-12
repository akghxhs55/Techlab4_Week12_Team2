#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "LevelEditorToolbar.h"

#include "Asset/AssetManager.h"

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
		DrawPlayGroup();
	}
	ImGui::EndChild();                  // Begin 결과와 관계없이 항상 EndChild
	ImGui::PopStyleVar(2);
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
	//Button("##Play", Play);
	//Button("##Pause", Pause);
	//Button("##Resume", Resume);
	//Button("##Step", StepFrame);
	//Button("##Stop", Stop);

	EndToolbarGroup();
}

void FLevelEditorToolbar::DrawPlayModeMenu()
{
	if (!GetPlayMode || !SetPlayMode)
		return;

	// 실행 중에는 모드를 바꿔도 현재 세션에 반영되지 않으므로 막는다.
	const bool bCanChange = !CanChangePlayMode || CanChangePlayMode();
	ImGui::BeginDisabled(!bCanChange);
	if (ImGui::ArrowButtonEx("##PlayModeMenu", ImGuiDir_Down, EditorCommandUI::GetToolbarButtonSize()))
		ImGui::OpenPopup("PlayModePopup");
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
