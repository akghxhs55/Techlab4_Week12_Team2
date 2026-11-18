#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "AnimTimelinePanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimNotify.h"
#include "Animation/AnimNotifyState.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "UObject/Class.h"
#include "UObject/Property.h"
#include "UObject/UObjectHash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
    constexpr float RulerHeight = 24.0f;
    constexpr float NotifyRowHeight = 22.0f;
    constexpr float DiamondRadius = 5.0f;
    constexpr float MinStateBarWidth = 4.0f;
    constexpr float TrackHeaderWidth = 110.0f;
    constexpr float EndHandleWidth = 5.0f;   // 막대 안쪽으로 잡히는 폭
    constexpr float EndHandleSlop = 3.0f;    // 막대 바깥으로 더 잡히는 폭 (짧은 막대도 잡기 쉽게)

    const ImU32 RowColorA = EditorColor::BgElevated;
    const ImU32 RowColorB = EditorColor::BgElevatedAlt;
    const ImU32 NamedNotifyColor = EditorColor::Warning;    // 이름 Notify
    const ImU32 ClassNotifyColor = EditorColor::CategoryBlue;    // UAnimNotify
    const ImU32 NotifyStateColor = EditorColor::CategoryPurple;   // UAnimNotifyState

    const UObject* NotifyObjectOf(const FAnimNotifyEvent& Event)
    {
        if (Event.NotifyStateClass) return Event.NotifyStateClass;
        return Event.Notify;
    }

    ImU32 NotifyColorOf(const FAnimNotifyEvent& Event)
    {
        if (Event.NotifyStateClass) return NotifyStateColor;
        return Event.Notify ? ClassNotifyColor : NamedNotifyColor;
    }

    ImU32 WithAlpha(ImU32 Color, int Alpha)
    {
        return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
    }

    // "UAnimNotifyState_AttackWindow" -> "AttackWindow"
    FString ShortClassName(const UObject& Object)
    {
        FString Name = Object.GetClass()->Name;
        if (!Name.empty() && Name[0] == 'U') Name.erase(0, 1);
        for (const char* Prefix : { "AnimNotifyState_", "AnimNotify_" })
        {
            if (Name.rfind(Prefix, 0) == 0)
            {
                Name.erase(0, std::strlen(Prefix));
                break;
            }
        }
        return Name;
    }

    FString NotifyLabel(const FAnimNotifyEvent& Event)
    {
        if (auto* Named = dynamic_cast<const UAnimNotifyState_Named*>(Event.NotifyStateClass))
            return Named->NotifyName.empty() ? FString("(Unnamed)") : Named->NotifyName;

        if (auto* Sound = dynamic_cast<const UAnimNotify_PlaySound*>(Event.Notify))
        {
            const FString Stem = std::filesystem::path(Sound->SoundPath).stem().string();
            return Stem.empty() ? FString("PlaySound") : "PlaySound: " + Stem;
        }

        if (const UObject* Object = NotifyObjectOf(Event))
            return ShortClassName(*Object);

        return Event.NotifyName.IsNone() ? FString("(Unnamed)") : Event.NotifyName.ToString();
    }

    // 툴팁에 Notify 객체의 UPROPERTY 값을 보여준다 (편집은 디테일 패널 단계에서)
    void DrawPropertyLines(const UObject& Object)
    {
        for (const UStruct* Struct = Object.GetClass(); Struct; Struct = Struct->GetSuperStruct())
        {
            for (const FProperty* Property : Struct->GetProperties())
            {
                const void* Value = Property->ContainerPtrToValuePtr<void>(&Object);
                const char* Name = Property->Name.c_str();
                if (CastField<FFloatProperty>(Property))       ImGui::Text("%s: %.3f", Name, *static_cast<const float*>(Value));
                else if (CastField<FIntProperty>(Property))    ImGui::Text("%s: %d", Name, *static_cast<const int32*>(Value));
                else if (CastField<FBoolProperty>(Property))   ImGui::Text("%s: %s", Name, *static_cast<const bool*>(Value) ? "true" : "false");
                else if (CastField<FStrProperty>(Property))    ImGui::Text("%s: %s", Name, static_cast<const FString*>(Value)->c_str());
                else if (CastField<FVectorProperty>(Property))
                {
                    const FVector& V = *static_cast<const FVector*>(Value);
                    ImGui::Text("%s: (%.2f, %.2f, %.2f)", Name, V.X, V.Y, V.Z);
                }
            }
        }
    }

    void DrawNotifyTooltip(const FAnimNotifyEvent& Event, float FrameRate)
    {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(NotifyColorOf(Event)), "%s", NotifyLabel(Event).c_str());

        const UObject* Object = NotifyObjectOf(Event);
        if (Event.NotifyStateClass)  ImGui::TextDisabled("NotifyState (%s)", Object->GetClass()->Name.c_str());
        else if (Event.Notify)       ImGui::TextDisabled("Notify (%s)", Object->GetClass()->Name.c_str());
        else                         ImGui::TextDisabled("Notify (Name)");

        ImGui::Separator();
        ImGui::Text("Time: %.3f s  (Frame %.1f)", Event.TriggerTime, Event.TriggerTime * FrameRate);
        if (Event.NotifyStateClass)
            ImGui::Text("Duration: %.3f s  (%.1f frames)", Event.Duration, Event.Duration * FrameRate);
        if (Event.TriggerWeightThreshold > 0.0f)
            ImGui::Text("Weight Threshold: %.2f", Event.TriggerWeightThreshold);
        ImGui::Text("Track: %d", Event.TrackIndex);

        if (Object)
        {
            ImGui::Separator();
            DrawPropertyLines(*Object);
        }
        ImGui::EndTooltip();
    }
}

