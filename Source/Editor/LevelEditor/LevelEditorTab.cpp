#include "EnginePCH.h"
#include "Editor/LevelEditor/LevelEditorTab.h"
#include "Editor/KurisuEd/EditorFileUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include <algorithm>
#include <filesystem>

// 레벨 에디터 패널끼리만 도킹되게 하는 윈도우 클래스 ID. 레벨 에디터 탭은 하나뿐이라 고정 이름을 쓴다.
static const ImGuiID PanelClassId = ImHashStr("LevelEditorPanelClass");

FLevelEditorTab::FLevelEditorTab()
{
	OnLevelLoaded.Add([this](std::string_view Path) {
		FLevelEditorTab::UpdateTabName(Path);
	});

	FLevelEditorTab::UpdateTabName(FEditorFileUtils::CurrentScenePath);
}

void FLevelEditorTab::DrawMenuBar()
{
	////////////////////////////////////////////////////////////
	// File 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene")) { OnNewScene.Broadcast(); }
		if (ImGui::MenuItem("Open Scene...")) { OnOpenScene.Broadcast(); }

		ImGui::Separator();

		if (ImGui::MenuItem("Save")) { OnSaveScene.Broadcast(); }
		if (ImGui::MenuItem("Save As...")) { OnSaveSceneAs.Broadcast(); }

		ImGui::Separator();

		if (ImGui::BeginMenu("Import FBX"))
		{
			if (ImGui::MenuItem("Static Mesh...")) { OnImportFbx.Broadcast(EFbxImportMode::Static); }
			if (ImGui::MenuItem("Skeletal Mesh...")) { OnImportFbx.Broadcast(EFbxImportMode::Skeletal); }
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}



	////////////////////////////////////////////////////////////
	// Edit 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("Edit"))
	{
		const bool bCanUndo = CanUndoQuery && CanUndoQuery();
		const bool bCanRedo = CanRedoQuery && CanRedoQuery();
		const char* UndoAction = (bCanUndo && GetUndoLabelQuery) ? GetUndoLabelQuery() : "";
		const char* RedoAction = (bCanRedo && GetRedoLabelQuery) ? GetRedoLabelQuery() : "";
		const FString UndoLabel = (UndoAction && UndoAction[0]) ? std::format("Undo {}", UndoAction) : FString("Undo");
		const FString RedoLabel = (RedoAction && RedoAction[0]) ? std::format("Redo {}", RedoAction) : FString("Redo");
		if (ImGui::MenuItem(UndoLabel.c_str(), "Ctrl+Z", false, bCanUndo)) { OnUndo.Broadcast(); }
		if (ImGui::MenuItem(RedoLabel.c_str(), "Ctrl+Y", false, bCanRedo)) { OnRedo.Broadcast(); }
		ImGui::EndMenu();
	}



	////////////////////////////////////////////////////////////
	// Window 탭
	////////////////////////////////////////////////////////////

	EditorDock::DrawWindowMenu(Panels);



	////////////////////////////////////////////////////////////
	// Build 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("Build"))
	{
		const bool bCanPackage = !IsPackaging || !IsPackaging();
		if (ImGui::MenuItem("Package Game (Release)", nullptr, false, bCanPackage)) { OnPackageGame.Broadcast(); }
		ImGui::EndMenu();
	}
}

void FLevelEditorTab::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
		Panel->Tick(DeltaTime);
}

