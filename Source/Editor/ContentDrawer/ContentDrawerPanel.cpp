#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Render/SkeletalMesh.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Text/Font.h"
#include "Editor/KurisuEd/EditorDragDrop.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/AnimBlueprintFile.h"

namespace
{
	std::string ToLowerAscii(std::string_view Text)
	{
		std::string Result(Text);
		std::transform(Result.begin(), Result.end(), Result.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Result;
	}

	bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view LowerNeedle)
	{
		if (LowerNeedle.empty())
		{
			return true;
		}
		return ToLowerAscii(Haystack).find(LowerNeedle) != std::string::npos;
	}

	void DrawSearchBox(const char* WidgetId, const char* Hint, char* Buffer, size_t BufferSize, float Width, bool bFocus)
	{
		if (Width <= 0.0f)
		{
			return;
		}

		const ImTextureID SearchIcon = ViewportToolbarWidgets::GetIcon("Search");
		const float IconSize = SearchIcon ? EditorStyle::Scale(14.0f) : 0.0f;
		const float IconPadLeft = SearchIcon ? (IconSize + EditorStyle::Scale(8.0f)) : 0.0f;
		const ImGuiStyle& Style = ImGui::GetStyle();
		const bool bHasText = (Buffer[0] != '\0');
		const float ClearWidth = bHasText ? ImGui::GetFrameHeight() : 0.0f;
		const float InputWidth = (std::max)(EditorStyle::Scale(40.0f), Width - (bHasText ? ClearWidth + Style.ItemSpacing.x : 0.0f));

		ImGui::SetNextItemWidth(InputWidth);
		if (bFocus)
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			ImVec2(Style.FramePadding.x + IconPadLeft, Style.FramePadding.y));
		ImGui::InputTextWithHint(WidgetId, Hint, Buffer, BufferSize);
		ImGui::PopStyleVar();