void FAnimTimelinePanel::DrawContent()
{
    UAnimSequence* Sequence = Editor.GetSequence();
    if (!Sequence)
    {
        ImGui::TextDisabled("No sequence");
        return;
    }

    const ImVec2 PanelAvail = ImGui::GetContentRegionAvail();
    if (PanelAvail.x < 100.0f || PanelAvail.y < 60.0f)
    {
        return;
    }

    const float TransportBtnSide = std::round(ImGui::GetFrameHeight() * 1.5f);
    const float MinLeftWidth = EditorStyle::Scale(150.0f);
    const float MaxLeftWidth = std::max(MinLeftWidth, PanelAvail.x - EditorStyle::Scale(200.0f));
    const float LeftWidth = std::clamp(EditorStyle::Scale(LeftPaneWidth), MinLeftWidth, MaxLeftWidth);
    const float SplitterWidth = EditorStyle::Scale(6.0f);
    const float RightWidth = std::max(60.0f, PanelAvail.x - LeftWidth - SplitterWidth);

    const float EffectiveRulerHeight = std::max(EditorStyle::Scale(RulerHeight), ImGui::GetFrameHeight() + EditorStyle::Scale(2.0f));
    const float ScaledRowHeight = std::max(EditorStyle::Scale(NotifyRowHeight), ImGui::GetTextLineHeight() + EditorStyle::Scale(8.0f));
    const float BottomBarHeight = TransportBtnSide + EditorStyle::Scale(8.0f);
    const float BodyHeight = std::max(EffectiveRulerHeight + ScaledRowHeight, PanelAvail.y - BottomBarHeight - EditorStyle::Scale(4.0f));

    // 필터에 맞는 Notify 트랙 목록 구성
    TArray<int32> VisibleTracks;
    const int32 NumTracks = Sequence->GetNumNotifyTracks();
    for (int32 Track = 0; Track < NumTracks; ++Track)
    {
        if (TrackFilterBuffer[0] == '\0')
        {
            VisibleTracks.Add(Track);
            continue;
        }
        if (EditorStyle::ContainsIgnoreCase(Sequence->GetNotifyTrackName(Track).c_str(), TrackFilterBuffer))
        {
            VisibleTracks.Add(Track);
            continue;
        }
        for (const FAnimNotifyEvent& Event : Sequence->GetNotifies())
        {
            if (Event.TrackIndex == Track &&
                EditorStyle::ContainsIgnoreCase(NotifyLabel(Event).c_str(), TrackFilterBuffer))
            {
                VisibleTracks.Add(Track);
                break;
            }
        }
    }

    const ImVec2 BodyOrigin = ImGui::GetCursorScreenPos();

    // 1. 상단 고정 헤더 줄: [필터 검색 + 프레임] | [프레임 눈금자 + 플레이헤드]
    DrawHeaderRow(BodyOrigin, LeftWidth, RightWidth, EffectiveRulerHeight);

    bool bSplitterHovered = false;
    bool bSplitterDragging = false;

    // 헤더 영역 세로 경계 스플리터
    ImGui::SetCursorScreenPos(ImVec2(BodyOrigin.x + LeftWidth, BodyOrigin.y));
    ImGui::InvisibleButton("##HeaderPaneSplitter", ImVec2(SplitterWidth, EffectiveRulerHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        bSplitterHovered = true;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        bSplitterDragging = true;
    }

    // 2. 중단 스크롤 영역: [노티파이/트랙 목록] | [타임라인 트랙 영역]
    const float TracksScrollHeight = std::max(ScaledRowHeight, BodyHeight - EffectiveRulerHeight);
    ImGui::SetCursorScreenPos(ImVec2(BodyOrigin.x, BodyOrigin.y + EffectiveRulerHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##TimelineTracksScroll", ImVec2(PanelAvail.x, TracksScrollHeight), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 ScrollOrigin = ImGui::GetCursorScreenPos();
        const float Length = Editor.GetPlayLength();
        const float Rate = Editor.GetFrameRate();
        const float PixelsPerSecond = Length > 0.0f ? RightWidth / Length : 1.0f;
        const float RightOriginX = ScrollOrigin.x + LeftWidth + SplitterWidth;

        // 2-A. "Notifies" 카테고리 헤더 행 (접기/펼치기 + 트랙 추가 버튼)
        {
            const ImVec2 CatLeftMin = ScrollOrigin;
            const ImVec2 CatLeftMax(CatLeftMin.x + LeftWidth, CatLeftMin.y + ScaledRowHeight);
            Draw->AddRectFilled(CatLeftMin, CatLeftMax, EditorColor::BgHeader);

            ImGui::SetCursorScreenPos(CatLeftMin);
            const float AddBtnW = EditorStyle::Scale(20.0f);
            ImGui::InvisibleButton("##NotifiesCategoryBtn", ImVec2(std::max(1.0f, LeftWidth - AddBtnW - EditorStyle::Scale(4.0f)), ScaledRowHeight));
            if (ImGui::IsItemHovered())
            {
                Draw->AddRectFilled(CatLeftMin, ImVec2(CatLeftMax.x - AddBtnW - EditorStyle::Scale(4.0f), CatLeftMax.y), EditorColor::White(16));
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                bNotifiesExpanded = !bNotifiesExpanded;
            }

            // 삼각형 화살표 + "Notifies" 텍스트
            const float ArrowSize = EditorStyle::Scale(8.0f);
            const float CenterY = (CatLeftMin.y + CatLeftMax.y) * 0.5f;
            const ImVec2 ArrowCenter(CatLeftMin.x + EditorStyle::Scale(10.0f), CenterY);
            if (bNotifiesExpanded)
            {
                Draw->AddTriangleFilled(
                    ImVec2(ArrowCenter.x - ArrowSize * 0.45f, ArrowCenter.y - ArrowSize * 0.25f),
                    ImVec2(ArrowCenter.x + ArrowSize * 0.45f, ArrowCenter.y - ArrowSize * 0.25f),
                    ImVec2(ArrowCenter.x, ArrowCenter.y + ArrowSize * 0.35f),
                    EditorColor::TextSecondary);
            }
            else
            {
                Draw->AddTriangleFilled(
                    ImVec2(ArrowCenter.x - ArrowSize * 0.25f, ArrowCenter.y - ArrowSize * 0.45f),
                    ImVec2(ArrowCenter.x + ArrowSize * 0.35f, ArrowCenter.y),
                    ImVec2(ArrowCenter.x - ArrowSize * 0.25f, ArrowCenter.y + ArrowSize * 0.45f),
                    EditorColor::TextSecondary);
            }
            Draw->AddText(ImVec2(CatLeftMin.x + EditorStyle::Scale(22.0f), CenterY - ImGui::GetTextLineHeight() * 0.5f),
                EditorColor::TextPrimary, "Notifies");

            // 오른쪽 끝 "+ Track" 추가 버튼
            ImGui::SetCursorScreenPos(ImVec2(CatLeftMax.x - AddBtnW - EditorStyle::Scale(2.0f),
                CatLeftMin.y + (ScaledRowHeight - AddBtnW) * 0.5f));
            if (ImGui::Button("+##AddNotifyTrackBtn", ImVec2(AddBtnW, AddBtnW)))
            {
                Sequence->AddNotifyTrack();
                Editor.MarkNotifiesDirty();
                bNotifiesExpanded = true;
            }
            ImGui::SetItemTooltip("Add Notify Track");

            // 오른쪽 타임라인 쪽 카테고리 줄 배경 + 프레임 격자
            const ImVec2 CatRightMin(RightOriginX, ScrollOrigin.y);
            const ImVec2 CatRightMax(RightOriginX + RightWidth, ScrollOrigin.y + ScaledRowHeight);
            Draw->AddRectFilled(CatRightMin, CatRightMax, EditorColor::BgRaised);
            Draw->AddLine(ImVec2(CatLeftMin.x, CatLeftMax.y - 1.0f), ImVec2(CatRightMax.x, CatRightMax.y - 1.0f), EditorColor::Border);

            const float PixelsPerFrame = Rate > 0.0f ? PixelsPerSecond / Rate : 0.0f;
            if (PixelsPerFrame >= 6.0f)
            {
                const int32 NumFrames = static_cast<int32>(std::floor(Length * Rate + 0.0001f));
                for (int32 Frame = 0; Frame <= NumFrames; ++Frame)
                {
                    const float X = RightOriginX + Frame * PixelsPerFrame;
                    Draw->AddLine(ImVec2(X, CatRightMin.y), ImVec2(X, CatRightMax.y), EditorColor::White(14));
                }
            }
        }

        // 2-B. 펼쳐진 경우 Notify 트랙 헤더(왼쪽) & Notify 트랙(오른쪽)
        float TotalTrackHeight = ScaledRowHeight;
        if (bNotifiesExpanded)
        {
            const ImVec2 TracksLeftOrigin(ScrollOrigin.x, ScrollOrigin.y + ScaledRowHeight);
            const ImVec2 TracksRightOrigin(RightOriginX, ScrollOrigin.y + ScaledRowHeight);
            const float NotifyHeight = DrawNotifyTracks(TracksRightOrigin, RightWidth, PixelsPerSecond, ScaledRowHeight, VisibleTracks);
            DrawTrackHeaders(TracksLeftOrigin, LeftWidth, ScaledRowHeight, VisibleTracks);
            TotalTrackHeight += NotifyHeight;
        }

        // 플레이헤드 수직선 (스크롤 영역 전체 관통)
        const float PlayheadX = RightOriginX + Editor.GetPlaybackTime() * PixelsPerSecond;
        const float LineBottom = std::max(ScrollOrigin.y + TotalTrackHeight, ScrollOrigin.y + TracksScrollHeight);
        Draw->AddLine(ImVec2(PlayheadX, ScrollOrigin.y), ImVec2(PlayheadX, LineBottom), EditorColor::AccentBright, 2.0f);

        // 세로 분할선 및 트랙 영역 스플리터
        const float SplitLineX = ScrollOrigin.x + LeftWidth + SplitterWidth * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(ScrollOrigin.x + LeftWidth, ScrollOrigin.y));
        ImGui::InvisibleButton("##TracksPaneSplitter", ImVec2(SplitterWidth, std::max(1.0f, LineBottom - ScrollOrigin.y)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            bSplitterHovered = true;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            bSplitterDragging = true;
        }
        const ImU32 SplitColor = bTimelineSplitterActive ? EditorColor::AccentBright : EditorColor::Border;
        const float SplitThickness = bTimelineSplitterActive ? 2.0f : 1.0f;
        Draw->AddLine(ImVec2(SplitLineX, ScrollOrigin.y), ImVec2(SplitLineX, LineBottom), SplitColor, SplitThickness);

        ImGui::SetCursorScreenPos(ImVec2(ScrollOrigin.x, ScrollOrigin.y + TotalTrackHeight));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    bTimelineSplitterActive = bSplitterHovered || bSplitterDragging;
    if (bTimelineSplitterActive)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (bSplitterDragging)
    {
        const float Scale = std::max(EditorStyle::Scale(1.0f), 0.1f);
        const float MaxUnscaled = std::max(150.0f, (PanelAvail.x - EditorStyle::Scale(200.0f)) / Scale);
        LeftPaneWidth = std::clamp((LeftWidth + ImGui::GetIO().MouseDelta.x) / Scale, 150.0f, MaxUnscaled);
    }

    // 3. 하단 바: [재생/정지/역재생/배속 버튼] | [타임라인 스크러버 + 프레임 + Notifies + Save]
    ImGui::SetCursorScreenPos(ImVec2(BodyOrigin.x, BodyOrigin.y + BodyHeight + EditorStyle::Scale(2.0f)));
    DrawBottomTransportBar(LeftWidth, RightWidth + SplitterWidth);
    DrawRecordNotifyPopup();

    // Delete 키: 타임라인 창에 포커스가 있고 글자 입력 중이 아닐 때 선택한 Notify 삭제
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput
        && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        DeleteNotify(Editor.GetSelectedNotify());
    }
}

void FAnimTimelinePanel::LoadIcons()
{
    if (bIconsLoaded) return;
    bIconsLoaded = true;

    auto LoadIcon = [](const char* PrimaryPath, const char* FallbackPath = nullptr) -> UTexture2D*
    {
        UTexture2D* Tex = UAssetManager::GetAssetByPath<UTexture2D>(PrimaryPath);
        if (!Tex && FallbackPath)
        {
            Tex = UAssetManager::GetAssetByPath<UTexture2D>(FallbackPath);
        }
        return Tex;
    };

    IconBackward = LoadIcon("Engine/Icons/Timeline/Backwards.png");
    IconStepBack = LoadIcon("Engine/Icons/Timeline/Step_Backwards.png");
    IconRecord = LoadIcon("Engine/Icons/Timeline/Record.png");
    IconForward = LoadIcon("Engine/Icons/Timeline/Play.png", "Engine/Icons/Toolbar/Play.png");
    IconPause = LoadIcon("Engine/Icons/Timeline/Pause.png", "Engine/Icons/Toolbar/Pause.png");
    IconStepForward = LoadIcon("Engine/Icons/Timeline/Step_Forward_.png");
    IconLoop = LoadIcon("Engine/Icons/Timeline/Loop.png");
}

void FAnimTimelinePanel::DrawHeaderRow(const ImVec2& Origin, float LeftWidth, float RightWidth, float HeaderHeight)
{
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImGuiIO& IO = ImGui::GetIO();
    const float Length = Editor.GetPlayLength();
    const float Rate = Editor.GetFrameRate();
    const float Time = Editor.GetPlaybackTime();
    const int32 CurrentFrame = static_cast<int32>(std::lround(Time * Rate));
    const float SplitterWidth = EditorStyle::Scale(6.0f);

    // ---- 왼쪽: 필터 검색창 + 현재 프레임 배지 ----
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + (HeaderHeight - ImGui::GetFrameHeight()) * 0.5f));
    char FrameBadge[16];
    std::snprintf(FrameBadge, sizeof(FrameBadge), "%d", CurrentFrame);
    const float BadgeWidth = std::max(EditorStyle::Scale(28.0f), ImGui::CalcTextSize(FrameBadge).x + EditorStyle::Scale(10.0f));
    const ImGuiStyle& Style = ImGui::GetStyle();
    const bool bHasFilter = (TrackFilterBuffer[0] != '\0');
    const float ClearWidth = bHasFilter ? ImGui::GetFrameHeight() : 0.0f;
    const float FilterWidth = std::max(40.0f, LeftWidth - BadgeWidth - Style.ItemSpacing.x * (bHasFilter ? 2.0f : 1.0f) - (bHasFilter ? ClearWidth : 0.0f) - EditorStyle::Scale(2.0f));

    const ImTextureID SearchIcon = ViewportToolbarWidgets::GetIcon("Search");
    const float SearchIconSize = (SearchIcon != ImTextureID_Invalid) ? EditorStyle::Scale(14.0f) : 0.0f;
    const float SearchPadLeft = (SearchIcon != ImTextureID_Invalid) ? (SearchIconSize + EditorStyle::Scale(8.0f)) : 0.0f;

    ImGui::SetNextItemWidth(FilterWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style.FramePadding.x + SearchPadLeft, Style.FramePadding.y));
    ImGui::InputTextWithHint("##TimelineFilter", "Filter...", TrackFilterBuffer, sizeof(TrackFilterBuffer));
    ImGui::PopStyleVar();

    if (SearchIcon != ImTextureID_Invalid)
    {
        const ImVec2 BoxMin = ImGui::GetItemRectMin();
        const ImVec2 BoxMax = ImGui::GetItemRectMax();
        const float IconX = BoxMin.x + Style.FramePadding.x + EditorStyle::Scale(2.0f);
        const float IconY = BoxMin.y + ((BoxMax.y - BoxMin.y) - SearchIconSize) * 0.5f;
        Draw->AddImage(SearchIcon,
            ImVec2(IconX, IconY), ImVec2(IconX + SearchIconSize, IconY + SearchIconSize),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(180, 185, 195, 220));
    }

    if (bHasFilter)
    {
        ImGui::SameLine();
        if (ImGui::Button("X##ClearTimelineFilter", ImVec2(ClearWidth, ImGui::GetFrameHeight())))
        {
            TrackFilterBuffer[0] = '\0';
        }
        ImGui::SetItemTooltip("Clear filter");
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", FrameBadge);

    // ---- 세로 구분선 ----
    Draw->AddLine(
        ImVec2(Origin.x + LeftWidth + SplitterWidth * 0.5f, Origin.y),
        ImVec2(Origin.x + LeftWidth + SplitterWidth * 0.5f, Origin.y + HeaderHeight),
        bTimelineSplitterActive ? EditorColor::AccentBright : EditorColor::Border,
        bTimelineSplitterActive ? 2.0f : 1.0f);

    // ---- 오른쪽: 프레임 눈금자 + 플레이헤드 배지 ----
    const ImVec2 RulerOrigin(Origin.x + LeftWidth + SplitterWidth, Origin.y);
    const ImVec2 RulerMax(RulerOrigin.x + RightWidth, RulerOrigin.y + HeaderHeight);
    Draw->AddRectFilled(RulerOrigin, RulerMax, EditorColor::BgHeader);
    Draw->AddLine(ImVec2(RulerOrigin.x, RulerMax.y - 1.0f), ImVec2(RulerMax.x, RulerMax.y - 1.0f), EditorColor::Border);

    const int32 TotalFrames = std::max(Editor.GetNumFrames(), 1);
    const float PixelsPerSecond = Length > 0.0f ? RightWidth / Length : 1.0f;
    const float PixelsPerFrame = Rate > 0.0f ? PixelsPerSecond / Rate : RightWidth / static_cast<float>(TotalFrames);

    // 라벨이 겹치지 않는 최소 프레임 간격 선택
    const int32 FrameSteps[] = { 1, 2, 5, 10, 20, 50, 100 };
    int32 LabelStep = FrameSteps[IM_ARRAYSIZE(FrameSteps) - 1];
    for (int32 Candidate : FrameSteps)
    {
        if (Candidate * PixelsPerFrame >= EditorStyle::Scale(28.0f))
        {
            LabelStep = Candidate;
            break;
        }
    }

    Draw->PushClipRect(RulerOrigin, RulerMax, true);
    for (int32 Frame = 0; Frame <= TotalFrames; ++Frame)
    {
        const float X = RulerOrigin.x + Frame * PixelsPerFrame;
        const bool bMajor = (Frame % LabelStep) == 0;
        if (bMajor)
        {
            Draw->AddLine(ImVec2(X, RulerOrigin.y + HeaderHeight - EditorStyle::Scale(10.0f)),
                ImVec2(X, RulerMax.y), EditorColor::TextMuted);
            char Label[16];
            std::snprintf(Label, sizeof(Label), "%d", Frame);
            Draw->AddText(ImVec2(X + EditorStyle::Scale(3.0f), RulerOrigin.y + EditorStyle::Scale(2.0f)),
                EditorColor::TextSecondary, Label);
        }
        else if (PixelsPerFrame >= 4.0f)
        {
            Draw->AddLine(ImVec2(X, RulerOrigin.y + HeaderHeight - EditorStyle::Scale(5.0f)),
                ImVec2(X, RulerMax.y), EditorColor::TextDisabled);
        }
    }

    // 스크럽: 눈금자 클릭/드래그
    ImGui::SetCursorScreenPos(RulerOrigin);
    ImGui::InvisibleButton("##Scrub", ImVec2(RightWidth, HeaderHeight));
    if (ImGui::IsItemActivated())
    {
        bWasPlayingBeforeScrub = Editor.IsPlaying();
    }
    if (ImGui::IsItemActive())
    {
        Editor.SetPlaybackTime(std::clamp((IO.MousePos.x - RulerOrigin.x) / PixelsPerSecond, 0.0f, Length));
    }
    if (ImGui::IsItemDeactivated() && bWasPlayingBeforeScrub)
    {
        Editor.Play();
    }

    // 플레이헤드 상단 배지 + 수직선
    const float PlayheadX = RulerOrigin.x + Editor.GetPlaybackTime() * PixelsPerSecond;
    const float Percent = Length > 0.0f ? (Time / Length) * 100.0f : 0.0f;
    char HeadText[48];
    std::snprintf(HeadText, sizeof(HeadText), "%d (%.2fs) (%.1f%%)", CurrentFrame, Time, Percent);
    const ImVec2 HeadTextSize = ImGui::CalcTextSize(HeadText);
    const float TagW = EditorStyle::Scale(8.0f);
    const float TagH = EditorStyle::Scale(12.0f);
    Draw->AddRectFilled(
        ImVec2(PlayheadX - TagW * 0.5f, RulerOrigin.y + 1.0f),
        ImVec2(PlayheadX + TagW * 0.5f, RulerOrigin.y + 1.0f + TagH),
        EditorColor::AccentBright, 2.0f);
    const float TextX = std::min(PlayheadX + TagW * 0.5f + EditorStyle::Scale(4.0f),
        RulerMax.x - HeadTextSize.x - EditorStyle::Scale(4.0f));
    Draw->AddText(ImVec2(std::max(RulerOrigin.x + 2.0f, TextX), RulerOrigin.y + 1.0f),
        EditorColor::TextPrimary, HeadText);
    Draw->AddLine(ImVec2(PlayheadX, RulerOrigin.y), ImVec2(PlayheadX, RulerMax.y), EditorColor::AccentBright, 2.0f);
    Draw->PopClipRect();
}

