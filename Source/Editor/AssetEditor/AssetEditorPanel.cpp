#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/EditorUI/EditorStyle.h"

FAssetEditorPanelBase::FAssetEditorPanelBase(const char* InName, const FString& InWindowId)
	: PanelName(InName)
	, WindowName(PanelName + "###" + InWindowId)
{
}

void FAssetEditorPanelBase::OnRender()
{
	// 같은 탭의 패널끼리만 도킹되도록 탭이 정한 클래스를 지정합니다.
	const ImGuiWindowClass WindowClass = EditorDock::MakePanelClass(DockClassId);
	ImGui::SetNextWindowClass(&WindowClass);
	ImGui::SetNextWindowSize(ImVec2(EditorStyle::Scale(480.0f), EditorStyle::Scale(320.0f)), ImGuiCond_FirstUseEver);

	// 뷰포트 패널은 레벨 에디터 Viewports처럼 여백 없이 창을 꽉 채운다.
	const bool bFullBleed = IsViewportPanel();
	ImGuiWindowFlags Flags = GetWindowFlags();
	if (bFullBleed)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		Flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}

	const bool bVisible = ImGui::Begin(GetWindowName(), GetOpenFlag(), Flags);
	// WindowPadding은 Begin 시점에만 읽히므로 바로 되돌려 안쪽 위젯(툴바 등)에는 기본값이 쓰이게 한다.
	if (bFullBleed)
	{
		ImGui::PopStyleVar();
	}
	if (bVisible)
	{
		DrawContent();
	}
	ImGui::End();
}

bool FAssetEditorPanelBase::IsDetached(ImGuiID DockSpaceId) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(GetWindowName());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != DockSpaceId);
}
