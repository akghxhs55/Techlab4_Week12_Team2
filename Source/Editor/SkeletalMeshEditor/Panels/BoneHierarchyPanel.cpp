#include "EnginePCH.h"
#include "BoneHierarchyPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "Runtime/Render/SkeletalMeshData.h"
#include "Editor/EditorUI/EditorStyle.h"

FBoneHierarchyPanel::FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Bone Hierarchy")
{
}

void FBoneHierarchyPanel::DrawContent()
{
	DrawTree(Editor, SearchBuffer, sizeof(SearchBuffer));
}

void FBoneHierarchyPanel::DrawTree(FSkeletalPreviewEditor& InEditor, char* InSearchBuffer, size_t InSearchBufferSize)
{
	if (!InEditor.GetMesh())
	{
		return;
	}

	// 레벨 에디터 아웃라이너처럼 맨 위에 검색창을 둔다.
	EditorStyle::SearchBox("##BoneSearch", InSearchBuffer, InSearchBufferSize);

	if (ImGui::BeginChild("BoneHierarchyChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, EditorStyle::Scale(12.0f));

		for (int32 RootBoneIndex : InEditor.GetRootBoneIndices())
		{
			if (SubtreeMatches(InEditor, RootBoneIndex, InSearchBuffer))
			{
				DrawBoneHierarchy(InEditor, RootBoneIndex, InSearchBuffer);
			}
		}

		ImGui::PopStyleVar();
	}

	ImGui::EndChild();
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

void FBoneHierarchyPanel::DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const char* Filter)
{
	ImGuiTreeNodeFlags Flag =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	const TArray<FEditorBoneItem>& BoneItems = InEditor.GetBoneItems();

	int32 ChildBoneCount = BoneItems[BoneIndex].ChildBoneIndices.Num();
	if (ChildBoneCount == 0)
	{
		Flag |= ImGuiTreeNodeFlags_Leaf;
	}

	if (InEditor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushID(BoneIndex);

	// 표시 이름과 ID를 분리하여 축약된 이름이 겹쳐도 구분되게 함
	const FString& BoneName = BoneItems[BoneIndex].Name;

	// 검색 중에는 찾은 자손이 보이도록 펼친다.
	const bool bFiltering = Filter && *Filter;
	if (bFiltering && ChildBoneCount > 0)
	{
		ImGui::SetNextItemOpen(true);
	}

	// 접힌 노드도 선택할 수 있도록 펼침 상태와 선택 처리를 분리
	const bool bOpen = ImGui::TreeNodeEx("Bone", Flag, "%s", BoneName.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		InEditor.SelectBone(BoneIndex);
	}

	if (bOpen)
	{
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			if (SubtreeMatches(InEditor, ChildIndex, Filter))
			{
				DrawBoneHierarchy(InEditor, ChildIndex, Filter);
			}
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}
