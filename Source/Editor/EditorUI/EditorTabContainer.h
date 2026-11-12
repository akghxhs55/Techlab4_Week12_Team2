#pragma once

#include "Containers/Array.h"
#include "Editor/EditorUI/EditorTab.h"
#include <functional>

// 탭은 각 편집기가 소유하며, 등록한 탭의 수명은 컨테이너보다 길어야 한다.
class FEditorTabContainer
{
public:
	// Window 메뉴에 아직 열지 않은 에디터의 실행 항목을 추가한다.
	std::function<void()> DrawEditorLaunchers;
	void AddTab(IEditorTab* Tab);

	void RemoveTab(IEditorTab* Tab);
	void ActivateTab(IEditorTab* Tab);
	void RequestCloseTab(IEditorTab* Tab);
	void ProcessCloseRequests();

	IEditorTab* GetMainSelectedTab() const { return MainSelectedTab; }
	IEditorTab* GetFocusedTab() const { return FocusedTab; }

	void DrawMainMenuBar();

	void OnRender(bool bPassthruCentralNode = false);

private:
	void DrawTabs();

	static bool IsInDockSpace(const ImGuiWindow* Window, ImGuiID DockSpaceId);

	void DrawTabMenu(IEditorTab& Tab);
	void DrawEditorTabsMenu();

	TArray<IEditorTab*> Tabs;
	TArray<IEditorTab*> PendingCloseTabs;
	IEditorTab* PendingActivation = nullptr;
	bool bRendering = false;
	IEditorTab* MainSelectedTab = nullptr;
	IEditorTab* FocusedTab = nullptr;
};