void FAnimTimelinePanel::DrawBottomTransportBar(float LeftWidth, float RightWidth)
{
    LoadIcons();

    const float Length = Editor.GetPlayLength();
    const float Rate = Editor.GetFrameRate();
    const int32 TotalFrames = Editor.GetNumFrames();
    const float Time = Editor.GetPlaybackTime();
    const int32 CurrentFrame = static_cast<int32>(std::lround(Time * Rate));
    const float BtnSide = std::round(ImGui::GetFrameHeight() * 1.5f);
    const float IconSide = EditorStyle::Scale(21.0f);
    const float Gap = EditorStyle::Scale(4.0f);

    const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddLine(
        ImVec2(BarOrigin.x, BarOrigin.y - EditorStyle::Scale(2.0f)),
        ImVec2(BarOrigin.x + LeftWidth + RightWidth, BarOrigin.y - EditorStyle::Scale(2.0f)),
        EditorColor::Border, 1.0f);

    auto DrawIconButton = [BtnSide, IconSide](const char* Id, UTexture2D* Icon, const char* FallbackText,
        const ImVec4& Tint = ImVec4(1, 1, 1, 1), bool bLeftBar = false, bool bRightBar = false) -> bool
    {
        if (Icon && Icon->GetResource() && Icon->GetResource()->GetSRV())
        {
            const ImVec2 Pos = ImGui::GetCursorScreenPos();
            const bool bClicked = ImGui::Button(Id, ImVec2(BtnSide, BtnSide));
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            const float OffsetX = bLeftBar ? EditorStyle::Scale(2.0f) : (bRightBar ? -EditorStyle::Scale(2.0f) : 0.0f);
            const ImVec2 Center(Pos.x + BtnSide * 0.5f + OffsetX, Pos.y + BtnSide * 0.5f);
            const ImVec2 Min(Center.x - IconSide * 0.5f, Center.y - IconSide * 0.5f);
            const ImVec2 Max(Center.x + IconSide * 0.5f, Center.y + IconSide * 0.5f);
            const ImU32 Color = ImGui::ColorConvertFloat4ToU32(Tint);
            DrawList->AddImage((ImTextureID)Icon->GetResource()->GetSRV(), Min, Max, ImVec2(0, 0), ImVec2(1, 1), Color);

            const float BarW = EditorStyle::Scale(2.5f);
            const float BarPad = EditorStyle::Scale(3.0f);
            if (bLeftBar)
            {
                const float BarX = Min.x - BarW;
                DrawList->AddRectFilled(ImVec2(BarX, Min.y + BarPad), ImVec2(BarX + BarW, Max.y - BarPad), Color);
            }
            if (bRightBar)
            {
                const float BarX = Max.x;
                DrawList->AddRectFilled(ImVec2(BarX, Min.y + BarPad), ImVec2(BarX + BarW, Max.y - BarPad), Color);
            }
            return bClicked;
        }
        return ImGui::Button(FallbackText, ImVec2(BtnSide, BtnSide));
    };

    // ---- 하단 왼쪽: 재생 / 역재생 / 프레임 / 루프 / 배속 버튼 ----
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Gap, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(BarOrigin.x, BarOrigin.y + EditorStyle::Scale(2.0f)));

    // 1. |< (처음으로)
    if (DrawIconButton("##ToFront", IconBackward, "|<", ImVec4(1, 1, 1, 1), true, false))
    {
        Editor.SetPlaybackTime(0.0f);
    }
    ImGui::SetItemTooltip("To Beginning (First Frame)");

    // 2. <| (1프레임 뒤로)
    ImGui::SameLine();
    if (DrawIconButton("##StepBack", IconStepBack, "<|"))
    {
        Editor.StepFrame(-1);
    }
    ImGui::SetItemTooltip("Previous Frame (Step Back)");

    // 3. < (역방향 재생 / 일시정지 토글)
    ImGui::SameLine();
    const bool bPlayingReverse = Editor.IsPlaying() && Editor.IsReverse();
    UTexture2D* RevIcon = bPlayingReverse ? IconPause : IconBackward;
    if (DrawIconButton("##ReversePlay", RevIcon, bPlayingReverse ? "||" : "<"))
    {
        if (bPlayingReverse) Editor.Pause();
        else Editor.PlayReverse();
    }
    ImGui::SetItemTooltip(bPlayingReverse ? "Pause" : "Play Reverse");

    // 4. ● (현재 프레임에 Notify 추가)
    ImGui::SameLine();
    if (DrawIconButton("##RecordNotify", IconRecord, "O", ImVec4(0.90f, 0.22f, 0.22f, 1.0f)))
    {
        RecordNotifyTime = std::clamp(Rate > 0.0f ? std::round(Time * Rate) / Rate : Time, 0.0f, Length);
        bOpenRecordNotifyPopup = true;
    }
    ImGui::SetItemTooltip("Add Notify at Current Frame");

    // 5. > (순방향 재생 / 일시정지 토글)
    ImGui::SameLine();
    const bool bPlayingForward = Editor.IsPlaying() && !Editor.IsReverse();
    UTexture2D* FwdIcon = bPlayingForward ? IconPause : IconForward;
    if (DrawIconButton("##ForwardPlay", FwdIcon, bPlayingForward ? "||" : ">"))
    {
        if (bPlayingForward) Editor.Pause();
        else Editor.PlayForward();
    }
    ImGui::SetItemTooltip(bPlayingForward ? "Pause" : "Play Forward");

    // 6. |> (1프레임 앞으로)
    ImGui::SameLine();
    if (DrawIconButton("##StepForward", IconStepForward, "|>"))
    {
        Editor.StepFrame(+1);
    }
    ImGui::SetItemTooltip("Next Frame (Step Forward)");

    // 7. >| (마지막으로)
    ImGui::SameLine();
    if (DrawIconButton("##ToEnd", IconForward, ">|", ImVec4(1, 1, 1, 1), false, true))
    {
        Editor.SetPlaybackTime(Length);
    }
    ImGui::SetItemTooltip("To End (Last Frame)");

    // 8. Loop (루프 토글)
    ImGui::SameLine();
    const ImVec4 LoopTint = Editor.bLooping ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    if (DrawIconButton("##LoopToggle", IconLoop, "Loop", LoopTint))
    {
        Editor.bLooping = !Editor.bLooping;
    }
    ImGui::SetItemTooltip(Editor.bLooping ? "Loop: Enabled" : "Loop: Disabled");

    // 9. 배속 조절 (x1.0)
    const float MinTransportLeftWidth = BtnSide * 8.0f + Gap * 7.0f + EditorStyle::Scale(68.0f);
    const float EffectiveLeftWidth = std::max(LeftWidth, MinTransportLeftWidth);
    const float CenteredControlY = BarOrigin.y + EditorStyle::Scale(2.0f) + (BtnSide - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SameLine(0.0f, EditorStyle::Scale(6.0f));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, CenteredControlY));
    const float RateWidth = std::max(EditorStyle::Scale(42.0f), BarOrigin.x + EffectiveLeftWidth - ImGui::GetCursorScreenPos().x - EditorStyle::Scale(6.0f));
    float AbsRate = std::abs(Editor.PlayRate);
    ImGui::SetNextItemWidth(std::min(RateWidth, EditorStyle::Scale(56.0f)));
    if (ImGui::DragFloat("##Rate", &AbsRate, 0.05f, 0.1f, 3.0f, "x%.1f"))
    {
        AbsRate = std::clamp(AbsRate, 0.1f, 3.0f);
        Editor.PlayRate = Editor.IsReverse() ? -AbsRate : AbsRate;
    }
    ImGui::SetItemTooltip("Playback Rate");
    ImGui::PopStyleVar();

    // ---- 하단 오른쪽: 프레임 범위 + 타임라인 스크럽 바 + Notifies + Save ----
    const float RightStartX = BarOrigin.x + EffectiveLeftWidth + EditorStyle::Scale(6.0f);
    const float RightEndX = BarOrigin.x + LeftWidth + RightWidth;
    const float BarY = CenteredControlY;

    const float FrameInputW = EditorStyle::Scale(44.0f);
    char TotalStr[32];
    std::snprintf(TotalStr, sizeof(TotalStr), "%d  (%.2fs/%.2fs)", TotalFrames, Time, Length);
    const float TotalStrW = ImGui::CalcTextSize(TotalStr).x;
    const float StartLabelW = ImGui::CalcTextSize("0").x;

    const float NotifiesCheckW = ImGui::GetFrameHeight() + ImGui::CalcTextSize("Notifies").x + ImGui::GetStyle().ItemInnerSpacing.x;
    const float SaveBtnW = ImGui::CalcTextSize("Save").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool bDirty = Editor.IsDirty();
    const bool bHasSaveMsg = !bDirty && !Editor.GetSaveMessage().empty();
    const float StatusW = bDirty ? ImGui::CalcTextSize("Unsaved").x : (bHasSaveMsg ? ImGui::CalcTextSize(Editor.GetSaveMessage().c_str()).x : 0.0f);

    const float Spacing = EditorStyle::Scale(8.0f);
    const float FixedRightControlsW = StartLabelW + FrameInputW + TotalStrW + NotifiesCheckW + SaveBtnW
        + (StatusW > 0.0f ? StatusW + Spacing : 0.0f) + Spacing * 6.0f;
    const float ScrubBarW = std::max(EditorStyle::Scale(60.0f), (RightEndX - RightStartX) - FixedRightControlsW);

    ImGui::SetCursorScreenPos(ImVec2(RightStartX, BarY));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("0");

    ImGui::SameLine(0.0f, Spacing);
    ImGui::SetNextItemWidth(FrameInputW);
    int32 InputFrame = CurrentFrame;
    const bool bFrameCommitted = ImGui::InputInt("##FrameInput", &InputFrame, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
    if (bFrameCommitted || ImGui::IsItemDeactivatedAfterEdit())
    {
        InputFrame = std::clamp(InputFrame, 0, std::max(TotalFrames, 0));
        Editor.SetPlaybackTime(Rate > 0.0f ? static_cast<float>(InputFrame) / Rate : 0.0f);
    }
    ImGui::SetItemTooltip("Current Frame (enter number to jump)");

    // 가로 타임라인 스크럽 바 (0 ~ Length)
    ImGui::SameLine(0.0f, Spacing);
    const ImVec2 ScrubMin = ImGui::GetCursorScreenPos();
    const float ScrubHeight = ImGui::GetFrameHeight();
    const ImVec2 ScrubMax(ScrubMin.x + ScrubBarW, ScrubMin.y + ScrubHeight);
    ImGui::InvisibleButton("##BottomTimelineScrub", ImVec2(ScrubBarW, ScrubHeight));
    if (ImGui::IsItemActivated())
    {
        bWasPlayingBeforeBottomScrub = Editor.IsPlaying();
    }
    if (ImGui::IsItemActive())
    {
        const float Alpha = std::clamp((ImGui::GetIO().MousePos.x - ScrubMin.x) / std::max(ScrubBarW, 1.0f), 0.0f, 1.0f);
        Editor.SetPlaybackTime(Alpha * Length);
    }
    if (ImGui::IsItemDeactivated() && bWasPlayingBeforeBottomScrub)
    {
        Editor.Play();
    }

    const float TroughPadY = EditorStyle::Scale(5.0f);
    const ImVec2 TroughMin(ScrubMin.x, ScrubMin.y + TroughPadY);
    const ImVec2 TroughMax(ScrubMax.x, ScrubMax.y - TroughPadY);
    Draw->AddRectFilled(TroughMin, TroughMax, EditorColor::BgDeepest, EditorStyle::Scale(3.0f));
    Draw->AddRect(TroughMin, TroughMax, EditorColor::Border, EditorStyle::Scale(3.0f));

    const float Norm = Length > 0.0f ? std::clamp(Time / Length, 0.0f, 1.0f) : 0.0f;
    const float KnobW = EditorStyle::Scale(10.0f);
    const float TravelW = std::max(1.0f, ScrubBarW - KnobW);
    const float KnobX = ScrubMin.x + Norm * TravelW;
    if (Norm > 0.0f)
    {
        Draw->AddRectFilled(TroughMin, ImVec2(KnobX + KnobW * 0.5f, TroughMax.y),
            EditorColor::WithAlpha(EditorColor::Accent, 140), EditorStyle::Scale(3.0f));
    }
    const ImU32 KnobColor = (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ? EditorColor::TextPrimary
        : EditorColor::TextMuted;
    Draw->AddRectFilled(ImVec2(KnobX, TroughMin.y + 1.0f), ImVec2(KnobX + KnobW, TroughMax.y - 1.0f),
        KnobColor, EditorStyle::Scale(3.0f));

    ImGui::SameLine(0.0f, Spacing);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TotalStr);

    // Week12 기존 기능 유지: Notifies 체크박스 + Save 버튼
    ImGui::SameLine(0.0f, Spacing);
    ImGui::Checkbox("Notifies", &Editor.bPreviewNotifies);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fire notifies while playing (sounds etc.). Name-based notifies are logged.");
    }

    ImGui::SameLine(0.0f, Spacing);
    ImGui::BeginDisabled(!bDirty);
    if (ImGui::Button("Save"))
    {
        Editor.Save();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Save notifies and root motion settings to the .anim.bin (Ctrl+S)");
    }

    if (bDirty)
    {
        ImGui::SameLine(0.0f, Spacing);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(EditorColor::ToVec4(EditorColor::Warning), "Unsaved");
    }
    else if (bHasSaveMsg)
    {
        ImGui::SameLine(0.0f, Spacing);
        ImGui::AlignTextToFramePadding();
        const bool bFailed = Editor.GetSaveMessage().rfind("failed") != FString::npos;
        ImGui::TextColored(bFailed ? EditorColor::ToVec4(EditorColor::Error) : EditorColor::ToVec4(EditorColor::Success),
            "%s", Editor.GetSaveMessage().c_str());
    }
}

