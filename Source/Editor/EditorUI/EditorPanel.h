#pragma once

#include <imgui.h>
#include <imgui_internal.h>

// 편집기 탭이 패널을 자기 도킹 공간 안에서만 도킹시키고 Window 메뉴로 여닫게 하는 공용 코드.
// 도킹 공간과 그 안의 패널이 같은 ClassId를 써야 서로 도킹된다.
namespace EditorDock
{
	// 도킹 공간에 넘긴다. 클래스가 없는 창과 다른 탭의 패널은 들어오지 못한다.
	inline ImGuiWindowClass MakeDockSpaceClass(ImGuiID ClassId)
	{
		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = ClassId;
		WindowClass.DockingAllowUnclassed = false;
		return WindowClass;
	}

	// 패널 창의 Begin 앞에서 SetNextWindowClass로 넘긴다. 패널은 Window 메뉴로만 여닫는다.
	inline ImGuiWindowClass MakePanelClass(ImGuiID ClassId)
	{
		ImGuiWindowClass WindowClass = MakeDockSpaceClass(ClassId);
		WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
		return WindowClass;
	}

	// 탭의 Window 메뉴. 등록된 패널을 모두 나열하고 누르면 여닫는다.
	template <typename TPanels>
	void DrawWindowMenu(TPanels& Panels)
	{
		if (!ImGui::BeginMenu("Window"))
			return;

		for (auto& Panel : Panels)
		{
			const bool bOpen = Panel->IsOpen();
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
				Panel->SetOpen(!bOpen);
		}
		ImGui::EndMenu();
	}
}

class IEditorPanel
{
public:
	virtual ~IEditorPanel() = default;
	virtual bool Init() = 0;
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnRender() = 0;

	bool IsOpen() const { return bIsOpen; }
	void SetOpen(bool bOpen) { bIsOpen = bOpen; }
	virtual const char* GetPanelName() const = 0;

private:
	bool bIsOpen = true;
};