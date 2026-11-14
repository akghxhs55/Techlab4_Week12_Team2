#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FSkeletalMeshEditor;

// SkeletalMesh 패널 베이스. 창 생성, 닫기 상태, 도킹은 다른 애셋 에디터와 같은 FAssetEditorPanelBase가 맡습니다.
class FSkeletalMeshEditorPanel : public TAssetEditorPanel<FSkeletalMeshEditor>
{
public:
	FSkeletalMeshEditorPanel(FSkeletalMeshEditor& InEditor, const char* InName);
};
