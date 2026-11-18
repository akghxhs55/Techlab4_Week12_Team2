#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Containers/Array.h"

// Unreal 애니메이션 뷰어의 Skeleton Tree. 스켈레탈 메시 에디터의 Bone Hierarchy와 같은 트리를 그린다.
// 본을 고르면 뷰포트에서 강조되고, 뷰포트에서 본을 클릭하면 여기서도 선택된다(FSkeletalPreviewEditor가 선택을 공유).
class FAnimSkeletonTreePanel : public TAssetEditorPanel<FAnimSequenceEditor>
{
public:
    explicit FAnimSkeletonTreePanel(FAnimSequenceEditor& InEditor)
        : TAssetEditorPanel<FAnimSequenceEditor>(InEditor, "Skeleton Tree", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;

private:
    char SearchBuffer[128] = {};
    int32 LastSelectedBone = -1;
};

struct FAnimAssetEntry
{
    FString FullPath;
    FString DisplayName;
    FString DisplayPath;
    FString SizeText;
};

// 같은 스켈레탈 메시용 애니메이션 시퀀스(.anim.bin)를 검색/필터링해 표 형태로 그리는 공용 뷰
struct FAnimAssetBrowserView
{
    void Refresh(const FString& PreviewMeshPath);
    // 애니메이션을 더블클릭하면 해당 경로를 OutOpenedPath에 채우고 true를 반환한다.
    bool Draw(const FString& PreviewMeshPath, const FString& CurrentAnimPath, bool bShowDirtyNotice, FString& OutOpenedPath);

    TArray<FAnimAssetEntry> Entries;
    FString SelectedPath;
    FString LastPreviewMeshPath;
    bool bScanned = false;
    char SearchBuffer[128] = {};
};

// Unreal 애니메이션 뷰어의 Asset Browser.
// 지금 애니메이션과 같은 미리보기 메시를 쓰는 애니메이션만 보여 주고, 더블클릭하면 이 뷰어에서 그 애니메이션으로 바꾼다.
class FAnimAssetBrowserPanel : public TAssetEditorPanel<FAnimSequenceEditor>
{
public:
    explicit FAnimAssetBrowserPanel(FAnimSequenceEditor& InEditor)
        : TAssetEditorPanel<FAnimSequenceEditor>(InEditor, "Asset Browser", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;

private:
    FAnimAssetBrowserView BrowserView;
};
