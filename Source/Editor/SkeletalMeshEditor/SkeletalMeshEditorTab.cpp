#include "EnginePCH.h"
#include "SkeletalMeshEditorTab.h"
#include "SkeletalMeshEditor.h"
#include "Panels/SkeletalMeshViewportPanel.h"
#include "Panels/SkeletalMeshInfoPanel.h"
#include "Panels/SkeletalMeshDetailsPanel.h"
#include "Panels/BoneHierarchyPanel.h"
#include "Panels/SkeletalMeshEditorPanel.h"
#include "Editor/AnimSequenceEditor/Panels/AnimBrowserPanels.h"

namespace
{
	// 스켈레탈 메시 뷰어에서 이 메시용 애니메이션(.anim.bin)을 검색·조회하고 더블클릭으로 여는 패널
	class FSkeletalMeshAssetBrowserPanel final : public FSkeletalMeshEditorPanel
	{
	public:
		explicit FSkeletalMeshAssetBrowserPanel(FSkeletalMeshEditor& InEditor)
			: FSkeletalMeshEditorPanel(InEditor, "Asset Browser")
		{
		}

	protected:
		void DrawContent() override
		{
			FString OpenedPath;
			if (BrowserView.Draw(Editor.GetAssetPath(), FString(), false, OpenedPath))
			{
				Editor.OpenAnimationAsset(OpenedPath);
			}
		}

	private:
		FAnimAssetBrowserView BrowserView;
	};
}

FSkeletalMeshEditorTab::FSkeletalMeshEditorTab(FSkeletalMeshEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, MeshEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지합니다.
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("SkeletalMeshDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("SkeletalMeshPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FSkeletalMeshViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshInfoPanel>(InEditor));
	Panels.Add(MakeUnique<FBoneHierarchyPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshAssetBrowserPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FSkeletalMeshEditorTab::DrawMenuBar()
{
	// 레벨 에디터와 같은 순서: File → Edit → Window
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Close Editor"))
		{
			MeshEditor.Close();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, MeshEditor.CanUndoPoseEdit()))
		{
			MeshEditor.UndoPoseEdit();
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, MeshEditor.CanRedoPoseEdit()))
		{
			MeshEditor.RedoPoseEdit();
		}
		ImGui::EndMenu();
	}

	EditorDock::DrawWindowMenu(Panels);
}

void FSkeletalMeshEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID HierarchyId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.18f, nullptr, &ViewportId);
	ImGuiID RightId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.22f, nullptr, &ViewportId);
	// 오른쪽 패널을 위에서부터 Info, Details, Asset Browser 순서로 배치합니다.
	ImGuiID RemainingId = RightId;
	const ImGuiID InfoId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 0.22f, nullptr, &RemainingId);
	ImGuiID DetailsId = RemainingId;
	const ImGuiID BrowserId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.45f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), InfoId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), HierarchyId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), BrowserId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FSkeletalMeshEditorTab::OnRender(bool bActive)
{
	MeshEditor.SetViewportState(0, 0, {});
	if (MeshEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작합니다.
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
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시합니다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}

	MeshEditor.CommitBonePoseUndo(MeshEditor.IsGizmoDragging() || ImGui::IsAnyItemActive());

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bTabFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy) ||
		HasFocusedSubWindow();
	if (bActive && bTabFocused && IO.KeyCtrl && !IO.WantTextInput)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if (IO.KeyShift) MeshEditor.RedoPoseEdit();
			else             MeshEditor.UndoPoseEdit();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
		{
			MeshEditor.RedoPoseEdit();
		}
	}
}