		if (SearchIcon)
		{
			const ImVec2 BoxMin = ImGui::GetItemRectMin();
			const ImVec2 BoxMax = ImGui::GetItemRectMax();
			const float IconY = BoxMin.y + ((BoxMax.y - BoxMin.y) - IconSize) * 0.5f;
			const float IconX = BoxMin.x + Style.FramePadding.x + EditorStyle::Scale(2.0f);
			ImGui::GetWindowDrawList()->AddImage(
				SearchIcon,
				ImVec2(IconX, IconY),
				ImVec2(IconX + IconSize, IconY + IconSize),
				ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 1.0f),
				IM_COL32(180, 185, 195, 220));
		}

		if (bHasText)
		{
			ImGui::SameLine();
			std::string ClearId = std::string("X##Clear") + WidgetId;
			if (ImGui::Button(ClearId.c_str(), ImVec2(ClearWidth, ImGui::GetFrameHeight())))
			{
				Buffer[0] = '\0';
			}
		}
	}

	bool DrawHistoryButton(const char* WidgetId, bool bForward, bool bEnabled, const char* Tooltip)
	{
		const float BtnSize = ImGui::GetFrameHeight();
		ImGui::BeginDisabled(!bEnabled);
		const bool bClicked = ImGui::InvisibleButton(WidgetId, ImVec2(BtnSize, BtnSize));
		const bool bHovered = bEnabled && ImGui::IsItemHovered();
		const bool bActive = bEnabled && ImGui::IsItemActive();

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
		const float Radius = BtnSize * 0.38f;
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		if (bActive)
		{
			DrawList->AddCircleFilled(Center, Radius, IM_COL32(255, 255, 255, 45), 24);
		}
		else if (bHovered)
		{
			DrawList->AddCircleFilled(Center, Radius, IM_COL32(255, 255, 255, 26), 24);
		}

		const ImU32 InkCol = bEnabled
			? (bHovered ? IM_COL32(240, 242, 246, 255) : IM_COL32(205, 210, 218, 240))
			: IM_COL32(125, 130, 138, 100);
		const float Stroke = EditorStyle::Scale(1.5f);

		DrawList->AddCircle(Center, Radius, InkCol, 24, Stroke);

		const float Dir = bForward ? 1.0f : -1.0f;
		const float HalfShaft = Radius * 0.44f;
		const float HeadSize = Radius * 0.34f;
		const ImVec2 Tip(Center.x + Dir * HalfShaft, Center.y);
		const ImVec2 Tail(Center.x - Dir * HalfShaft, Center.y);

		DrawList->AddLine(Tail, Tip, InkCol, Stroke);
		DrawList->AddLine(Tip, ImVec2(Tip.x - Dir * HeadSize, Tip.y - HeadSize), InkCol, Stroke);
		DrawList->AddLine(Tip, ImVec2(Tip.x - Dir * HeadSize, Tip.y + HeadSize), InkCol, Stroke);

		ImGui::EndDisabled();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Tooltip);
		}

		return bEnabled && bClicked;
	}

	bool IsValidEntryName(std::string_view Name)
	{
		return !Name.empty()
			&& Name != "."
			&& Name != ".."
			&& Name.find_first_of("\\/:*?\"<>|") == std::string_view::npos;
	}

	bool IsPathOrDescendant(const fs::path& Candidate, const fs::path& Base)
	{
		if (Candidate == Base)
		{
			return true;
		}
		const fs::path Rel = Candidate.lexically_relative(Base);
		if (Rel.empty())
		{
			return false;
		}
		return *Rel.begin() != ".." && !Rel.is_absolute();
	}

	fs::path RebasePathPrefix(const fs::path& Original, const fs::path& OldPrefix, const fs::path& NewPrefix)
	{
		if (Original == OldPrefix)
		{
			return NewPrefix;
		}
		return NewPrefix / Original.lexically_relative(OldPrefix);
	}

	ImTextureID GetFolderIconTextureId(const FString& FolderIconPath)
	{
		if (UTexture2D* FolderTex = UAssetManager::GetAssetByPath<UTexture2D>(FolderIconPath))
		{
			if (FolderTex->GetResource() && FolderTex->GetResource()->GetSRV())
			{
				return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(FolderTex->GetResource()->GetSRV()));
			}
		}
		return 0;
	}

	void DrawRightChevron(ImDrawList* DrawList, ImVec2 Center, float Size, ImU32 Color)
	{
		const float HalfH = Size * 0.42f;
		const float HalfW = Size * 0.25f;
		const float Stroke = EditorStyle::Scale(1.3f);
		DrawList->AddLine(ImVec2(Center.x - HalfW, Center.y - HalfH), ImVec2(Center.x + HalfW, Center.y), Color, Stroke);
		DrawList->AddLine(ImVec2(Center.x + HalfW, Center.y), ImVec2(Center.x - HalfW, Center.y + HalfH), Color, Stroke);
	}

	void DrawTrashIcon(ImDrawList* DrawList, ImVec2 TopLeft, float Size, ImU32 Color)
	{
		const float Stroke = EditorStyle::Scale(1.25f);
		const float cx = TopLeft.x + Size * 0.5f;
		// 뚜껑 손잡이
		DrawList->AddLine(
			ImVec2(cx - Size * 0.16f, TopLeft.y + Size * 0.10f),
			ImVec2(cx + Size * 0.16f, TopLeft.y + Size * 0.10f),
			Color, Stroke);
		// 뚜껑 가로 막대
		DrawList->AddLine(
			ImVec2(cx - Size * 0.38f, TopLeft.y + Size * 0.24f),
			ImVec2(cx + Size * 0.38f, TopLeft.y + Size * 0.24f),
			Color, Stroke);
		// 통 몸체
		const ImVec2 BodyPts[4] = {
			ImVec2(cx - Size * 0.28f, TopLeft.y + Size * 0.28f),
			ImVec2(cx + Size * 0.28f, TopLeft.y + Size * 0.28f),
			ImVec2(cx + Size * 0.22f, TopLeft.y + Size * 0.88f),
			ImVec2(cx - Size * 0.22f, TopLeft.y + Size * 0.88f)
		};
		DrawList->AddPolyline(BodyPts, 4, Color, ImDrawFlags_Closed, Stroke);
		// 내부 세로 줄 2개
		DrawList->AddLine(
			ImVec2(cx - Size * 0.09f, TopLeft.y + Size * 0.42f),
			ImVec2(cx - Size * 0.07f, TopLeft.y + Size * 0.76f),
			Color, Stroke);
		DrawList->AddLine(
			ImVec2(cx + Size * 0.09f, TopLeft.y + Size * 0.42f),
			ImVec2(cx + Size * 0.07f, TopLeft.y + Size * 0.76f),
			Color, Stroke);
	}

	bool DrawDeleteMenuItem()
	{
		const float ItemW = ImMax(ImGui::GetContentRegionAvail().x, EditorStyle::Scale(125.0f));
		const float ItemH = ImGui::GetFrameHeight();
		const bool bClicked = ImGui::InvisibleButton("##DeleteMenuItem", ImVec2(ItemW, ItemH));
		const bool bHovered = ImGui::IsItemHovered();

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		const ImU32 BgCol = bHovered ? IM_COL32(190, 42, 42, 255) : IM_COL32(142, 30, 30, 235);
		DrawList->AddRectFilled(Min, Max, BgCol, EditorStyle::Scale(3.0f));

		const float IconSize = EditorStyle::Scale(13.0f);
		const float PadX = EditorStyle::Scale(6.0f);
		const float CenterY = (Min.y + Max.y) * 0.5f;
		const ImU32 WhiteCol = IM_COL32(255, 255, 255, 245);

		DrawTrashIcon(DrawList, ImVec2(Min.x + PadX, CenterY - IconSize * 0.5f), IconSize, WhiteCol);

		const float TextX = Min.x + PadX + IconSize + EditorStyle::Scale(6.0f);
		const float TextY = CenterY - ImGui::GetFontSize() * 0.5f;
		DrawList->AddText(ImVec2(TextX, TextY), WhiteCol, "Delete");

		if (bClicked)
		{
			ImGui::CloseCurrentPopup();
			return true;
		}
		return false;
	}

	bool DrawFolderTreeNode(const char* HiddenId, const char* Label, ImTextureID FolderTex, ImGuiTreeNodeFlags Flags, float* OutTextStartX = nullptr)
	{
		const float NodeStartX = ImGui::GetCursorScreenPos().x;
		const bool bIsOpen = ImGui::TreeNodeEx(HiddenId, Flags);

		const ImVec2 ItemMin = ImGui::GetItemRectMin();
		const ImVec2 ItemMax = ImGui::GetItemRectMax();
		const float RowCenterY = (ItemMin.y + ItemMax.y) * 0.5f;
		const float FontSize = ImGui::GetFontSize();
		float CursorX = NodeStartX + ImGui::GetTreeNodeToLabelSpacing();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->PushClipRect(ItemMin, ItemMax, true);

		if (FolderTex)
		{
			const float IconSize = FontSize;
			const float IconY = RowCenterY - IconSize * 0.5f;
			DrawList->AddImage(
				FolderTex,
				ImVec2(CursorX, IconY),
				ImVec2(CursorX + IconSize, IconY + IconSize));
			CursorX += IconSize + EditorStyle::Scale(5.0f);
		}

		if (OutTextStartX)
		{
			*OutTextStartX = CursorX;
		}

		if (Label && Label[0] != '\0')
		{
			DrawList->AddText(
				ImVec2(CursorX, RowCenterY - FontSize * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text),
				Label);
		}

		DrawList->PopClipRect();
		return bIsOpen;
	}
}

namespace
{
	// UTF-8 한 글자의 바이트 수 (한글 파일 이름도 글자 중간에서 자르지 않도록)
	int Utf8CharLength(unsigned char Lead)
	{
		if (Lead < 0x80) return 1;
		if ((Lead >> 5) == 0x6) return 2;
		if ((Lead >> 4) == 0xE) return 3;
		return 4;
	}

	// [Begin, End) 중 Width 안에 들어가는 가장 긴 앞부분의 끝을 찾는다. 최소 한 글자는 넣는다.
	const char* FitPrefix(const char* Begin, const char* End, float Width)
	{
		const char* Fit = Begin;
		const char* Cursor = Begin;
		while (Cursor < End)
		{
			const char* Next = std::min(End, Cursor + Utf8CharLength(static_cast<unsigned char>(*Cursor)));
			if (ImGui::CalcTextSize(Begin, Next).x > Width && Fit != Begin) break;
			Fit = Cursor = Next;
		}
		return Fit;
	}

