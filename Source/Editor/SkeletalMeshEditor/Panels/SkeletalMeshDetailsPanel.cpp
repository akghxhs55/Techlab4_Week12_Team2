#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Editor/Details/PropertyEditor.h"

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();

	if (ImGui::Button("Reset Transform"))
	{
		Editor.ResetBone(BoneIndex);
	}

	ImGui::Separator();

	FTransform Transform;
	if (!Editor.GetSelectedBoneLocalTransform(Transform))
	{
		ImGui::TextDisabled("Select a bone in Bone Hierarchy.");
		return;
	}



	// 선택이 바뀌면 이전 본의 편집 상태가 새 본에 이어지지 않도록 ID를 구분합니다.
	ImGui::PushID(BoneIndex);
	bool bChanged = false;
	// 레벨 디테일 패널과 같은 X/Y/Z 축 컨트롤을 쓴다 (축 버튼을 누르면 그 축만 초기화).
	bChanged |= FPropertyEditor::DrawVector3("Location", Transform.Location.V, 0.0f);
	// 드래그 중 값이 튀지 않도록 마지막 오일러 값을 기억한다
	static FRotationConversionCache RotationCache;
	FRotator Euler = RotationCache.QuatToRotator(Transform.Rotation);
	if (FPropertyEditor::DrawRotator("Rotation", Euler))
	{
		Transform.Rotation = RotationCache.RotatorToQuat(Euler);
		bChanged = true;
	}
	bChanged |= FPropertyEditor::DrawVector3("Scale", Transform.Scale.V, 1.0f);
	ImGui::PopID();

	if (bChanged)
	{
		Editor.SetSelectedBoneLocalTransform(Transform);
	}
}
