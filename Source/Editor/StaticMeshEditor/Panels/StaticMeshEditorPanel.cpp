#include "EnginePCH.h"
#include "StaticMeshEditorPanel.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"

// 창 ID 형식("이름###StaticMeshPanel:이름:경로")은 그대로 유지되어 저장된 도킹 배치가 깨지지 않습니다.
FStaticMeshEditorPanel::FStaticMeshEditorPanel(FStaticMeshEditor& InEditor, const char* InName)
	: TAssetEditorPanel<FStaticMeshEditor>(InEditor, InName, "StaticMeshPanel")
{
}