	// UE 콘텐츠 브라우저처럼 이름을 칸 폭에 맞춰 최대 두 줄로 보여 주고, 넘치면 "..."으로 줄인다.
	// 이름 대부분이 밑줄로 이어져 공백이 없으므로 단어가 아니라 글자 단위로 줄을 바꾼다.
	void DrawTruncatedName(const std::string& Name, float Width)
	{
		const char* Begin = Name.c_str();
		const char* End = Begin + Name.size();
		if (Width <= 1.0f || ImGui::CalcTextSize(Begin, End).x <= Width)
		{
			ImGui::TextUnformatted(Begin, End);
			return;
		}
		const char* LineBreak = FitPrefix(Begin, End, Width);
		ImGui::TextUnformatted(Begin, LineBreak);

		bool bTruncated = false;
		if (ImGui::CalcTextSize(LineBreak, End).x <= Width)
		{
			ImGui::TextUnformatted(LineBreak, End);
		}
		else
		{
			const float EllipsisWidth = ImGui::CalcTextSize("...").x;
			const char* SecondEnd = FitPrefix(LineBreak, End, std::max(1.0f, Width - EllipsisWidth));
			const std::string SecondLine = std::string(LineBreak, SecondEnd) + "...";
			ImGui::TextUnformatted(SecondLine.c_str());
			bTruncated = true;
		}
		// 잘린 이름은 마우스를 올리면 전체를 보여 준다.
		if (bTruncated && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Name.c_str());
	}
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::OnRender()
{
	if (ImGui::Begin("Content Drawer", GetOpenFlag()))
	{
		RenderContents();
	}
	ImGui::End();
}

void FContentDrawerPanel::RenderContents()
{
	std::error_code Error;

	if (!fs::is_directory(CurrentPath, Error))
	{
		CurrentPath = AssetRootPath;
		PathHistory = { CurrentPath };
		PathHistoryIndex = 0;
	}

	Error.clear();

	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		return;
	}

