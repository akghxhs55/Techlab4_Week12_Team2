#include "EnginePCH.h"
#include "Editor/EditorUI/BottomDrawerBar.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include <algorithm>

float FBottomDrawerBar::GetHeight()
{
	return EditorStyle::Scale(30.0f);
}

bool FBottomDrawerBar::IsDocked(const IEditorPanel* Panel) const
{
	return bAllowDocking && Panel && Panel->IsOpen();
}

void FBottomDrawerBar::BeginFrame(bool bActive)
{
	bDrawerOpenedThisFrame = false;
	bDrawerFocused = false;

	if (!bActive)
		return;

	if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift &&
		!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false) && ContentPanel)
	{
		if (IsDocked(ContentPanel))
		{
			ImGui::SetWindowFocus(ContentPanel->GetPanelName());
			ActiveDrawer = EBottomDrawer::None;
		}
		else
		{
			ActiveDrawer = (ActiveDrawer == EBottomDrawer::ContentDrawer)
				? EBottomDrawer::None : EBottomDrawer::ContentDrawer;
			bDrawerOpenedThisFrame = (ActiveDrawer == EBottomDrawer::ContentDrawer);
		}
	}
}

void FBottomDrawerBar::Draw(const ImVec2& TabWinPos)
{
	ImVec2 BarMin{};
	ImVec2 BarMax{};
	bool bBarHovered = false;
	DrawStatusBar(GetHeight(), BarMin, BarMax, bBarHovered);
	DrawOverlay(TabWinPos, BarMin, BarMax, bBarHovered);
}

