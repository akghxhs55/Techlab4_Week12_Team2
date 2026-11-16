#pragma once

#include "SkeletalMeshEditorPanel.h"
#include "Containers/Array.h"

class FSkeletalPreviewEditor;

// 스켈레탈 메시의 본 계층을 표시하는 패널입니다.
class FBoneHierarchyPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor);

	// 맨 위 검색창과 본 계층 트리를 그린다. 애니메이션 뷰어의 Skeleton Tree도 같은 함수를 쓴다.
	// 검색어는 패널마다 따로 두므로 호출하는 쪽이 버퍼를 넘긴다.
	static void DrawTree(FSkeletalPreviewEditor& InEditor, char* InSearchBuffer, size_t InSearchBufferSize);

protected:
	void DrawContent() override;

private:
	static void DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const char* Filter);
	// 이 본이나 자손 중 하나라도 검색어를 이름에 포함하면 true
	static bool SubtreeMatches(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const char* Filter);

	char SearchBuffer[128] = {};
};
