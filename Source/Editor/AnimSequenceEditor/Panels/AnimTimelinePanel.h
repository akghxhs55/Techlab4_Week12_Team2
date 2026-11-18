#pragma once

#include "Core/Misc/Guid.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"

class UTexture2D;

class FAnimTimelinePanel : public TAssetEditorPanel<FAnimSequenceEditor>
{
public:
    explicit FAnimTimelinePanel(FAnimSequenceEditor& InEditor)
        : TAssetEditorPanel<FAnimSequenceEditor>(InEditor, "Timeline", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;

private:
    void LoadIcons();
    void DrawBottomTransportBar(float LeftWidth, float RightWidth);
    void DrawHeaderRow(const ImVec2& Origin, float LeftWidth, float RightWidth, float RulerHeight);
    // 눈금자 아래 Notify 트랙들. 그린 높이를 돌려준다.
    float DrawNotifyTracks(const ImVec2& TracksOrigin, float Width, float PixelsPerSecond, float RowHeight, const TArray<int32>& VisibleTracks);
    // 왼쪽 열: 트랙 이름, 우클릭 메뉴(위/아래 삽입, 이름 변경, 삭제), 더블클릭 이름 변경
    void DrawTrackHeaders(const ImVec2& HeaderOrigin, float Width, float RowHeight, const TArray<int32>& VisibleTracks);
    // 드래그 중인 Notify를 마우스 위치의 트랙/시간으로 옮긴다
    void UpdateNotifyDrag(const ImVec2& TracksOrigin, float PixelsPerSecond, float RowHeight, const TArray<int32>& VisibleTracks);
    // 트랙 우클릭 메뉴: 이름/클래스 Notify·NotifyState 추가, 우클릭한 Notify 삭제
    void DrawNotifyContextMenu();
    void DrawAddNotifyMenuItems(float Time, int32 TrackIndex);
    void DrawRecordNotifyPopup();
    void DeleteNotify(const FGuid& Guid);

    bool bWasPlayingBeforeScrub = false;
    bool bWasPlayingBeforeBottomScrub = false;

    // 왼쪽 트랙 리스트 폭·필터·접힘 상태
    float LeftPaneWidth = 265.0f;
    bool bTimelineSplitterActive = false;
    char TrackFilterBuffer[128] = {};
    bool bNotifiesExpanded = true;

    // Notify 드래그 (누른 Notify의 Guid, 잡은 지점과 시작 시간의 차이)
    FGuid DraggingNotify;
    float DragGrabOffset = 0.0f;
    ImVec2 DragStartMouse;
    bool bDragMoved = false;
    bool bDragResize = false;     // NotifyState 끝 핸들을 잡았으면 이동 대신 길이 조절

    // Notify 우클릭 메뉴가 가리키는 자리
    float ContextTime = 0.0f;
    int32 ContextNotifyTrack = 0;
    FGuid ContextNotify;          // 우클릭한 Notify (빈 곳이면 무효)

    int32 ContextTrack = -1;      // 트랙 메뉴/이름 변경 팝업이 가리키는 트랙
    char RenameBuffer[64] = {};

    // 타임라인 Record 버튼으로 현재 프레임에 Notify를 추가하는 팝업 상태
    float RecordNotifyTime = 0.0f;
    char NewNotifyNameBuffer[128] = "NewNotify";
    bool bOpenRecordNotifyPopup = false;

    UTexture2D* IconBackward = nullptr;
    UTexture2D* IconStepBack = nullptr;
    UTexture2D* IconRecord = nullptr;
    UTexture2D* IconForward = nullptr;
    UTexture2D* IconPause = nullptr;
    UTexture2D* IconStepForward = nullptr;
    UTexture2D* IconLoop = nullptr;
    bool bIconsLoaded = false;
};