void FBottomDrawerBar::DrawStatusBar(float Height, ImVec2& OutBarMin, ImVec2& OutBarMax, bool& OutHovered)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(3.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { EditorStyle::Scale(6.0f), EditorStyle::Scale(0.0f) });
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.086f, 0.086f, 0.098f, 1.0f));

	const bool bOpen = ImGui::BeginChild("##EditorStatusBar", ImVec2(0.0f, Height),
		ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (bOpen)
	{
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImVec2 WinPos = ImGui::GetWindowPos();
		const ImVec2 WinSize = ImGui::GetWindowSize();
		DL->AddLine(WinPos, { WinPos.x + WinSize.x, WinPos.y }, IM_COL32(48, 48, 54, 255), 1.0f);

		const float BtnHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
		const ImTextureID FolderTex = []() -> ImTextureID {
			if (UTexture2D* Tex = UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Folder.png"))
				if (Tex->GetResource() && Tex->GetResource()->GetSRV())
					return (ImTextureID)Tex->GetResource()->GetSRV();
			return ImTextureID_Invalid;
		}();
		const ImTextureID ConsoleTex = ViewportToolbarWidgets::GetIcon("Console");

		auto DrawerButton = [&](const char* Id, const char* Label, ImTextureID IconTex,
			EBottomDrawer TargetDrawer, IEditorPanel* TargetPanel, const char* Tooltip)
		{
			const bool bDocked = IsDocked(TargetPanel);
			const bool bDrawerActive = (ActiveDrawer == TargetDrawer) && !bDocked;
			const float IconSide = EditorStyle::Scale(15.0f);
			const float PadX = EditorStyle::Scale(8.0f);
			const float Gap = EditorStyle::Scale(5.0f);
			float Width = PadX * 2.0f + ImGui::CalcTextSize(Label).x;
			if (IconTex != ImTextureID_Invalid)
				Width += IconSide + Gap;

			if (bDrawerActive)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Accent);
			}
			else if (bDocked)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::BgHeader);
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::BgElevated);
			}
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bDrawerActive ? EditorColor::AccentHover : EditorColor::BgAccentHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorColor::AccentActive);

			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const bool bClicked = ImGui::Button(Id, ImVec2(Width, BtnHeight));
			ImGui::PopStyleColor(3);

			const float CenterY = Pos.y + BtnHeight * 0.5f;
			float X = Pos.x + PadX;
			if (IconTex != ImTextureID_Invalid)
			{
				DL->AddImage(IconTex,
					{ X, CenterY - IconSide * 0.5f },
					{ X + IconSide, CenterY + IconSide * 0.5f },
					{ 0, 0 }, { 1, 1 }, IM_COL32(225, 225, 230, 255));
				X += IconSide + Gap;
			}
			DL->AddText({ X, CenterY - ImGui::GetTextLineHeight() * 0.5f },
				ImGui::GetColorU32(ImGuiCol_Text), Label);

			if (bClicked && TargetPanel)
			{
				if (bDocked)
				{
					ImGui::SetWindowFocus(TargetPanel->GetPanelName());
					ActiveDrawer = EBottomDrawer::None;
				}
				else if (bDrawerActive)
				{
					ActiveDrawer = EBottomDrawer::None;
				}
				else
				{
					ActiveDrawer = TargetDrawer;
					bDrawerOpenedThisFrame = true;
				}
			}

			if (ImGui::IsItemHovered())
			{
				if (bDocked)
				{
					ImGui::SetTooltip("%s (Docked in layout - click to focus)", Label);
				}
				else
				{
					ImGui::SetTooltip("%s", Tooltip);
				}
			}
		};

		if (ContentPanel)
		{
			DrawerButton("##StatusContentDrawer", "Content Drawer", FolderTex, EBottomDrawer::ContentDrawer, ContentPanel, "Toggle Content Drawer (Ctrl+Space)");
			ImGui::SameLine();
		}

		if (LogPanel)
		{
			DrawerButton("##StatusOutputLog", "Output Log", ConsoleTex, EBottomDrawer::OutputLog, LogPanel, "Toggle Output Log drawer");
			ImGui::SameLine();

			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			if (ConsoleTex != ImTextureID_Invalid)
			{
				const float IconSide = EditorStyle::Scale(15.0f);
				const ImVec2 Cursor = ImGui::GetCursorScreenPos();
				const float CenterY = Cursor.y + BtnHeight * 0.5f;
				ImGui::Dummy(ImVec2(IconSide, BtnHeight));
				DL->AddImage(ConsoleTex,
					{ Cursor.x, CenterY - IconSide * 0.5f },
					{ Cursor.x + IconSide, CenterY + IconSide * 0.5f },
					{ 0, 0 }, { 1, 1 }, IM_COL32(190, 190, 196, 255));
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Cmd");
			ImGui::SameLine();
			const float InputWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.35f,
				EditorStyle::Scale(140.0f), EditorStyle::Scale(280.0f));
			LogPanel->RenderCommandInput("##StatusBarCmdInput", StatusCmdBuf, sizeof(StatusCmdBuf), InputWidth, false, false);
		}
	}

	OutHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	ImGui::EndChild();
	OutBarMin = ImGui::GetItemRectMin();
	OutBarMax = ImGui::GetItemRectMax();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FBottomDrawerBar::DrawOverlay(const ImVec2& TabWinPos, const ImVec2& BarMin, const ImVec2& BarMax, bool bBarHovered)
{
	if (ActiveDrawer == EBottomDrawer::ContentDrawer && (!ContentPanel || IsDocked(ContentPanel)))
	{
		ActiveDrawer = EBottomDrawer::None;
	}
	else if (ActiveDrawer == EBottomDrawer::OutputLog && (!LogPanel || IsDocked(LogPanel)))
	{
		ActiveDrawer = EBottomDrawer::None;
	}

	if (ActiveDrawer == EBottomDrawer::None)
	{
		bResizingDrawer = false;
		return;
	}

	IEditorPanel* TargetPanel = (ActiveDrawer == EBottomDrawer::ContentDrawer)
		? static_cast<IEditorPanel*>(ContentPanel)
		: static_cast<IEditorPanel*>(LogPanel);
	if (!TargetPanel)
	{
		ActiveDrawer = EBottomDrawer::None;
		return;
	}

	const float MarginX = EditorStyle::Scale(6.0f);
	const float DrawerWidth = std::max(EditorStyle::Scale(280.0f), (BarMax.x - BarMin.x) - MarginX * 2.0f);
	const float MinHeight = EditorStyle::Scale(180.0f);
	const float MaxHeight = std::max(EditorStyle::Scale(220.0f), (BarMin.y - TabWinPos.y) - EditorStyle::Scale(16.0f));
	DrawerHeight = std::clamp(DrawerHeight, MinHeight, MaxHeight);

	const ImVec2 DrawerPos(BarMin.x + MarginX, BarMin.y - DrawerHeight);
	ImGui::SetNextWindowPos(DrawerPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(DrawerWidth, DrawerHeight), ImGuiCond_Always);
	ImGui::SetNextWindowViewport(ImGui::GetWindowViewport()->ID);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.125f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(4.0f) });

	const ImGuiWindowFlags OverlayFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoSavedSettings;

	bool bOverlayHovered = false;
	if (ImGui::Begin("##BottomDrawerOverlay", nullptr, OverlayFlags))
	{
		bDrawerFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		bOverlayHovered = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_RootAndChildWindows |
			ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
			ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		ImGui::InvisibleButton("##DrawerResizeGrip", ImVec2(ImGui::GetContentRegionAvail().x, EditorStyle::Scale(5.0f)));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive())
		{
			bResizingDrawer = true;
			DrawerHeight = std::clamp(DrawerHeight - ImGui::GetIO().MouseDelta.y, MinHeight, MaxHeight);
		}
		else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bResizingDrawer = false;
		}

		// 드로어 상단 헤더: [제목] ... [Dock in Layout] [X]
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(TargetPanel->GetPanelName());

		const float DockBtnW = bAllowDocking ? EditorStyle::Scale(116.0f) : 0.0f;
		const float CloseBtnW = ImGui::GetFrameHeight();
		const float RightClusterW = bAllowDocking
			? DockBtnW + CloseBtnW + ImGui::GetStyle().ItemSpacing.x
			: CloseBtnW;
		const float AvailRight = ImGui::GetWindowContentRegionMax().x - RightClusterW;
		if (AvailRight > ImGui::GetCursorPosX())
		{
			ImGui::SameLine(AvailRight);
		}
		else
		{
			ImGui::SameLine();
		}

		if (bAllowDocking)
		{
			if (ImGui::Button("Dock in Layout", ImVec2(DockBtnW, 0.0f)))
			{
				TargetPanel->SetOpen(true);
				ActiveDrawer = EBottomDrawer::None;
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("X##CloseDrawer", ImVec2(CloseBtnW, 0.0f)))
		{
			ActiveDrawer = EBottomDrawer::None;
		}

		ImGui::Separator();

		if (ActiveDrawer != EBottomDrawer::None)
		{
			if (ImGui::BeginChild("##BottomDrawerBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
			{
				if (ActiveDrawer == EBottomDrawer::ContentDrawer && ContentPanel)
					ContentPanel->RenderContents();
				else if (ActiveDrawer == EBottomDrawer::OutputLog && LogPanel)
					LogPanel->RenderContents();
			}
			ImGui::EndChild();
		}
	}
	ImGui::End();

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);

	const bool bAnyMouseClicked =
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Middle);

	// 바깥 영역 클릭 시 자동 닫힘 (열린 프레임, 크기 조절 중, 드래그 앤 드롭 중, 팝업 열림 중 제외)
	if (ActiveDrawer != EBottomDrawer::None &&
		!bDrawerOpenedThisFrame &&
		!bResizingDrawer &&
		!bOverlayHovered &&
		!bBarHovered &&
		bAnyMouseClicked &&
		ImGui::GetDragDropPayload() == nullptr &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
	{
		ActiveDrawer = EBottomDrawer::None;
	}
}