	// 마우스 측면 이전/이후 버튼 지원 (콘텐츠 드로어에 호버된 경우)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(3)) && CanNavigateBack())
		{
			NavigateBack();
		}
		else if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(4)) && CanNavigateForward())
		{
			NavigateForward();
		}
	}

	// F2 단축키로 선택된 항목 인라인 이름 변경 시작
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput
		&& InlineRenameTarget.empty()
		&& ImGui::IsKeyPressed(ImGuiKey_F2))
	{
		std::error_code SelErr;
		if (!SelectedAssetPath.empty() && fs::exists(SelectedAssetPath, SelErr) && SelectedAssetPath.parent_path() == CurrentPath)
		{
			RequestRenameItem(SelectedAssetPath, bSelectedAssetIsDirectory, false);
		}
		else if (CurrentPath != AssetRootPath && fs::is_directory(CurrentPath, SelErr))
		{
			RequestRenameItem(CurrentPath, true, true);
		}
	}

	const float TotalAvailW = ImGui::GetContentRegionAvail().x;
	const float SplitterWidth = EditorStyle::Scale(4.0f);
	const float MinTreeWidth = ImMin(EditorStyle::Scale(140.0f), TotalAvailW * 0.45f);
	const float MaxTreeWidth = ImMax(MinTreeWidth, TotalAvailW * 0.65f);
	if (FolderTreeWidth <= 0.0f)
	{
		FolderTreeWidth = EditorStyle::Scale(210.0f);
	}
	FolderTreeWidth = ImClamp(FolderTreeWidth, MinTreeWidth, MaxTreeWidth);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.058f, 0.065f, 1.0f));
	ImGui::BeginChild("FolderTreeView", ImVec2(FolderTreeWidth, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	{
		// UE5 스타일 회색 헤더 바 (프로젝트 이름 + 경로 검색 토글)
		const float HeaderHeight = ImGui::GetFrameHeight() + EditorStyle::Scale(6.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(8.0f), EditorStyle::Scale(3.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.19f, 0.21f, 1.0f));
		if (ImGui::BeginChild("FolderTreeHeader", ImVec2(0.0f, HeaderHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar))
		{
			const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
			const float ChevronBoxW = EditorStyle::Scale(12.0f);
			ImGui::Dummy(ImVec2(ChevronBoxW, ImGui::GetFrameHeight()));
			ViewportToolbarWidgets::DrawChevron(
				ImGui::GetWindowDrawList(),
				ImVec2(HeaderPos.x + ChevronBoxW * 0.45f, HeaderPos.y + ImGui::GetFrameHeight() * 0.5f),
				EditorStyle::Scale(7.0f),
				ImGui::GetColorU32(ImGuiCol_Text));

			std::string ProjectLabel = "Kurisu";
			if (FPaths::HasProject())
			{
				const std::string Stem = FPaths::ToUtf8(FPaths::ProjectFilePath().stem());
				if (!Stem.empty())
				{
					ProjectLabel = Stem;
				}
				else if (!FPaths::ProjectName().empty())
				{
					ProjectLabel = FPaths::ProjectName();
				}
			}

			ImGui::SameLine(0.0f, EditorStyle::Scale(4.0f));
			ImGui::AlignTextToFramePadding();
			const float ToggleSize = ImGui::GetFrameHeight();
			const float TextStartLocalX = ImGui::GetCursorPosX();
			const float TextAvailW = ImGui::GetContentRegionAvail().x;
			const float ToggleLocalX = TextStartLocalX + ImMax(0.0f, TextAvailW - ToggleSize);
			const ImVec2 TextScreenPos = ImGui::GetCursorScreenPos();
			const float TextMaxScreenX = TextScreenPos.x + ImMax(0.0f, TextAvailW - ToggleSize - EditorStyle::Scale(4.0f));

			ImGui::PushClipRect(TextScreenPos, ImVec2(TextMaxScreenX, HeaderPos.y + HeaderHeight), true);
			ImGui::TextUnformatted(ProjectLabel.c_str());
			ImGui::PopClipRect();

			ImGui::SameLine(ToggleLocalX);

			const ImTextureID SearchIcon = ViewportToolbarWidgets::GetIcon("Search");
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			if (bShowPathSearch)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.43f, 0.85f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.50f, 0.92f, 1.0f));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.28f, 0.32f, 0.75f));
			}

			bool bToggleClicked = false;
			if (SearchIcon)
			{
				bToggleClicked = ImGui::ImageButton("##PathSearchToggle",
					SearchIcon,
					ImVec2(ToggleSize - EditorStyle::Scale(8.0f), ToggleSize - EditorStyle::Scale(8.0f)));
			}
			else
			{
				bToggleClicked = ImGui::Button("?##PathSearchToggle", ImVec2(ToggleSize, ToggleSize));
			}

			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar();

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Search folder paths");
			}

			if (bToggleClicked)
			{
				bShowPathSearch = !bShowPathSearch;
				if (bShowPathSearch)
				{
					bFocusPathSearch = true;
				}
				else
				{
					PathSearchBuf[0] = '\0';
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		if (bShowPathSearch)
		{
			ImGui::SetCursorPos(ImVec2(EditorStyle::Scale(6.0f), ImGui::GetCursorPosY() + EditorStyle::Scale(4.0f)));
			DrawSearchBox("##PathSearchInput", "Search Paths", PathSearchBuf, sizeof(PathSearchBuf),
				ImGui::GetContentRegionAvail().x - EditorStyle::Scale(6.0f), bFocusPathSearch);
			bFocusPathSearch = false;
		}

		// 검은색 배경의 폴더 하이라키 영역
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(4.0f), EditorStyle::Scale(6.0f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.058f, 0.065f, 1.0f));
		ImGui::BeginChild("FolderTreeScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		{
			// 루트 노드: 상단 경로 표시와 같도록 폴더 이름과 폴더 아이콘을 함께 보여 준다. (예: Content)
			const std::string RootName = FPaths::ToUtf8(AssetRootPath.filename());
			const ImTextureID FolderTex = GetFolderIconTextureId(FolderIconPath);
			const ImGuiTreeNodeFlags RootFlags =
				ImGuiTreeNodeFlags_DefaultOpen |
				ImGuiTreeNodeFlags_SpanAvailWidth |
				(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) |
				ImGuiTreeNodeFlags_DrawLinesToNodes;

			if (DrawFolderTreeNode("##AssetRoot", RootName.c_str(), FolderTex, RootFlags))
			{
				if (ImGui::IsItemClicked())
				{
					NavigateTo(AssetRootPath);
				}
				fs::path PendingTreeSelection = CurrentPath;
				RenderFolderTree(AssetRootPath, PendingTreeSelection);
				if (PendingTreeSelection != CurrentPath)
				{
					NavigateTo(PendingTreeSelection);
				}
				ImGui::TreePop();
			}
		}
		ImGui::EndChild();

		if (bPendingCommitInlineRename)
		{
			bPendingCommitInlineRename = false;
			TryCommitInlineRename();
		}
	}
	ImGui::EndChild();

	// 좌측 폴더 트리와 우측 에셋 그리드 사이 구분선 겸 드래그 스플리터
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::InvisibleButton("##ContentDrawerSplitter", ImVec2(SplitterWidth, -1.0f));
	const bool bSplitterHovered = ImGui::IsItemHovered();
	const bool bSplitterActive = ImGui::IsItemActive();
	if (bSplitterHovered || bSplitterActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	if (bSplitterActive)
	{
		FolderTreeWidth = ImClamp(FolderTreeWidth + ImGui::GetIO().MouseDelta.x, MinTreeWidth, MaxTreeWidth);
	}
	{
		const ImVec2 SplitMin = ImGui::GetItemRectMin();
		const ImVec2 SplitMax = ImGui::GetItemRectMax();
		const float MidX = (SplitMin.x + SplitMax.x) * 0.5f;
		const ImU32 LineCol = (bSplitterHovered || bSplitterActive)
			? IM_COL32(70, 125, 215, 255)
			: IM_COL32(28, 30, 35, 255);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(MidX, SplitMin.y), ImVec2(MidX, SplitMax.y), LineCol, EditorStyle::Scale(1.5f));
	}

	ImGui::SameLine(0.0f, 0.0f);

	// 우측 영역: 상단 툴바(Import, 뒤로/앞으로, 브레드크럼) + 검색창 + 에셋 그리드
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(8.0f), EditorStyle::Scale(6.0f)));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.132f, 0.148f, 1.0f));
	ImGui::BeginChild("AssetGridView", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	{
		RenderTopToolbar();

		const std::string CurrentFolderName = FPaths::ToUtf8(CurrentPath.filename());
		const std::string AssetSearchHint = CurrentFolderName.empty()
			? "Search Assets"
			: ("Search " + CurrentFolderName);
		DrawSearchBox("##AssetSearchInput", AssetSearchHint.c_str(), AssetSearchBuf, sizeof(AssetSearchBuf),
			ImGui::GetContentRegionAvail().x, false);
		ImGui::Spacing();

		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.118f, 0.132f, 1.0f));
		ImGui::BeginChild("AssetGridScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		{
		// 빈 곳 우클릭 (항목 위 우클릭은 제외)
		if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("New Folder"))
			{
				strcpy_s(NewAssetName, "NewFolder");
				bOpenCreateFolder = true;   // Create Widget과 같은 이유로 다음 줄에서 모달을 연다
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Create Widget"))
			{
				strcpy_s(NewAssetName, "NewWidget");
				bOpenCreateWidget = true;   // 메뉴 팝업 안에서 바로 다른 팝업을 열 수 없어 다음 줄에서 연다
			}
			ImGui::EndPopup();
		}
		if (bOpenCreateWidget) { ImGui::OpenPopup("Create Widget"); bOpenCreateWidget = false; }

		if (ImGui::BeginPopupModal("Create Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
			const fs::path Target = CurrentPath / (FString(NewAssetName) + ".widget");
			const bool bExists = fs::exists(Target);
			if (bExists) ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "A file with this name already exists.");

			ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0');
			if (ImGui::Button("Create") && OnCreateWidget)
			{
				OnCreateWidget(Target.generic_string());   // "Content/UI/GameOver.widget" 형식
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (bOpenCreateFolder) { ImGui::OpenPopup("New Folder"); bOpenCreateFolder = false; }

		if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			const bool bEnter = ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName), ImGuiInputTextFlags_EnterReturnsTrue);

			// 경로 구분자나 Windows에서 금지된 문자가 들어가면 만들지 않는다
			const FString FolderName = NewAssetName;
			const bool bInvalidName = FolderName.empty() || FolderName == "." || FolderName == ".."
				|| FolderName.find_first_of("\\/:*?\"<>|") != FString::npos;
			const fs::path Target = CurrentPath / FPaths::FromUtf8(FolderName);   // 한글 폴더 이름도 깨지지 않게
			std::error_code ExistsError;
			const bool bExists = !bInvalidName && fs::exists(Target, ExistsError);

			if (bExists) ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "A file or folder with this name already exists.");
			else if (bInvalidName && !FolderName.empty()) ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "Invalid folder name.");

			ImGui::BeginDisabled(bExists || bInvalidName);
			if (ImGui::Button("Create") || (bEnter && !bExists && !bInvalidName))
			{
				std::error_code CreateError;
				if (fs::create_directory(Target, CreateError))
				{
					ImGui::CloseCurrentPopup();
				}
				else
				{
					KRS_LOG(Warning, "Failed to create folder {} : {}", FPaths::ToUtf8(Target), CreateError.message());
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		// 그리드 레이아웃 계산 (FramePadding과 CellPadding을 포함해 오른쪽 테두리가 잘리지 않도록 계산)
		const ImGuiStyle& GridStyle = ImGui::GetStyle();
		const float padding = EditorStyle::Scale(12.0f);
		const float thumbnailSize = EditorStyle::Scale(80.0f);
		const float framePadX = GridStyle.FramePadding.x;
		const float framePadY = GridStyle.FramePadding.y;
		const float cellSize = thumbnailSize + framePadX * 2.0f + GridStyle.CellPadding.x * 2.0f + padding;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1) ColumnCount = 1;

		// 테이블 기반 그리드 시작
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			// 폴더를 열 때 멈추지 않도록 썸네일은 프레임당 하나씩 로드한다. 그동안은 파일 아이콘을 보여 준다.
			bool bLoadedThumbnail = false;

			auto ComputeItemImgSize = [thumbnailSize, framePadX]() -> float
			{
				const float MaxImg = ImGui::GetContentRegionAvail().x - framePadX * 2.0f - EditorStyle::Scale(2.0f);
				return ImMin(thumbnailSize, ImMax(EditorStyle::Scale(24.0f), MaxImg));
			};

			// 맨 앞 칸: ".." 를 더블클릭하면 상위 폴더로 간다. 루트에서는 보이지 않는다.
			bool bGoToParent = false;
			if (CurrentPath != AssetRootPath)
			{
				ImGui::TableNextColumn();
				ImGui::PushID("##ParentFolder");
				const float ItemImgSize = ComputeItemImgSize();
				if (UTexture2D* FolderIcon = UAssetManager::GetAssetByPath<UTexture2D>(FolderIconPath))
				{
					ImGui::ImageButton("##ParentFolderButton", (ImTextureID)FolderIcon->GetResource()->GetSRV(), { ItemImgSize, ItemImgSize });
				}
				else
				{
					ImGui::Button("##ParentFolderButton", { ItemImgSize + framePadX * 2.0f, ItemImgSize + framePadY * 2.0f });
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					bGoToParent = true;   // 순회 중인 폴더를 바꾸지 않도록 루프가 끝난 뒤 이동한다
				}
				ImGui::TextWrapped("..");
				ImGui::PopID();
			}

			const std::string AssetSearchLower = ToLowerAscii(AssetSearchBuf);

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
					continue;

				std::string PathString = Path.generic_string();
				std::string FilenameString = FPaths::ToUtf8(Path.filename());

				if (!ContainsCaseInsensitive(FilenameString, AssetSearchLower))
					continue;

				ImGui::TableNextColumn();

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FolderIconPath);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::FindLoadedAsset(PathString);
					if (!Asset && !bLoadedThumbnail)
					{
						// 메시는 썸네일로 쓰지 않으므로 드로어를 연 것만으로 로드하지 않는다
						const EAssetType UnloadedType = UAssetManager::GetUnloadedAssetType(PathString);
						if (UnloadedType == EAssetType::Texture || UnloadedType == EAssetType::Font)
						{
							Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
							bLoadedThumbnail = true;
						}
					}

					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}

					if (Thumbnail == nullptr)
					{
						Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FileIconPath);
					}
				}

				const float ItemImgSize = ComputeItemImgSize();
				const bool bIsSelected = (SelectedAssetPath == Path);
				const bool bRenamingThis = (!bInlineRenameInTree && InlineRenameTarget == Path);

				// UE5 스타일 파란색 선택 카드 하이라이트 (썸네일 + 아래 이름/입력창 포함)
				if (bIsSelected)
				{
					const ImVec2 TileStart = ImGui::GetCursorScreenPos();
					const float CardW = ItemImgSize + framePadX * 2.0f;
					const float ButtonH = ItemImgSize + framePadY * 2.0f;
					const bool bTwoLines = !bRenamingThis && (ImGui::CalcTextSize(FilenameString.c_str()).x > thumbnailSize);
					const float LabelH = bRenamingThis
						? ImGui::GetFrameHeight()
						: (ImGui::GetTextLineHeight() * (bTwoLines ? 2.0f : 1.0f) + (bTwoLines ? GridStyle.ItemSpacing.y : 0.0f));
					const float CardBottomY = TileStart.y + ButtonH + GridStyle.ItemSpacing.y + LabelH + EditorStyle::Scale(3.0f);
					ImGui::GetWindowDrawList()->AddRectFilled(
						ImVec2(TileStart.x - EditorStyle::Scale(2.0f), TileStart.y - EditorStyle::Scale(2.0f)),
						ImVec2(TileStart.x + CardW + EditorStyle::Scale(2.0f), CardBottomY),
						IM_COL32(14, 92, 214, 230),
						EditorStyle::Scale(4.0f));
				}

				if (bIsSelected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.36f, 0.80f, 0.65f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.44f, 0.90f, 0.85f));
				}

				if (Thumbnail)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)Thumbnail->GetResource()->GetSRV(), { ItemImgSize, ItemImgSize });
				}
				else
				{
					ImGui::Button("##FallbackAssetButton", { ItemImgSize + framePadX * 2.0f, ItemImgSize + framePadY * 2.0f });
				}

				if (bIsSelected)
				{
					ImGui::PopStyleColor(2);
				}

				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					SelectedAssetPath = Path;
					bSelectedAssetIsDirectory = bIsDirectory;
				}

				if (ImGui::BeginPopupContextItem("##AssetContext"))
				{
					if (!bIsDirectory)
					{
						URenderAsset* ContextAsset = UAssetManager::FindLoadedAsset(PathString);
						const bool bSkeletalMesh = (ContextAsset && ContextAsset->IsA<USkeletalMesh>())
							|| UAssetManager::GetUnloadedAssetType(PathString) == EAssetType::SkeletalMesh;
						if (bSkeletalMesh)
						{
							BlendSpaceCreator.DrawMenuItems(PathString, CurrentPath);
							AnimBlueprintCreator.DrawMenuItems(PathString, CurrentPath);
							ImGui::Separator();
						}
					}

					if (ImGui::MenuItem("Rename", "F2"))
					{
						RequestRenameItem(Path, bIsDirectory, false);
					}
					if (DrawDeleteMenuItem())
					{
						RequestDeleteItem(Path, bIsDirectory);
					}
					ImGui::EndPopup();
				}

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				// 더블 클릭 이벤트 처리
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (bIsDirectory)
					{
						NavigateTo(CurrentPath / Path.filename()); // 하위 폴더로 이동
					}

					// TODO: UIEditor를 FAssetEditor 계열로 통합
					else if (Path.extension() == ".widget" && OnOpenWidget)
					{
						OnOpenWidget(PathString);
					}

					else if (Path.extension() == ".scene" && OnOpenScene)
					{
						OnOpenScene(PathString);
					}

					else if ((Path.extension() == ".obj" || FFbxMeshBake::IsBakedPath(PathString)
						|| AnimSequenceFile::IsAnimSequencePath(PathString)) && OnOpenMesh)
					{
						OnOpenMesh(PathString);
					}

					else if ((BlendSpaceFile::IsBlendSpacePath(PathString) || AnimBlueprintFile::IsAnimBlueprintPath(PathString)) && OnOpenMesh)
					{
						OnOpenMesh(PathString);
					}

				}
				// 이름 표시 또는 폴더/파일 아래 인라인 이름 수정 입력창
				if (bRenamingThis)
				{
					ImGui::SetNextItemWidth(ItemImgSize + framePadX * 2.0f);
					if (bFocusInlineRename)
					{
						ImGui::SetKeyboardFocusHere();
						bFocusInlineRename = false;
					}
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.06f, 0.08f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.62f, 0.98f, 0.95f));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorStyle::Scale(3.0f));
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorStyle::Scale(4.0f), EditorStyle::Scale(2.0f)));
					const bool bEnter = ImGui::InputText("##InlineRename", RenameBuffer, sizeof(RenameBuffer),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
					ImGui::PopStyleVar(3);
					ImGui::PopStyleColor(2);

					if (ImGui::IsItemActive())
					{
						bInlineRenameWasActive = true;
					}
					if (bEnter)
					{
						bPendingCommitInlineRename = true;
					}
					else if (bInlineRenameWasActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
					{
						CancelInlineRename();
					}
					else if (bInlineRenameWasActive && !ImGui::IsItemActive())
					{
						bPendingCommitInlineRename = true;
					}
				}
				else
				{
					DrawTruncatedName(FilenameString, thumbnailSize);
				}

				ImGui::PopID();
			}
			ImGui::EndTable();

			if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				SelectedAssetPath.clear();
			}

			if (bPendingCommitInlineRename)
			{
				bPendingCommitInlineRename = false;
				TryCommitInlineRename();
			}

			if (bGoToParent)
			{
				NavigateTo(CurrentPath.parent_path());
			}
		}
		}
		ImGui::EndChild();
	}
	BlendSpaceCreator.ProcessCreateRequest();
    AnimBlueprintCreator.ProcessCreateRequest();

	ImGui::EndChild();

	RenderAssetModals();
}