void FLevelEditorTab::OnRender(bool bActive)
{
	bDrawerOpenedThisFrame = false;
	bDrawerFocused = false;

	if (bActive)
	{
		if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift &&
			!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			if (DrawerContentPanel && DrawerContentPanel->IsOpen())
			{
				ImGui::SetWindowFocus(DrawerContentPanel->GetPanelName());
				ActiveDrawer = EBottomDrawer::None;
			}
			else
			{
				ActiveDrawer = (ActiveDrawer == EBottomDrawer::ContentDrawer)
					? EBottomDrawer::None : EBottomDrawer::ContentDrawer;
				bDrawerOpenedThisFrame = (ActiveDrawer == EBottomDrawer::ContentDrawer);
			}
		}

		Toolbar.Draw();
	}

	const ImVec2 TabWinPos = ImGui::GetWindowPos();
	const float StatusBarHeight = EditorStyle::Scale(30.0f);

	if (bUseDockSpace && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
	{
		// 부모 창이 달라져도 기존 패널 도킹 ID는 유지한다.
		const ImGuiID DockSpaceId = ImHashStr("EngineDockingSpace", 0, ImHashStr("DockingSpace"));
		ImGuiDockNodeFlags Flags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode : ImGuiDockNodeFlags_None;
		if (!bActive)
			Flags |= ImGuiDockNodeFlags_KeepAliveOnly;

		const float DockHeight = bActive
			? std::max(1.0f, ImGui::GetContentRegionAvail().y - StatusBarHeight - ImGui::GetStyle().ItemSpacing.y)
			: 0.0f;
		const ImGuiWindowClass DockSpaceClass = EditorDock::MakeDockSpaceClass(PanelClassId);
		ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, DockHeight), Flags, &DockSpaceClass);
	}

	if (bActive)
	{
		ImVec2 BarMin{};
		ImVec2 BarMax{};
		bool bBarHovered = false;
		DrawStatusBar(StatusBarHeight, BarMin, BarMax, bBarHovered);

		// 레벨 에디터 패널은 창을 각자 Begin하므로, 여기서 다음 창의 클래스를 미리 지정한다.
		const ImGuiWindowClass PanelClass = EditorDock::MakePanelClass(PanelClassId);
		for (auto& Panel : Panels)
		{
			if (!Panel->IsOpen())
				continue;

			ImGui::SetNextWindowClass(&PanelClass);
			Panel->OnRender();
			// 패널이 창을 열지 않고 돌아오면 지정한 클래스가 엉뚱한 창에 붙지 않게 지운다.
			GImGui->NextWindowData.HasFlags &= ~ImGuiNextWindowDataFlags_HasWindowClass;
		}

		DrawBottomDrawerOverlay(TabWinPos, BarMin, BarMax, bBarHovered);
	}
}

