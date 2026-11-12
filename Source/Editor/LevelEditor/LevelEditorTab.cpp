#include "EnginePCH.h"
#include "Editor/LevelEditor/LevelEditorTab.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Editor/EditorUI/EditorPanel.h"
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
	if (bActive)
		Toolbar.Draw();

	if (bUseDockSpace && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
	{
		// 부모 창이 달라져도 기존 패널 도킹 ID는 유지한다.
		const ImGuiID DockSpaceId = ImHashStr("EngineDockingSpace", 0, ImHashStr("DockingSpace"));
		ImGuiDockNodeFlags Flags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode : ImGuiDockNodeFlags_None;
		if (!bActive)
			Flags |= ImGuiDockNodeFlags_KeepAliveOnly;

		const ImGuiWindowClass DockSpaceClass = EditorDock::MakeDockSpaceClass(PanelClassId);
		ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &DockSpaceClass);
	}

	if (bActive)
	{
		// 레벨 에디터 패널은 창을 각자 Begin하므로, 여기서 다음 창의 클래스를 미리 지정한다.
		const ImGuiWindowClass PanelClass = EditorDock::MakePanelClass(PanelClassId);
		for (auto& Panel : Panels)
		{
			if (!Panel->IsOpen())
				continue;

			ImGui::SetNextWindowClass(&PanelClass);
			Panel->OnRender();
			// 패널이 창을 열지 않고 돌아오면 지정한 클래스가 엉뚱한 창에 붙지 않게 지운다.
			GImGui->NextWindowData.Flags &= ~ImGuiNextWindowDataFlags_HasWindowClass;
		}
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