void FContentDrawerPanel::RenderTopToolbar()
{
	if (ImGui::Button("Import"))
	{
		ImGui::OpenPopup("ImportAssetPopup");
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Import external mesh asset (FBX)");
	}

	const ImVec2 ImportMin = ImGui::GetItemRectMin();
	const ImVec2 ImportMax = ImGui::GetItemRectMax();
	ImGui::SetNextWindowPos(ImVec2(ImportMin.x, ImportMax.y + EditorStyle::Scale(2.0f)), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("ImportAssetPopup"))
	{
		if (ImGui::MenuItem("Import Static Mesh (FBX)..."))
		{
			if (OnImportFbx)
			{
				OnImportFbx(EFbxImportMode::Static);
			}
		}
		if (ImGui::MenuItem("Import Skeletal Mesh (FBX)..."))
		{
			if (OnImportFbx)
			{
				OnImportFbx(EFbxImportMode::Skeletal);
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (DrawHistoryButton("##HistoryBack", false, CanNavigateBack(), "Back"))
	{
		NavigateBack();
	}

	ImGui::SameLine(0.0f, EditorStyle::Scale(2.0f));
	if (DrawHistoryButton("##HistoryForward", true, CanNavigateForward(), "Forward"))
	{
		NavigateForward();
	}

	ImGui::SameLine();
	RenderNavigationBar();
}

void FContentDrawerPanel::RenderNavigationBar()
{
	struct FBreadcrumbSegment
	{
		std::string Label;
		fs::path TargetPath;
	};

	std::vector<FBreadcrumbSegment> Segments;
	Segments.push_back({ AssetRootPath.filename().string(), AssetRootPath });

	fs::path AccumPath = AssetRootPath;
	const fs::path Relative = CurrentPath.lexically_relative(AssetRootPath);
	for (const fs::path& Part : Relative)
	{
		if (Part.empty() || Part == "." || Part == "..")
		{
			continue;
		}
		AccumPath /= Part;
		Segments.push_back({ Part.string(), AccumPath });
	}

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.085f, 0.092f, 0.105f, 0.90f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(6.0f), 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(4.0f));
	if (ImGui::BeginChild("PathBreadcrumbBar", ImVec2(0.0f, ImGui::GetFrameHeight()),
		ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.27f, 0.33f, 0.70f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.34f, 0.42f, 0.90f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorStyle::Scale(3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorStyle::Scale(2.0f), 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorStyle::Scale(6.0f), ImGui::GetStyle().FramePadding.y));

		for (size_t Index = 0; Index < Segments.size(); ++Index)
		{
			if (Index > 0)
			{
				ImGui::SameLine();
				const ImVec2 ChevronPos = ImGui::GetCursorScreenPos();
				const float ChevronBoxW = EditorStyle::Scale(10.0f);
				const float FrameH = ImGui::GetFrameHeight();
				ImGui::Dummy(ImVec2(ChevronBoxW, FrameH));
				DrawRightChevron(
					ImGui::GetWindowDrawList(),
					ImVec2(ChevronPos.x + ChevronBoxW * 0.5f, ChevronPos.y + FrameH * 0.5f),
					EditorStyle::Scale(7.5f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::SameLine();
			}

			ImGui::PushID(static_cast<int>(Index));
			if (ImGui::Button(Segments[Index].Label.c_str()))
			{
				NavigateTo(Segments[Index].TargetPath);
			}
			ImGui::PopID();
		}

		ImGui::PopStyleVar(4);
		ImGui::PopStyleColor(3);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();
}

void FContentDrawerPanel::NavigateTo(const fs::path& NewPath)
{
	std::error_code Error;
	if (!fs::is_directory(NewPath, Error) || NewPath == CurrentPath)
	{
		return;
	}

	if (PathHistory.empty())
	{
		PathHistory.push_back(CurrentPath);
		PathHistoryIndex = 0;
	}

	if (PathHistoryIndex + 1 < PathHistory.size())
	{
		PathHistory.resize(PathHistoryIndex + 1);
	}

	PathHistory.push_back(NewPath);
	constexpr size_t MaxHistoryEntries = 64;
	if (PathHistory.size() > MaxHistoryEntries)
	{
		PathHistory.erase(PathHistory.begin());
	}
	PathHistoryIndex = PathHistory.size() - 1;
	CurrentPath = NewPath;
	SelectedAssetPath.clear();
	CancelInlineRename();
}

bool FContentDrawerPanel::CanNavigateBack() const
{
	return !PathHistory.empty() && PathHistoryIndex > 0;
}

bool FContentDrawerPanel::CanNavigateForward() const
{
	return !PathHistory.empty() && (PathHistoryIndex + 1) < PathHistory.size();
}

void FContentDrawerPanel::NavigateBack()
{
	if (!CanNavigateBack())
	{
		return;
	}

	size_t Candidate = PathHistoryIndex - 1;
	while (true)
	{
		std::error_code Error;
		if (fs::is_directory(PathHistory[Candidate], Error))
		{
			PathHistoryIndex = Candidate;
			CurrentPath = PathHistory[Candidate];
			SelectedAssetPath.clear();
			CancelInlineRename();
			return;
		}
		if (Candidate == 0)
		{
			return;
		}
		--Candidate;
	}
}

void FContentDrawerPanel::NavigateForward()
{
	if (!CanNavigateForward())
	{
		return;
	}

	size_t Candidate = PathHistoryIndex + 1;
	while (Candidate < PathHistory.size())
	{
		std::error_code Error;
		if (fs::is_directory(PathHistory[Candidate], Error))
		{
			PathHistoryIndex = Candidate;
			CurrentPath = PathHistory[Candidate];
			SelectedAssetPath.clear();
			CancelInlineRename();
			return;
		}
		++Candidate;
	}
}

bool FContentDrawerPanel::DoesSubtreeMatchPathFilter(const fs::path& Path, std::string_view LowerFilter) const
{
	if (LowerFilter.empty())
	{
		return true;
	}

	if (ContainsCaseInsensitive(Path.filename().string(), LowerFilter))
	{
		return true;
	}

	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();
		std::error_code TypeError;
		if (!TypeError && fs::is_directory(Entry, TypeError) && !TypeError)
		{
			if (DoesSubtreeMatchPathFilter(Entry, LowerFilter))
			{
				return true;
			}
		}
	}

	return false;
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	const std::string LowerFilter = ToLowerAscii(PathSearchBuf);

	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory)
			continue;

		const FString FilenameString =
			FPaths::ToUtf8(Entry.filename());

		if (!LowerFilter.empty() && !DoesSubtreeMatchPathFilter(Entry, LowerFilter))
			continue;

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
			Flags |= ImGuiTreeNodeFlags_Selected;

		if (!LowerFilter.empty())
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}

		const std::string NodeHiddenId = "##TreeFolder_" + Entry.generic_string();
		const ImTextureID FolderTex = GetFolderIconTextureId(FolderIconPath);
		const bool bRenamingInTree = (bInlineRenameInTree && InlineRenameTarget == Entry);
		float NodeTextX = 0.0f;
		const bool bIsOpen = DrawFolderTreeNode(
			NodeHiddenId.c_str(),
			bRenamingInTree ? "" : FilenameString.c_str(),
			FolderTex,
			Flags,
			&NodeTextX);

		const ImVec2 NodeRectMin = ImGui::GetItemRectMin();
		const ImVec2 NodeRectMax = ImGui::GetItemRectMax();

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Rename", "F2"))
			{
				RequestRenameItem(Entry, true, true);
			}
			if (DrawDeleteMenuItem())
			{
				RequestDeleteItem(Entry, true);
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemClicked() && !bRenamingInTree)
			SelectedPath = Entry;

		if (bRenamingInTree)
		{
			const ImVec2 BackupCursor = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(NodeTextX, NodeRectMin.y));
			const float InputW = ImMax(EditorStyle::Scale(64.0f), NodeRectMax.x - NodeTextX - EditorStyle::Scale(4.0f));
			ImGui::SetNextItemWidth(InputW);
			if (bFocusInlineRename)
			{
				ImGui::SetKeyboardFocusHere();
				bFocusInlineRename = false;
			}
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.62f, 0.98f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorStyle::Scale(3.0f), 0.0f));
			const bool bEnter = ImGui::InputText("##TreeInlineRename", RenameBuffer, sizeof(RenameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			if (ImGui::IsItemActive())
			{
				bInlineRenameWasActive = true;
			}
			if (bEnter)
			{
				bPendingCommitInlineRename = true;
			}
			else if (bInlineRenameWasActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				CancelInlineRename();
			}
			else if (bInlineRenameWasActive && !ImGui::IsItemActive())
			{
				bPendingCommitInlineRename = true;
			}
			ImGui::SetCursorScreenPos(BackupCursor);
		}

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}

void FContentDrawerPanel::RequestRenameItem(const fs::path& TargetPath, bool bIsDirectory, bool bInTree)
{
	InlineRenameTarget = TargetPath;
	bInlineRenameIsDirectory = bIsDirectory;
	bInlineRenameInTree = bInTree;
	bFocusInlineRename = true;
	bInlineRenameWasActive = false;
	bPendingCommitInlineRename = false;
	FileOpErrorMessage.clear();
	const std::string CurrentName = FPaths::ToUtf8(TargetPath.filename());
	strcpy_s(RenameBuffer, CurrentName.c_str());
}

void FContentDrawerPanel::CancelInlineRename()
{
	InlineRenameTarget.clear();
	bFocusInlineRename = false;
	bInlineRenameWasActive = false;
	bPendingCommitInlineRename = false;
}

void FContentDrawerPanel::TryCommitInlineRename()
{
	if (InlineRenameTarget.empty())
	{
		return;
	}

	const std::string_view NewNameView(RenameBuffer);
	const bool bInvalidName = !IsValidEntryName(NewNameView);
	const fs::path TargetPath = InlineRenameTarget.parent_path() / FPaths::FromUtf8(RenameBuffer);
	std::error_code ExistsError;
	const bool bSameName = (TargetPath == InlineRenameTarget);
	const bool bExists = !bInvalidName && !bSameName && fs::exists(TargetPath, ExistsError);

	if (!bInvalidName && !bSameName && !bExists)
	{
		std::error_code RenameError;
		fs::rename(InlineRenameTarget, TargetPath, RenameError);
		if (!RenameError)
		{
			if (SelectedAssetPath == InlineRenameTarget)
			{
				SelectedAssetPath = TargetPath;
			}
			if (bInlineRenameIsDirectory)
			{
				if (IsPathOrDescendant(CurrentPath, InlineRenameTarget))
				{
					CurrentPath = RebasePathPrefix(CurrentPath, InlineRenameTarget, TargetPath);
				}
				for (fs::path& HistoryEntry : PathHistory)
				{
					if (IsPathOrDescendant(HistoryEntry, InlineRenameTarget))
					{
						HistoryEntry = RebasePathPrefix(HistoryEntry, InlineRenameTarget, TargetPath);
					}
				}
			}
		}
		else
		{
			KRS_LOG(Warning, "Failed to rename {} -> {} : {}", FPaths::ToUtf8(InlineRenameTarget), FPaths::ToUtf8(TargetPath), RenameError.message());
		}
	}

	CancelInlineRename();
}

void FContentDrawerPanel::RequestDeleteItem(const fs::path& TargetPath, bool bIsDirectory)
{
	CancelInlineRename();
	PendingItemTarget = TargetPath;
	PendingItemIsDirectory = bIsDirectory;
	FileOpErrorMessage.clear();
	bOpenDeleteModal = true;
}

void FContentDrawerPanel::RenderAssetModals()
{
	if (bOpenDeleteModal)
	{
		ImGui::OpenPopup("Delete");
		bOpenDeleteModal = false;
	}

	if (ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const std::string TargetName = FPaths::ToUtf8(PendingItemTarget.filename());
		if (PendingItemIsDirectory)
		{
			ImGui::Text("Delete folder '%s' and all of its contents from disk?", TargetName.c_str());
		}
		else
		{
			ImGui::Text("Delete file '%s' from disk?", TargetName.c_str());
		}
		ImGui::TextDisabled("This action cannot be undone.");

		if (!FileOpErrorMessage.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", FileOpErrorMessage.c_str());
		}

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68f, 0.16f, 0.16f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.22f, 0.22f, 1.0f));
		if (ImGui::Button("Delete"))
		{
			std::error_code RemoveError;
			fs::remove_all(PendingItemTarget, RemoveError);
			if (!RemoveError)
			{
				if (!SelectedAssetPath.empty() && IsPathOrDescendant(SelectedAssetPath, PendingItemTarget))
				{
					SelectedAssetPath.clear();
				}
				if (IsPathOrDescendant(CurrentPath, PendingItemTarget))
				{
					fs::path Fallback = PendingItemTarget.parent_path();
					std::error_code DirErr;
					if (!fs::is_directory(Fallback, DirErr) || !IsPathOrDescendant(Fallback, AssetRootPath))
					{
						Fallback = AssetRootPath;
					}
					CurrentPath = Fallback;
				}

				for (size_t i = PathHistory.size(); i > 0; --i)
				{
					const size_t Idx = i - 1;
					std::error_code HistErr;
					if (IsPathOrDescendant(PathHistory[Idx], PendingItemTarget) || !fs::is_directory(PathHistory[Idx], HistErr))
					{
						PathHistory.erase(PathHistory.begin() + static_cast<ptrdiff_t>(Idx));
						if (PathHistoryIndex >= Idx && PathHistoryIndex > 0)
						{
							--PathHistoryIndex;
						}
					}
				}
				if (PathHistory.empty())
				{
					PathHistory.push_back(CurrentPath);
					PathHistoryIndex = 0;
				}
				else if (PathHistoryIndex >= PathHistory.size())
				{
					PathHistoryIndex = PathHistory.size() - 1;
				}

				ImGui::CloseCurrentPopup();
			}
			else
			{
				FileOpErrorMessage = RemoveError.message();
				KRS_LOG(Warning, "Failed to delete {} : {}", FPaths::ToUtf8(PendingItemTarget), FileOpErrorMessage);
			}
		}
		ImGui::PopStyleColor(2);

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
