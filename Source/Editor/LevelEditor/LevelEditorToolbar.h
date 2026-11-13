#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/EditorUI/EditorCommand.h"
#include "Editor/HitoriEd/PlayInEditorDataTypes.h"

class FLevelEditorToolbar
{
public:
	~FLevelEditorToolbar() = default;

	void SetCommands(const TArray<FEditorCommand>& InCommands) { Commands = InCommands; }
	// Play 그룹 끝에 PIE 실행 위치 선택 메뉴를 붙인다. 엔진이 상태를 소유한다.
	void SetPlayModeBinding(std::function<EPlayModeType()> InGetPlayMode,
		std::function<void(EPlayModeType)> InSetPlayMode, std::function<bool()> InCanChangePlayMode)
	{
		GetPlayMode = std::move(InGetPlayMode);
		SetPlayMode = std::move(InSetPlayMode);
		CanChangePlayMode = std::move(InCanChangePlayMode);
	}
	// UE 메인 툴바의 Add(액터 배치)와 그리드 스냅 드롭다운. 메뉴 내용은 뷰포트 패널이 그린다.
	void SetPlacementMenus(std::function<void()> InDrawAddMenu, std::function<void()> InDrawGridMenu,
		std::function<int32()> InGetGridSpacing)
	{
		DrawAddMenu = std::move(InDrawAddMenu);
		DrawGridMenu = std::move(InDrawGridMenu);
		GetGridSpacing = std::move(InGetGridSpacing);
	}
	void Draw();

	static constexpr float Height = 40.0f;

private:
	void DrawPlacementGroup();
	void DrawPlayGroup();
	void DrawPlayModeMenu();

	bool bIsOpen = true;
	TArray<FEditorCommand> Commands;
	std::function<EPlayModeType()> GetPlayMode;
	std::function<void(EPlayModeType)> SetPlayMode;
	std::function<bool()> CanChangePlayMode;
	std::function<void()> DrawAddMenu;
	std::function<void()> DrawGridMenu;
	std::function<int32()> GetGridSpacing;


};