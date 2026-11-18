#include "EnginePCH.h"
#include "BoneHierarchyPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "Runtime/Render/SkeletalMeshData.h"
#include "Editor/EditorUI/EditorStyle.h"

namespace
{
	void DrawBoneIcon(ImDrawList* DL, ImVec2 Center, float Size, ImU32 Color)
	{
		const float Half = Size * 0.5f;
		const ImVec2 P1(Center.x - Half * 0.7f, Center.y + Half * 0.7f);
		const ImVec2 P2(Center.x + Half * 0.7f, Center.y - Half * 0.7f);
		const float Thickness = EditorStyle::Scale(1.6f);
		const float R = EditorStyle::Scale(1.4f);
		const float Offset = EditorStyle::Scale(1.2f);
		DL->AddLine(P1, P2, Color, Thickness);
		DL->AddCircleFilled(ImVec2(P1.x - Offset, P1.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P1.x + Offset, P1.y + Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x - Offset, P2.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x + Offset, P2.y + Offset), R, Color);
	}

	void DrawDotIcon(ImDrawList* DL, ImVec2 Center, float Radius, ImU32 Color)
	{
		DL->AddCircleFilled(Center, Radius, Color);
	}
}

FBoneHierarchyPanel::FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Bone Hierarchy")
{
}

void FBoneHierarchyPanel::DrawContent()
{
	DrawTree(Editor, SearchBuffer, sizeof(SearchBuffer), &LastSelectedBone);
}

void FBoneHierarchyPanel::DrawTree(FSkeletalPreviewEditor& InEditor, char* InSearchBuffer, size_t InSearchBufferSize,
	int32* InOutLastSelectedBone)
{
	if (!InEditor.GetMesh())
	{
		return;
	}

	// 레벨 에디터 아웃라이너처럼 맨 위에 검색창을 둔다.
	EditorStyle::SearchBox("##BoneSearch", InSearchBuffer, InSearchBufferSize);

	if (InEditor.GetRootBoneIndices().IsEmpty())
	{
		ImGui::TextDisabled("No bones");
		return;
	}

	bool bScrollToSelection = false;
	const int32 CurrentSelected = InEditor.GetSelectedBoneIndex();
	if (InOutLastSelectedBone && *InOutLastSelectedBone != CurrentSelected)
	{
		*InOutLastSelectedBone = CurrentSelected;
		bScrollToSelection = (CurrentSelected >= 0);
	}

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();
	TArray<uint8> AncestorMask;
	AncestorMask.Init(0, BoneItems.Num());
	if (CurrentSelected >= 0 && CurrentSelected < BoneItems.Num())
	{
		TArray<int32> ParentIndex;
		ParentIndex.Init(-1, BoneItems.Num());
		for (int32 i = 0; i < BoneItems.Num(); ++i)
		{
			for (int32 ChildIdx : BoneItems[i].ChildBoneIndices)
			{
				if (ChildIdx >= 0 && ChildIdx < BoneItems.Num())
				{
					ParentIndex[ChildIdx] = i;
				}
			}
		}

		int32 Cursor = ParentIndex[CurrentSelected];
		while (Cursor >= 0 && Cursor < BoneItems.Num() && AncestorMask[Cursor] == 0)
		{
			AncestorMask[Cursor] = 1;
			Cursor = ParentIndex[Cursor];
		}
	}

	ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.11f, 0.11f, 0.11f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TreeLines, ImVec4(0.45f, 0.45f, 0.48f, 1.0f));
	// TableSetBgColor로 전체 행 배경을 통일하므로 TreeNode 자체의 부분 사각형(Header)은 투명 처리한다.
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##BoneTreeTable", 1, TableFlags))
	{
		// IndentSpacing이 FontSize*0.5 + ItemInnerSpacing.x 보다 작으면 ImGui가 트리 가로 연결선(x1 >= x2)을 생략한다.
		const float MinIndent = ImGui::GetFontSize() * 0.5f + ImGui::GetStyle().ItemInnerSpacing.x + EditorStyle::Scale(7.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, std::max(EditorStyle::Scale(20.0f), MinIndent));

		for (int32 RootBoneIndex : InEditor.GetRootBoneIndices())
		{
			if (SubtreeMatches(InEditor, RootBoneIndex, InSearchBuffer))
			{
				DrawBoneHierarchy(InEditor, RootBoneIndex, InSearchBuffer, AncestorMask, InOutLastSelectedBone, bScrollToSelection);
			}
		}

		ImGui::PopStyleVar();

		// 빈 곳을 누르면 선택을 푼다.
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
		{
			InEditor.SelectBone(-1);
			if (InOutLastSelectedBone)
			{
				*InOutLastSelectedBone = -1;
			}
		}

		ImGui::EndTable();
	}
	ImGui::PopStyleColor(6);
}

bool FBoneHierarchyPanel::SubtreeMatches(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const char* Filter)
{
	if (!Filter || !*Filter)
	{
		return true;
	}

	const FEditorBoneItem& Item = InEditor.GetBoneItems()[BoneIndex];
	if (EditorStyle::ContainsIgnoreCase(Item.Name.c_str(), Filter))
	{
		return true;
	}
	for (int32 ChildIndex : Item.ChildBoneIndices)
	{
		if (SubtreeMatches(InEditor, ChildIndex, Filter))
		{
			return true;
		}
	}
	return false;
}

