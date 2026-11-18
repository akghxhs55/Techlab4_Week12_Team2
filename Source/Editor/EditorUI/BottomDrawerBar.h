#pragma once

#include "Core/Types.h"

class IEditorPanel;
class FContentDrawerPanel;
class FOutputLogPanel;
struct ImVec2;

class FBottomDrawerBar
{
public:
	void SetPanels(FContentDrawerPanel* InContentDrawer, FOutputLogPanel* InOutputLog)
	{
		ContentPanel = InContentDrawer;
		LogPanel = InOutputLog;
	}

	void SetAllowDocking(bool bInAllowDocking) { bAllowDocking = bInAllowDocking; }

	bool HasPanels() const { return ContentPanel || LogPanel; }
	bool IsFocused() const { return bDrawerFocused; }

	static float GetHeight();

	void BeginFrame(bool bActive);

	void Draw(const ImVec2& TabWinPos);

private:
	enum class EBottomDrawer
	{
		None,
		ContentDrawer,
		OutputLog
	};

	bool IsDocked(const IEditorPanel* Panel) const;
	void DrawStatusBar(float Height, ImVec2& OutBarMin, ImVec2& OutBarMax, bool& OutHovered);
	void DrawOverlay(const ImVec2& TabWinPos, const ImVec2& BarMin, const ImVec2& BarMax, bool bBarHovered);

	FContentDrawerPanel* ContentPanel = nullptr;
	FOutputLogPanel* LogPanel = nullptr;
	EBottomDrawer ActiveDrawer = EBottomDrawer::None;
	float DrawerHeight = 480.0f;
	bool bAllowDocking = true;
	bool bDrawerOpenedThisFrame = false;
	bool bResizingDrawer = false;
	bool bDrawerFocused = false;
	char StatusCmdBuf[256] = {};
};
