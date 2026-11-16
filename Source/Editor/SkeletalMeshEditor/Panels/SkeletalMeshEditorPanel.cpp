#include "EnginePCH.h"
#include "SkeletalMeshEditorPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"

// 창 ID 형식("이름###SkeletalMeshPanel:이름:경로")은 그대로 유지되어 저장된 도킹 배치가 깨지지 않습니다.
FSkeletalMeshEditorPanel::FSkeletalMeshEditorPanel(FSkeletalMeshEditor& InEditor, const char* InName)
	: TAssetEditorPanel<FSkeletalMeshEditor>(InEditor, InName, "SkeletalMeshPanel")
{
}