void FLevelEditorTab::DrawStatusBar(float Height, ImVec2& OutBarMin, ImVec2& OutBarMax, bool& OutHovered)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(3.0f) });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { EditorStyle::Scale(6.0f), EditorStyle::Scale(0.0f) });
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.086f, 0.086f, 0.098f, 1.0f));

	const bool bOpen = ImGui::BeginChild("##LevelStatusBar", ImVec2(0.0f, Height),
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
			const bool bDocked = TargetPanel && TargetPanel->IsOpen();
			const bool bDrawerActive = (ActiveDrawer == TargetDrawer) && !bDocked;
			const float IconSide = EditorStyle::Scale(15.0f);
			const float PadX = EditorStyle::Scale(8.0f);
			const float Gap = EditorStyle::Scale(5.0f);
			float Width = PadX * 2.0f + ImGui::CalcTextSize(Label).x;
			if (IconTex != ImTextureID_Invalid)
				Width += IconSide + Gap;

			if (bDrawerActive)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.44f, 0.76f, 1.0f));
			else if (bDocked)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.19f, 0.22f, 1.0f));
			else
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));

			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const bool bClicked = ImGui::Button(Id, ImVec2(Width, BtnHeight));
			ImGui::PopStyleColor();

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
					ImGui::SetTooltip("%s (Docked in layout - click to focus)", Label);
				else
					ImGui::SetTooltip("%s", Tooltip);
			}
		};

		if (DrawerContentPanel)
		{
			DrawerButton("##StatusContentDrawer", "Content Drawer", FolderTex,
				EBottomDrawer::ContentDrawer, DrawerContentPanel, "Toggle Content Drawer (Ctrl+Space)");
			ImGui::SameLine();
		}

		if (DrawerLogPanel)
		{
			DrawerButton("##StatusOutputLog", "Output Log", ConsoleTex,
				EBottomDrawer::OutputLog, DrawerLogPanel, "Toggle Output Log drawer");
			ImGui::SameLine();

			// UE5 하단 바의 Cmd 빠른 실행 입력칸
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
			ImGui::SetNextItemWidth(InputWidth);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.06f, 0.07f, 1.0f));
			if (ImGui::InputTextWithHint("##StatusBarCmdInput", "Enter Console Command",
				StatusCmdBuf, sizeof(StatusCmdBuf), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				if (StatusCmdBuf[0] != '\0')
				{
					DrawerLogPanel->ExecCommand(StatusCmdBuf);
					StatusCmdBuf[0] = '\0';
				}
			}
			ImGui::PopStyleColor();
		}
	}

	OutHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	ImGui::EndChild();
	OutBarMin = ImGui::GetItemRectMin();
	OutBarMax = ImGui::GetItemRectMax();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FLevelEditorTab::DrawBottomDrawerOverlay(const ImVec2& TabWinPos, const ImVec2& BarMin, const ImVec2& BarMax, bool bBarHovered)
{
	if (ActiveDrawer == EBottomDrawer::ContentDrawer && (!DrawerContentPanel || DrawerContentPanel->IsOpen()))
		ActiveDrawer = EBottomDrawer::None;
	else if (ActiveDrawer == EBottomDrawer::OutputLog && (!DrawerLogPanel || DrawerLogPanel->IsOpen()))
		ActiveDrawer = EBottomDrawer::None;

	if (ActiveDrawer == EBottomDrawer::None)
	{
		bResizingDrawer = false;
		return;
	}

	IEditorPanel* TargetPanel = (ActiveDrawer == EBottomDrawer::ContentDrawer)
		? static_cast<IEditorPanel*>(DrawerContentPanel)
		: static_cast<IEditorPanel*>(DrawerLogPanel);
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

		// 상단 경계선 드래그로 드로어 높이 조절
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

		const float DockBtnW = EditorStyle::Scale(116.0f);
		const float CloseBtnW = ImGui::GetFrameHeight();
		const float RightClusterW = DockBtnW + CloseBtnW + ImGui::GetStyle().ItemSpacing.x;
		const float AvailRight = ImGui::GetWindowContentRegionMax().x - RightClusterW;
		if (AvailRight > ImGui::GetCursorPosX())
		{
			ImGui::SameLine(AvailRight);
		}
		else
		{
			ImGui::SameLine();
		}

		if (ImGui::Button("Dock in Layout", ImVec2(DockBtnW, 0.0f)))
		{
			TargetPanel->SetOpen(true);
			ActiveDrawer = EBottomDrawer::None;
		}
		ImGui::SameLine();
		if (ImGui::Button("X##CloseDrawer", ImVec2(CloseBtnW, 0.0f)))
		{
			ActiveDrawer = EBottomDrawer::None;
		}

		ImGui::Separator();

		if (ActiveDrawer != EBottomDrawer::None)
		{
			if (ImGui::BeginChild("##BottomDrawerBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
			{
				if (ActiveDrawer == EBottomDrawer::ContentDrawer && DrawerContentPanel)
					DrawerContentPanel->RenderContents();
				else if (ActiveDrawer == EBottomDrawer::OutputLog && DrawerLogPanel)
					DrawerLogPanel->RenderContents();
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

void FLevelEditorTab::SetDockSpaceOptions(bool bEnabled, bool bPassthru)
{
	bUseDockSpace = bEnabled;
	bPassthruCentralNode = bPassthru;
}

void FLevelEditorTab::UpdateTabName(std::string_view InPath)
{
	const FString LevelName = InPath.empty()
		? "EmptyLevel" : std::filesystem::path(InPath).filename().string();
	TabName = LevelName + " (LevelEditor)###LevelEditor";
}