void FBoneHierarchyPanel::DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const char* Filter,
	const TArray<uint8>& AncestorMask, int32* InOutLastSelectedBone, bool& bScrollToSelection)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	ImGui::PushID(BoneIndex);

	const bool bSelected = (InEditor.GetSelectedBoneIndex() == BoneIndex);
	const bool bIsAncestor = !bSelected && (BoneIndex >= 0 && BoneIndex < AncestorMask.Num() && AncestorMask[BoneIndex] != 0);
	const bool bRowHovered = (ImGui::GetHoveredID() == ImGui::GetID("Bone"));

	if (bSelected)
	{
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, EditorColor::WithAlpha(EditorColor::Accent, 210));
	}
	else if (bIsAncestor)
	{
		// 선택된 본의 조상 행은 어둡고 옅은 적갈색 톤으로 표시한다.
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(66, 36, 35, 225));
	}
	else if (bRowHovered)
	{
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, EditorColor::WithAlpha(EditorColor::BgAccentHover, 210));
	}

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();
	const int32 ChildBoneCount = BoneItems[BoneIndex].ChildBoneIndices.Num();
	const bool bLeaf = (ChildBoneCount == 0);

	ImGuiTreeNodeFlags Flag =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DrawLinesToNodes;

	if (bLeaf)
	{
		Flag |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (bSelected)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}

	// 표시 이름과 ID를 분리하여 축약된 이름이 겹쳐도 구분되게 함
	const FString& BoneName = BoneItems[BoneIndex].Name;

	// 검색 중에는 찾은 자손이 보이도록 펼친다.
	const bool bFiltering = Filter && *Filter;
	if (bFiltering && !bLeaf)
	{
		ImGui::SetNextItemOpen(true);
	}

	bool bIsSocketOrLeaf = bLeaf;
	if (BoneName.find("_end") != FString::npos ||
		BoneName.find("Socket") != FString::npos ||
		BoneName.find("Cor_") != FString::npos)
	{
		bIsSocketOrLeaf = true;
	}

	// 아이콘과 이름 간격이 폰트에 따라 겹치지 않도록 라벨은 직접 배치한다.
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flag, "");

	ImDrawList* DL = ImGui::GetWindowDrawList();
	const ImVec2 ItemMin = ImGui::GetItemRectMin();
	const ImVec2 ItemMax = ImGui::GetItemRectMax();
	const float LabelAreaStartX = ItemMin.x + ImGui::GetTreeNodeToLabelSpacing();
	// 리프 노드는 화살표가 없으므로 트리 가로선 끝 바로 뒤에 점을 배치해 과도한 빈 여백을 없앤다.
	const float IconCenterX = bLeaf
		? (ItemMin.x + ImGui::GetStyle().FramePadding.x + EditorStyle::Scale(4.0f))
		: (LabelAreaStartX + EditorStyle::Scale(6.0f));
	const ImVec2 IconCenter(IconCenterX, (ItemMin.y + ItemMax.y) * 0.5f);
	const ImU32 IconColor = bSelected
		? IM_COL32(14, 14, 16, 255)
		: (bIsAncestor ? IM_COL32(205, 205, 205, 235) : IM_COL32(180, 180, 180, 220));

	if (bIsSocketOrLeaf)
	{
		DrawDotIcon(DL, IconCenter, EditorStyle::Scale(2.2f), IconColor);
	}
	else
	{
		DrawBoneIcon(DL, IconCenter, EditorStyle::Scale(8.0f), IconColor);
	}

	const float TextX = bLeaf
		? (IconCenterX + EditorStyle::Scale(8.0f))
		: (LabelAreaStartX + EditorStyle::Scale(bIsSocketOrLeaf ? 13.0f : 17.0f));
	const float TextY = (ItemMin.y + ItemMax.y - ImGui::GetTextLineHeight()) * 0.5f;
	const ImVec2 ClipMax(std::max(TextX + 1.0f, ItemMax.x - EditorStyle::Scale(4.0f)), ItemMax.y);
	DL->PushClipRect(ImVec2(TextX, ItemMin.y), ClipMax, true);
	DL->AddText(ImVec2(TextX, TextY), ImGui::GetColorU32(ImGuiCol_Text), BoneName.c_str());
	DL->PopClipRect();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		InEditor.SelectBone(BoneIndex);
		if (InOutLastSelectedBone)
		{
			*InOutLastSelectedBone = BoneIndex;
		}
	}

	if (bScrollToSelection && bSelected)
	{
		ImGui::SetScrollHereY(0.5f);
		bScrollToSelection = false;
	}

	if (bOpen && !bLeaf)
	{
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			if (SubtreeMatches(InEditor, ChildIndex, Filter))
			{
				DrawBoneHierarchy(InEditor, ChildIndex, Filter, AncestorMask, InOutLastSelectedBone, bScrollToSelection);
			}
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}
