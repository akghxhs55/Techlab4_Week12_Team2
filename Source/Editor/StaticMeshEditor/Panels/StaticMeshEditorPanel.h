#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FStaticMeshEditor;

// StaticMesh 패널 베이스. 창 생성, 닫기 상태, 도킹은 다른 애셋 에디터와 같은 FAssetEditorPanelBase가 맡습니다.
class FStaticMeshEditorPanel : public TAssetEditorPanel<FStaticMeshEditor>
{
public:
	FStaticMeshEditorPanel(FStaticMeshEditor& InEditor, const char* InName);
};