void FAnimTimelinePanel::DrawTrackHeaders(const ImVec2& HeaderOrigin, float Width, float RowHeight, const TArray<int32>& VisibleTracks)
{
    UAnimSequence* Sequence = Editor.GetSequence();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const int32 NumTracks = Sequence->GetNumNotifyTracks();
    const ImVec2 CellSize(Width, RowHeight);

    bool bOpenMenu = false;
    bool bOpenRename = false;

    for (int32 Row = 0; Row < VisibleTracks.Num(); ++Row)
    {
        const int32 Track = VisibleTracks[Row];
        const ImVec2 Min(HeaderOrigin.x, HeaderOrigin.y + Row * RowHeight);
        const ImVec2 Max(Min.x + CellSize.x, Min.y + CellSize.y);
        Draw->AddRectFilled(Min, Max, (Row % 2) ? RowColorB : RowColorA);

        // 긴 이름은 칸 안에서 자른다 (Notifies 아래에 들여쓰기)
        Draw->PushClipRect(Min, Max, true);
        Draw->AddText(ImVec2(Min.x + EditorStyle::Scale(22.0f), Min.y + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f),
            EditorColor::TextSecondary, Sequence->GetNotifyTrackName(Track).c_str());
        Draw->PopClipRect();

        ImGui::PushID(Track);
        ImGui::SetCursorScreenPos(Min);
        ImGui::InvisibleButton("##TrackHeader", CellSize);
        if (ImGui::IsItemHovered())
        {
            Draw->AddRect(Min, Max, EditorColor::White(50));
            ImGui::SetTooltip("Right-click: track menu\nDouble-click: rename");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ContextTrack = Track;
            bOpenMenu = true;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            ContextTrack = Track;
            bOpenRename = true;
        }
        ImGui::PopID();
    }

    // 팝업은 PushID 밖에서 열어야 아래 BeginPopup과 ID가 맞는다
    if (bOpenMenu)
    {
        ImGui::OpenPopup("##NotifyTrackMenu");
    }

    if (ImGui::BeginPopup("##NotifyTrackMenu"))
    {
        if (ContextTrack >= 0 && ContextTrack < NumTracks)
        {
            ImGui::TextDisabled("Track: %s", Sequence->GetNotifyTrackName(ContextTrack).c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Insert Track Above"))
            {
                Sequence->InsertNotifyTrack(ContextTrack);
                Editor.MarkNotifiesDirty();
            }
            if (ImGui::MenuItem("Insert Track Below"))
            {
                Sequence->InsertNotifyTrack(ContextTrack + 1);
                Editor.MarkNotifiesDirty();
            }
            if (ImGui::MenuItem("Rename..."))
            {
                bOpenRename = true;
            }

            int32 NumOnTrack = 0;
            for (const FAnimNotifyEvent& Event : Sequence->GetNotifies())
                if (Event.TrackIndex == ContextTrack) ++NumOnTrack;

            char RemoveLabel[64];
            if (NumOnTrack > 0) std::snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove Track (deletes %d notifies)", NumOnTrack);
            else                std::snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove Track");

            // 마지막 트랙은 지울 수 없다
            if (ImGui::MenuItem(RemoveLabel, nullptr, false, NumTracks > 1))
            {
                Sequence->RemoveNotifyTrack(ContextTrack);
                Editor.MarkNotifiesDirty();
            }
        }
        ImGui::EndPopup();
    }

    if (bOpenRename && ContextTrack >= 0 && ContextTrack < NumTracks)
    {
        const FAnimNotifyTrack* Data = ContextTrack < Sequence->GetNotifyTracks().Num() ? &Sequence->GetNotifyTracks()[ContextTrack] : nullptr;
        std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s", Data ? Data->Name.c_str() : "");
        ImGui::OpenPopup("##RenameNotifyTrack");
    }

    if (ImGui::BeginPopup("##RenameNotifyTrack"))
    {
        ImGui::Text("Track name (empty = number)");
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        const bool bEnter = ImGui::InputText("##TrackName", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if (bEnter || ImGui::Button("OK"))
        {
            Sequence->RenameNotifyTrack(ContextTrack, RenameBuffer);
            Editor.MarkNotifiesDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

float FAnimTimelinePanel::DrawNotifyTracks(const ImVec2& TracksOrigin, float Width, float PixelsPerSecond, float RowHeight, const TArray<int32>& VisibleTracks)
{
    const UAnimSequence* Sequence = Editor.GetSequence();
    const TArray<FAnimNotifyEvent>& Notifies = Sequence->GetNotifies();
    const float Rate = Editor.GetFrameRate();
    const float Length = Editor.GetPlayLength();

    const int32 NumRows = VisibleTracks.Num();
    const float Height = NumRows * RowHeight;
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 TracksMax(TracksOrigin.x + Width, TracksOrigin.y + Height);

    auto RowForTrack = [&VisibleTracks](int32 TrackIndex) -> int32
    {
        for (int32 Row = 0; Row < VisibleTracks.Num(); ++Row)
        {
            if (VisibleTracks[Row] == TrackIndex)
            {
                return Row;
            }
        }
        return INDEX_NONE;
    };

    // 드래그 중이면 놓일 줄을 밝게 보여 준다
    int32 DropRow = INDEX_NONE;
    if (DraggingNotify.IsValid() && bDragMoved)
        if (const FAnimNotifyEvent* Dragged = Sequence->FindNotify(DraggingNotify))
            DropRow = RowForTrack(Dragged->TrackIndex);

    // 줄 배경
    for (int32 Row = 0; Row < NumRows; ++Row)
    {
        const float Y = TracksOrigin.y + Row * RowHeight;
        const ImU32 RowColor = (Row == DropRow) ? EditorColor::BgAccentHover : ((Row % 2) ? RowColorB : RowColorA);
        Draw->AddRectFilled(ImVec2(TracksOrigin.x, Y), ImVec2(TracksMax.x, Y + RowHeight), RowColor);
    }

    // 프레임 격자: 프레임 간격이 충분히 넓을 때만
    const float PixelsPerFrame = Rate > 0.0f ? PixelsPerSecond / Rate : 0.0f;
    if (PixelsPerFrame >= 6.0f && NumRows > 0)
    {
        const int32 NumFrames = static_cast<int32>(std::floor(Length * Rate + 0.0001f));
        for (int32 Frame = 0; Frame <= NumFrames; ++Frame)
        {
            const float X = TracksOrigin.x + Frame * PixelsPerFrame;
            Draw->AddLine(ImVec2(X, TracksOrigin.y), ImVec2(X, TracksMax.y), EditorColor::White(14));
        }
    }

    // 툴팁/선택은 스크럽 같은 다른 조작 중에는 받지 않는다
    const ImVec2 Mouse = ImGui::GetIO().MousePos;
    const bool bCanHover = ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive()
        && ImGui::IsMouseHoveringRect(TracksOrigin, TracksMax);
    const FAnimNotifyEvent* Hovered = nullptr;
    bool bHoveredEndHandle = false;   // NotifyState 막대 오른쪽 끝 (길이 조절)
    const FGuid& Selected = Editor.GetSelectedNotify();
    const ImU32 SelectedOutline = EditorColor::White(255);

    // State 막대를 먼저 그리고 단발 마름모를 위에 그린다 (짧은 마름모가 막대에 가려지지 않게)
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        for (const FAnimNotifyEvent& Event : Notifies)
        {
            const bool bIsState = Event.NotifyStateClass != nullptr;
            if (bIsState != (Pass == 0))
                continue;

            const int32 Row = RowForTrack(Event.TrackIndex);
            if (Row == INDEX_NONE)
                continue;

            const float RowTop = TracksOrigin.y + Row * RowHeight;
            const float CenterY = RowTop + RowHeight * 0.5f;
            const float StartX = TracksOrigin.x + Event.TriggerTime * PixelsPerSecond;
            const ImU32 Color = NotifyColorOf(Event);
            const FString Label = NotifyLabel(Event);
            const bool bSelected = Selected.IsValid() && Event.Guid == Selected;

            ImVec2 HitMin, HitMax;
            if (bIsState)
            {
                const float EndX = std::max(TracksOrigin.x + Event.GetEndTriggerTime() * PixelsPerSecond, StartX + MinStateBarWidth);
                const ImVec2 BarMin(StartX, RowTop + 3.0f);
                const ImVec2 BarMax(EndX, RowTop + RowHeight - 3.0f);
                Draw->AddRectFilled(BarMin, BarMax, WithAlpha(Color, bSelected ? 210 : 150), 3.0f);
                Draw->AddRect(BarMin, BarMax, bSelected ? SelectedOutline : Color, 3.0f, 0, bSelected ? 2.0f : 1.0f);

                // 라벨은 막대 안에서 잘라 그린다
                Draw->PushClipRect(BarMin, BarMax, true);
                Draw->AddText(ImVec2(BarMin.x + 4.0f, CenterY - ImGui::GetTextLineHeight() * 0.5f), EditorColor::White(230), Label.c_str());
                Draw->PopClipRect();

                // 선택되면 끝 핸들을 보여 준다
                if (bSelected)
                    Draw->AddRectFilled(ImVec2(BarMax.x - 3.0f, BarMin.y + 2.0f), ImVec2(BarMax.x - 1.0f, BarMax.y - 2.0f), SelectedOutline);

                HitMin = BarMin;
                HitMax = ImVec2(BarMax.x + EndHandleSlop, BarMax.y);
            }
            else
            {
                const float Radius = bSelected ? DiamondRadius + 1.5f : DiamondRadius;
                const ImVec2 Points[4] =
                {
                    ImVec2(StartX, CenterY - Radius),
                    ImVec2(StartX + Radius, CenterY),
                    ImVec2(StartX, CenterY + Radius),
                    ImVec2(StartX - Radius, CenterY),
                };
                Draw->AddConvexPolyFilled(Points, 4, Color);
                Draw->AddPolyline(Points, 4, bSelected ? SelectedOutline : EditorColor::Ink, ImDrawFlags_Closed, bSelected ? 2.0f : 1.0f);
                Draw->AddText(ImVec2(StartX + DiamondRadius + 3.0f, CenterY - ImGui::GetTextLineHeight() * 0.5f), EditorColor::TextSecondary, Label.c_str());

                HitMin = ImVec2(StartX - DiamondRadius - 1.0f, RowTop);
                HitMax = ImVec2(StartX + DiamondRadius + 1.0f, RowTop + RowHeight);
            }

            // 미리보기에서 방금 발동했으면 잠깐 빛난다
            if (const float Flash = Editor.GetNotifyFlash(Event.Guid); Flash > 0.0f)
            {
                const int Alpha = static_cast<int>(Flash * 220.0f);
                if (bIsState)
                    Draw->AddRect(ImVec2(HitMin.x - 2.0f, HitMin.y - 2.0f), ImVec2(HitMax.x - EndHandleSlop + 2.0f, HitMax.y + 2.0f), EditorColor::WithAlpha(EditorColor::Warning, Alpha), 4.0f, 0, 2.0f);
                else
                    Draw->AddCircle(ImVec2(StartX, CenterY), DiamondRadius + 4.0f + (1.0f - Flash) * 4.0f, EditorColor::WithAlpha(EditorColor::Warning, Alpha), 0, 2.0f);
            }

            // 나중에 그린 것(위에 보이는 것)이 우선
            if (bCanHover && Mouse.x >= HitMin.x && Mouse.x <= HitMax.x && Mouse.y >= HitMin.y && Mouse.y <= HitMax.y)
            {
                Hovered = &Event;
                bHoveredEndHandle = bIsState && Mouse.x >= HitMax.x - EndHandleSlop - EndHandleWidth;
            }
        }
    }

    // 끝 핸들 위에서는 좌우 화살표 커서
    if (bHoveredEndHandle || (DraggingNotify.IsValid() && bDragResize && bDragMoved))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (NumRows == 0)
    {
        return 0.0f;
    }

    // 트랙 영역 전체를 버튼으로 덮어 클릭/드래그를 받는다 (레이아웃 공간도 차지해 창 스크롤이 생긴다)
    ImGui::SetCursorScreenPos(TracksOrigin);
    ImGui::InvisibleButton("##NotifyTracks", ImVec2(Width, Height));

    // 툴팁은 편집 전에 띄운다 (편집하면 배열이 재정렬돼 Hovered 포인터가 무효가 될 수 있다)
    if (Hovered && ImGui::IsItemHovered() && !DraggingNotify.IsValid())
    {
        DrawNotifyTooltip(*Hovered, Rate);
    }

    if (ImGui::IsItemActivated())
    {
        // Notify 위에서 누르면 선택 + 드래그 시작, 빈 곳이면 선택 해제
        if (Hovered)
        {
            Editor.SelectNotify(Hovered->Guid);
            DraggingNotify = Hovered->Guid;
            DragGrabOffset = (Mouse.x - TracksOrigin.x) / PixelsPerSecond - Hovered->TriggerTime;
            DragStartMouse = Mouse;
            bDragMoved = false;
            bDragResize = bHoveredEndHandle;
        }
        else
        {
            Editor.ClearNotifySelection();
        }
    }

    // 우클릭: 그 자리(트랙, 프레임에 맞춘 시간)에 추가하는 메뉴. Notify 위면 삭제도.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !DraggingNotify.IsValid())
    {
        const float RawTime = std::clamp((Mouse.x - TracksOrigin.x) / PixelsPerSecond, 0.0f, Length);
        ContextTime = Rate > 0.0f ? std::round(RawTime * Rate) / Rate : RawTime;
        const int32 ClickedRow = std::clamp(static_cast<int32>((Mouse.y - TracksOrigin.y) / RowHeight), 0, NumRows - 1);
        ContextNotifyTrack = VisibleTracks[ClickedRow];
        ContextNotify = Hovered ? Hovered->Guid : FGuid();
        if (Hovered) Editor.SelectNotify(Hovered->Guid);
        ImGui::OpenPopup("##NotifyContextMenu");
    }
    DrawNotifyContextMenu();

    if (ImGui::IsItemActive() && DraggingNotify.IsValid())
    {
        UpdateNotifyDrag(TracksOrigin, PixelsPerSecond, RowHeight, VisibleTracks);
    }

    if (ImGui::IsItemDeactivated())
    {
        DraggingNotify.Invalidate();
        bDragMoved = false;
    }

    return Height;
}

void FAnimTimelinePanel::DrawNotifyContextMenu()
{
    if (!ImGui::BeginPopup("##NotifyContextMenu"))
    {
        return;
    }

    UAnimSequence* Sequence = Editor.GetSequence();
    const float Rate = Editor.GetFrameRate();

    // 우클릭한 Notify가 있으면 맨 위에 삭제
    if (ContextNotify.IsValid())
    {
        if (const FAnimNotifyEvent* Event = Sequence->FindNotify(ContextNotify))
        {
            ImGui::TextDisabled("%s", NotifyLabel(*Event).c_str());
            if (ImGui::MenuItem("Delete", "Del"))
            {
                DeleteNotify(ContextNotify);
            }
            ImGui::Separator();
        }
    }

    ImGui::TextDisabled("Track %s  |  %.3f s (Frame %d)", Sequence->GetNotifyTrackName(ContextNotifyTrack).c_str(),
        ContextTime, static_cast<int>(std::lround(ContextTime * Rate)));

    DrawAddNotifyMenuItems(ContextTime, ContextNotifyTrack);

    ImGui::EndPopup();
}

void FAnimTimelinePanel::DrawAddNotifyMenuItems(float Time, int32 TrackIndex)
{
    UAnimSequence* Sequence = Editor.GetSequence();
    const float Rate = Editor.GetFrameRate();

    // 새 Notify를 지정한 트랙에 두고 선택한다
    auto Finish = [&](const FGuid& Guid)
        {
            Sequence->SetNotifyTrackIndex(Guid, TrackIndex);
            Editor.SelectNotify(Guid);
            Editor.MarkNotifiesDirty();
        };
    // NotifyState 기본 길이: 5프레임 (시퀀스 끝을 넘으면 엔진이 자른다)
    const float DefaultDuration = Rate > 0.0f ? 5.0f / Rate : 0.1f;

    if (ImGui::BeginMenu("Add Notify"))
    {
        if (ImGui::MenuItem("Named Notify"))
        {
            Finish(Sequence->AddNotify(Time, FName("NewNotify")));
        }
        ImGui::Separator();

        TArray<UClass*> Classes;
        GetDerivedClasses(UAnimNotify::StaticClass(), Classes, true);
        std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
        for (UClass* Class : Classes)
        {
            if (ImGui::MenuItem(Class->Name.c_str()))
            {
                FGuid Guid;
                if (Sequence->AddNotifyOfClass(Class, Time, 0.0f, &Guid))
                    Finish(Guid);
            }
        }
        if (Classes.IsEmpty())
        {
            ImGui::TextDisabled("(no UAnimNotify classes)");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add Notify State"))
    {
        if (ImGui::MenuItem("Named Notify State"))
        {
            FGuid Guid;
            Sequence->AddNotify<UAnimNotifyState_Named>(Time, DefaultDuration, &Guid)->NotifyName = "NewNotifyState";
            Finish(Guid);
        }
        ImGui::Separator();

        TArray<UClass*> Classes;
        GetDerivedClasses(UAnimNotifyState::StaticClass(), Classes, true);
        std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
        for (UClass* Class : Classes)
        {
            if (Class == UAnimNotifyState_Named::StaticClass())
                continue;   // 위의 "Named Notify State"로 따로 보여 준다
            if (ImGui::MenuItem(Class->Name.c_str()))
            {
                FGuid Guid;
                if (Sequence->AddNotifyOfClass(Class, Time, DefaultDuration, &Guid))
                    Finish(Guid);
            }
        }
        ImGui::EndMenu();
    }
}

void FAnimTimelinePanel::DrawRecordNotifyPopup()
{
    if (bOpenRecordNotifyPopup)
    {
        ImGui::OpenPopup("##RecordAddNotifyPopup");
        bOpenRecordNotifyPopup = false;
    }

    if (!ImGui::BeginPopup("##RecordAddNotifyPopup"))
    {
        return;
    }

    UAnimSequence* Sequence = Editor.GetSequence();
    const float Rate = Editor.GetFrameRate();
    ImGui::Text("Add Notify at %.2fs (Frame %d)", RecordNotifyTime, static_cast<int>(std::lround(RecordNotifyTime * Rate)));
    ImGui::Separator();

    ImGui::SetNextItemWidth(EditorStyle::Scale(150.0f));
    const bool bEnter = ImGui::InputText("Name", NewNotifyNameBuffer, sizeof(NewNotifyNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    if ((bEnter || ImGui::Button("Add")) && NewNotifyNameBuffer[0])
    {
        const FGuid Guid = Sequence->AddNotify(RecordNotifyTime, FName(NewNotifyNameBuffer));
        Sequence->SetNotifyTrackIndex(Guid, 0);
        Editor.SelectNotify(Guid);
        Editor.MarkNotifiesDirty();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();
    DrawAddNotifyMenuItems(RecordNotifyTime, 0);

    ImGui::EndPopup();
}

void FAnimTimelinePanel::DeleteNotify(const FGuid& Guid)
{
    if (!Guid.IsValid())
        return;

    // 드래그 중인 Notify를 지우면 드래그도 끝낸다
    if (DraggingNotify == Guid)
        DraggingNotify.Invalidate();

    if (Editor.GetSequence()->RemoveNotify(Guid))
    {
        if (Editor.GetSelectedNotify() == Guid)
            Editor.ClearNotifySelection();
        Editor.MarkNotifiesDirty();
    }
}

void FAnimTimelinePanel::UpdateNotifyDrag(const ImVec2& TracksOrigin, float PixelsPerSecond, float RowHeight, const TArray<int32>& VisibleTracks)
{
    UAnimSequence* Sequence = Editor.GetSequence();
    const FAnimNotifyEvent* Event = Sequence->FindNotify(DraggingNotify);
    if (!Event || VisibleTracks.IsEmpty())
    {
        DraggingNotify.Invalidate();
        return;
    }

    const ImVec2 Mouse = ImGui::GetIO().MousePos;

    // 클릭만 했을 때 1~2픽셀 흔들림으로 움직이지 않도록 문턱을 둔다
    if (!bDragMoved)
    {
        const float Dx = Mouse.x - DragStartMouse.x;
        const float Dy = Mouse.y - DragStartMouse.y;
        if (Dx * Dx + Dy * Dy < 4.0f * 4.0f)
            return;
        bDragMoved = true;
    }

    const float Rate = Editor.GetFrameRate();
    const float Length = Editor.GetPlayLength();
    const bool bSnap = Rate > 0.0f && !ImGui::GetIO().KeyShift;

    // 끝 핸들: 시작은 그대로 두고 길이만 바꾼다 (최소 1프레임)
    if (bDragResize)
    {
        float EndTime = std::clamp((Mouse.x - TracksOrigin.x) / PixelsPerSecond, 0.0f, Length);
        if (bSnap)
            EndTime = std::round(EndTime * Rate) / Rate;
        const float MinDuration = Rate > 0.0f ? 1.0f / Rate : 0.01f;
        const float NewDuration = std::max(EndTime - Event->TriggerTime, MinDuration);
        if (std::fabs(NewDuration - Event->Duration) > 1e-6f)
        {
            Sequence->SetNotifyDuration(DraggingNotify, NewDuration);
            Editor.MarkNotifiesDirty();
        }
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // 세로: 마우스가 있는 줄로 트랙 이동
    const int32 TargetRow = std::clamp(static_cast<int32>(std::floor((Mouse.y - TracksOrigin.y) / RowHeight)), 0, VisibleTracks.Num() - 1);
    const int32 TargetTrack = VisibleTracks[TargetRow];

    // 가로: 시간 이동. 기본은 프레임 단위로 맞추고, Shift를 누르면 자유롭게.
    float TargetTime = (Mouse.x - TracksOrigin.x) / PixelsPerSecond - DragGrabOffset;
    if (bSnap)
        TargetTime = std::round(TargetTime * Rate) / Rate;
    // NotifyState는 끝이 시퀀스 밖으로 나가면 길이가 잘리므로, 길이를 지키도록 시작 시간을 제한한다
    const float MaxStart = std::max(Length - (Event->NotifyStateClass ? Event->Duration : 0.0f), 0.0f);
    TargetTime = std::clamp(TargetTime, 0.0f, MaxStart);

    const bool bTrackChanged = TargetTrack != Event->TrackIndex;
    const bool bTimeChanged = std::fabs(TargetTime - Event->TriggerTime) > 1e-6f;

    // SetNotifyTriggerTime은 재정렬하므로 Event 포인터는 여기서부터 쓰지 않는다
    if (bTrackChanged) Sequence->SetNotifyTrackIndex(DraggingNotify, TargetTrack);
    if (bTimeChanged)  Sequence->SetNotifyTriggerTime(DraggingNotify, TargetTime);
    if (bTrackChanged || bTimeChanged)
        Editor.MarkNotifiesDirty();
}
