#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Character.h"
#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    // ELevelViewportType 순서와 같다.
    const char* ProjectionLabels[] = { "Perspective", "Top", "Front", "Right", "Bottom", "Left", "Back" };
    const char* ProjectionIcons[] = { "ViewPerspective", "ViewTop", "ViewFront", "ViewRight", "ViewBottom", "ViewLeft", "ViewBack" };
    static_assert(IM_ARRAYSIZE(ProjectionLabels) == static_cast<int>(ELevelViewportType::Count));

    // EViewportMode 순서와 같다. Solid는 UE 이름인 Lit으로 보여 준다.
    const char* ViewModeLabels[] = { "Lit", "Wireframe", "Scene Depth", "World Normal", "Light Culling", "Cascade", "Unlit" };
    const char* ViewModeIcons[] = { "Lit", "Wireframe", "BufferVisualization", "BufferVisualization", "LightCulling", "Cascade", "Unlit" };
    static_assert(IM_ARRAYSIZE(ViewModeLabels) == static_cast<int>(EViewportMode::Count));

    // ELayoutMode 순서와 같다.
    const char* LayoutIcons[] = { "Layout1Pane", "Layout4Panes", "Layout2Panes", "Layout2PanesStacked",
        "Layout3PanesLeft", "Layout3PanesRight", "Layout3PanesTop", "Layout3PanesBottom",
        "Layout4PanesLeft", "Layout4PanesRight", "Layout4PanesTop", "Layout4PanesBottom" };
    const char* LayoutTooltips[] = { "One pane", "Four panes 2x2", "Two panes (side by side)", "Two panes (stacked)",
        "Three panes (left)", "Three panes (right)", "Three panes (top)", "Three panes (bottom)",
        "Four panes (left)", "Four panes (right)", "Four panes (top)", "Four panes (bottom)" };
    static_assert(IM_ARRAYSIZE(LayoutIcons) == static_cast<int>(ELayoutMode::Count));

    // 메뉴의 ELevelViewportType 표시 순서 (UE: 원근 → 상단, 하단, 왼쪽, 오른쪽, 정면, 후면)
    const ELevelViewportType OrthoOrder[] = { ELevelViewportType::Top, ELevelViewportType::Bottom,
        ELevelViewportType::Left, ELevelViewportType::Right, ELevelViewportType::Front, ELevelViewportType::Back };
}

void FViewportsPanel::DrawViewportToolbar(int32 ViewIndex)
{
    FEditorViewportClient* Client = GetViewportClient(ViewIndex);
    if (!Client || !Client->IsActive()) return;
    const auto& Rect = Client->GetRect();
    if (Rect.Width < 100.0f || Rect.Height < 40.0f) return;

    // 왼쪽은 기즈모와 스폰, 오른쪽은 UE처럼 투영·카메라 속도·뷰 모드·표시·레이아웃 드롭다운을 둔다.
    // Layout: ⋮ 버튼(레이아웃 프리셋 메뉴), LayoutToggle: UE의 쿼드/싱글 전환 버튼
    // Add와 그리드 스냅은 UE처럼 레벨 에디터 메인 툴바에 있다.
    enum EItem { Move, Rotate, Scale, Space, Projection, Speed, ViewMode, Show, Render, Layout, LayoutToggle, ItemCount };
    // 왼쪽 묶음은 왼쪽 정렬, 오른쪽 묶음은 UE처럼 오른쪽 정렬한다.
    const EItem LeftItems[] = { Move, Rotate, Scale, Space };
    const EItem RightItems[] = { Projection, Speed, ViewMode, Show, Render, Layout, LayoutToggle };
    const char* PopupIds[ItemCount]{};
    PopupIds[Projection] = "Projection"; PopupIds[Speed] = "CameraSpeed";
    PopupIds[ViewMode] = "ViewMode"; PopupIds[Show] = "Show"; PopupIds[Render] = "Render"; PopupIds[Layout] = "Layout";
    EditorStyle::FPopupAnchor PopupAnchors[ItemCount]{};

    const bool bGameView = GameViewportClient && ViewIndex == GameViewportIndex;
    const int ProjectionIndex = std::clamp(static_cast<int>(Client->GetViewportType()), 0, static_cast<int>(ELevelViewportType::Count) - 1);
    const int ModeIndex = std::clamp(static_cast<int>(Client->GetViewportMode()), 0, static_cast<int>(EViewportMode::Count) - 1);

    char SpeedLabel[32];
    std::snprintf(SpeedLabel, sizeof(SpeedLabel), "%.2f", EditorSettings ? EditorSettings->GetSettings().CameraSpeed : 1.0f);

    // 항목별 내용. 아이콘이 있으면 UE식 드롭다운 버튼, 없으면 기존 텍스트 버튼이다.
    // Glyph가 있으면 아이콘 파일 대신 선으로 직접 그린다 (⋮, 田, □).
    enum class EGlyph : uint8 { None, Kebab, Quad, Single };
    struct FItemDesc { const char* Label = nullptr; const char* Icon = nullptr; bool bChevron = false; const char* Tooltip = nullptr; EGlyph Glyph = EGlyph::None; };
    FItemDesc Items[ItemCount];
    Items[Move] = { nullptr, "TransformMove", false, "Select and translate objects\nSpace: cycle Move / Rotate / Scale" };
    Items[Rotate] = { nullptr, "TransformRotate", false, "Select and rotate objects\nSpace: cycle Move / Rotate / Scale" };
    Items[Scale] = { nullptr, "TransformScale", false, "Select and scale objects\nSpace: cycle Move / Rotate / Scale" };
    // UE처럼 스케일은 항상 로컬 축이라 그동안은 Local로 고정해 보여 주고 버튼을 막는다. 고른 공간은 그대로 기억한다.
    const bool bScaleMode = Gizmo && Gizmo->GetMode() == EGizmoMode::Scale;
    const bool bWorldSpace = Gizmo && Gizmo->GetSpace() == EGizmoSpace::World && !bScaleMode;
    Items[Space] = { nullptr, bWorldSpace ? "SpaceWorld" : "SpaceLocal", false,
        bScaleMode ? "Scaling always uses local space" : (bWorldSpace ? "World space (click for local)" : "Local space (click for world)") };
    Items[Projection] = { ProjectionLabels[ProjectionIndex], ProjectionIcons[ProjectionIndex], true, "Viewport type and camera" };
    Items[Speed] = { SpeedLabel, "CameraSpeed", true, "Camera speed\nHold the right mouse button and scroll to change it." };
    Items[ViewMode] = { ViewModeLabels[ModeIndex], ViewModeIcons[ModeIndex], true, "View mode" };
    Items[Show] = { nullptr, "Show", true, "Show flags and viewport stats" };
    Items[Render] = { nullptr, "Settings", true, "Render settings" };   // UE 뷰포트처럼 톱니 아이콘
    Items[Layout] = { nullptr, nullptr, false, "Viewport layouts", EGlyph::Kebab };
    // 누르면 바뀔 모양을 보여 준다: 싱글이면 田(쿼드로), 여러 패널이면 □(이 뷰만 크게).
    const bool bSingleLayout = CurrentLayoutMode == ELayoutMode::Single;
    Items[LayoutToggle] = { nullptr, nullptr, false,
        bSingleLayout ? "Restore multi-pane layout" : "Maximize this viewport",
        bSingleLayout ? EGlyph::Quad : EGlyph::Single };

    float ButtonWidths[ItemCount]{};
    for (int Item = 0; Item < ItemCount; ++Item)
    {
        const FItemDesc& Desc = Items[Item];
        if (Desc.Glyph == EGlyph::Kebab)
            ButtonWidths[Item] = EditorStyle::Scale(16.0f);
        else if (Desc.Glyph != EGlyph::None)
            ButtonWidths[Item] = ImGui::GetFrameHeight();
        else
            ButtonWidths[Item] = Desc.Icon
                ? ViewportToolbarWidgets::IconButtonWidth(Desc.Label, Desc.bChevron)
                : ImGui::CalcTextSize(Desc.Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    const float Width = std::max(1.0f, Rect.Width - EditorStyle::Scale(12.0f));
    const float Gap = EditorStyle::Scale(4.0f);
    const float Padding = EditorStyle::Scale(4.0f);
    const float Avail = std::max(1.0f, Width - Padding * 2.0f);

    // 항목마다 줄과 x 위치를 정한다. 묶음 안에서만 줄바꿈하고, 오른쪽 묶음의 각 줄은 오른쪽 끝에 붙인다.
    int ItemRow[ItemCount]{};
    float ItemX[ItemCount]{};
    auto GroupWidth = [&](const EItem* Group, int Count)
    {
        float Total = 0.0f;
        for (int i = 0; i < Count; ++i) Total += (i > 0 ? Gap : 0.0f) + ButtonWidths[Group[i]];
        return Total;
    };
    auto PlaceGroup = [&](const EItem* Group, int Count, int StartRow, bool bAlignRight)
    {
        int Row = StartRow, RowFirst = 0;
        float RowUsed = 0.0f;
        auto FinishRow = [&](int End)
        {
            float X = bAlignRight ? std::max(0.0f, Avail - RowUsed) : 0.0f;
            for (int i = RowFirst; i < End; ++i)
            {
                ItemRow[Group[i]] = Row;
                ItemX[Group[i]] = X;
                X += ButtonWidths[Group[i]] + Gap;
            }
        };
        for (int i = 0; i < Count; ++i)
        {
            const float ItemWidth = ButtonWidths[Group[i]];
            if (RowUsed > 0.0f && RowUsed + Gap + ItemWidth > Avail)
            {
                FinishRow(i);
                ++Row; RowFirst = i; RowUsed = 0.0f;
            }
            RowUsed += (RowUsed > 0.0f ? Gap : 0.0f) + ItemWidth;
        }
        FinishRow(Count);
        return Row + 1;  // 다음 묶음이 시작할 줄
    };
    const int LeftCount = IM_ARRAYSIZE(LeftItems), RightCount = IM_ARRAYSIZE(RightItems);
    int Rows = 0;
    if (GroupWidth(LeftItems, LeftCount) + Gap * 4.0f + GroupWidth(RightItems, RightCount) <= Avail)
    {
        PlaceGroup(LeftItems, LeftCount, 0, false);
        Rows = PlaceGroup(RightItems, RightCount, 0, true);
    }
    else
    {
        Rows = PlaceGroup(RightItems, RightCount, PlaceGroup(LeftItems, LeftCount, 0, false), true);
    }

    const float Height = std::min(Rect.Height - EditorStyle::Scale(12.0f),
        Rows * ImGui::GetFrameHeight() + (Rows - 1) * Gap + Padding * 2.0f);
    const ImVec2 Position{ ContentOrigin.x + Rect.X + EditorStyle::Scale(6.0f), ContentOrigin.y + Rect.Y + EditorStyle::Scale(6.0f) };
    ToolbarRects[ViewIndex] = { Position.x, Position.y, Width, Height };
    ToolbarHeights[ViewIndex] = Height + EditorStyle::Scale(6.0f);
    ImGui::SetCursorScreenPos(Position);
    ImGui::PushID(ViewIndex);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Padding, Padding });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { Gap, Gap });
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(2.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 26, 30, 235));
    if (ImGui::BeginChild("ViewportToolbar", { Width, Height }, ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ActiveViewIndex = ViewIndex;
        for (int ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
        {
            const EItem Item = static_cast<EItem>(ItemIndex);
            const FItemDesc& Desc = Items[Item];
            const float ButtonWidth = ButtonWidths[Item];
            ImGui::SetCursorPos({ Padding + ItemX[Item], Padding + ItemRow[Item] * (ImGui::GetFrameHeight() + Gap) });

            const bool bGizmoItem = Item >= Move && Item <= Space;
            // PIE 게임 화면에서는 에디터 카메라·기즈모·스폰을 쓸 수 없다.
            const bool bDisabled = (bGizmoItem && !Gizmo) || (Item == Space && bScaleMode) ||
                (bGameView && (bGizmoItem || Item == Projection || Item == ViewMode || Item == Speed));
            // 지금 선택된 기즈모 모드는 눌린 색으로 표시한다.
            const bool bSelected = Gizmo && Item >= Move && Item <= Scale &&
                static_cast<int>(Gizmo->GetMode()) == Item - Move;

            EditorStyle::PushSelectedButton(bSelected);
            ImGui::BeginDisabled(bDisabled);
            ImGui::PushID(Item);
            bool bClicked = false;
            if (Desc.Glyph != EGlyph::None)
            {
                bClicked = ImGui::Button("##Glyph", { ButtonWidth, ImGui::GetFrameHeight() });
                ImDrawList* DrawList = ImGui::GetWindowDrawList();
                const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
                const ImVec2 Center{ (Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f };
                const ImU32 Color = IM_COL32(210, 210, 210, 255);
                const float Line = std::max(1.0f, EditorStyle::Scale(1.2f));
                if (Desc.Glyph == EGlyph::Kebab)
                {
                    // ⋮ 세로 점 세 개
                    const float Step = EditorStyle::Scale(5.0f), Radius = EditorStyle::Scale(1.6f);
                    for (int Dot = -1; Dot <= 1; ++Dot)
                        DrawList->AddCircleFilled({ Center.x, Center.y + Step * Dot }, Radius, Color);
                }
                else
                {
                    // 田(쿼드) 또는 □(싱글) 외곽선
                    const float Half = EditorStyle::Scale(7.0f);
                    const ImVec2 BoxMin{ Center.x - Half, Center.y - Half }, BoxMax{ Center.x + Half, Center.y + Half };
                    DrawList->AddRect(BoxMin, BoxMax, Color, EditorStyle::Scale(1.5f), 0, Line);
                    if (Desc.Glyph == EGlyph::Quad)
                    {
                        DrawList->AddLine({ Center.x, BoxMin.y }, { Center.x, BoxMax.y }, Color, Line);
                        DrawList->AddLine({ BoxMin.x, Center.y }, { BoxMax.x, Center.y }, Color, Line);
                    }
                }
            }
            else if (Desc.Icon)
                bClicked = ViewportToolbarWidgets::IconButton("##Item", Desc.Icon, Desc.Label, Desc.bChevron, ButtonWidth);
            else
                bClicked = ImGui::Button(Desc.Label, { ButtonWidth, 0.0f });
            ImGui::PopID();
            if (bClicked)
            {
                ActiveViewIndex = ViewIndex;
                if (Item == LayoutToggle)
                {
                    // UE처럼 바로 전환한다. 싱글 → 마지막 다중 패널(기본 쿼드), 다중 패널 → 이 뷰만 싱글
                    RequestedLayoutMode = CurrentLayoutMode == ELayoutMode::Single ? LastMultiPaneLayout : ELayoutMode::Single;
                    RequestedSingleViewIndex = ViewIndex;
                    bHasLayoutRequest = true;
                }
                else if (PopupIds[Item]) ImGui::OpenPopup(PopupIds[Item]);
                else if (Item >= Move && Item <= Scale) Gizmo->SetMode(static_cast<EGizmoMode>(Item - Move));
                else if (Item == Space)
                    Gizmo->SetSpace(Gizmo->GetSpace() == EGizmoSpace::World ? EGizmoSpace::Local : EGizmoSpace::World);
            }
            if (PopupIds[Item]) PopupAnchors[Item] = EditorStyle::LastItemAnchor();
            ImGui::EndDisabled();
            EditorStyle::PopSelectedButton(bSelected);
            if (Desc.Tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsPopupOpen(PopupIds[Item] ? PopupIds[Item] : ""))
                ImGui::SetTooltip("%s", Desc.Tooltip);
        }

        if (EditorStyle::BeginButtonPopup("Projection", PopupAnchors[Projection], EditorStyle::Scale(360.0f))) { DrawProjectionMenu(*Client); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("CameraSpeed", PopupAnchors[Speed], EditorStyle::Scale(300.0f))) { DrawCameraSpeedMenu(); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("ViewMode", PopupAnchors[ViewMode], EditorStyle::Scale(220.0f))) { DrawViewModeMenu(*Client); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Show", PopupAnchors[Show], EditorStyle::Scale(240.0f))) { DrawShowMenu(); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Render", PopupAnchors[Render])) { DrawRenderMenu(); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Layout", PopupAnchors[Layout], EditorStyle::Scale(250.0f))) { DrawLayoutMenu(ViewIndex); ImGui::EndPopup(); }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::PopID();
}

// UE 뷰포트의 투영 메뉴: 원근 / 직교 6방향 / 시야 설정
void FViewportsPanel::DrawProjectionMenu(FEditorViewportClient& Client)
{
    using namespace ViewportToolbarWidgets;
    const ELevelViewportType Current = Client.GetViewportType();

    SectionHeader("Perspective");
    if (IconMenuItem(ProjectionIcons[0], ProjectionLabels[0], Current == ELevelViewportType::Perspective, EIndicator::Radio))
        Client.SetViewportType(ELevelViewportType::Perspective);

    SectionHeader("Orthographic");
    for (const ELevelViewportType Type : OrthoOrder)
    {
        const int Index = static_cast<int>(Type);
        if (IconMenuItem(ProjectionIcons[Index], ProjectionLabels[Index], Current == Type, EIndicator::Radio))
            Client.SetViewportType(Type);
    }

    SectionHeader("View");
    // Location/Rotation의 세 칸이 좁아 보이지 않도록 넉넉히 준다.
    ImGui::PushItemWidth(EditorStyle::Scale(220.0f));
    if (Client.IsPerspective())
    {
        float Fov = Client.GetViewFOV();
        if (ImGui::DragFloat("Field of View", &Fov, 0.1f, 1.0f, 179.0f, "%.1f") && std::isfinite(Fov))
            Client.SetViewFOV(FMath::Clamp(Fov, 1.0f, 179.0f));
    }
    else
    {
        float OrthoWidth = Client.GetOrthoWidth();
        if (ImGui::DragFloat("Ortho Width", &OrthoWidth, 0.1f, 0.01f, 1000000.0f) && std::isfinite(OrthoWidth))
            Client.SetOrthoWidth(FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f));
    }
    float Near = Client.GetNearClip(), Far = Client.GetFarClip();
    bool bChanged = ImGui::InputFloat("Near Clip Plane", &Near);
    bChanged |= ImGui::InputFloat("Far Clip Plane", &Far);
    if (bChanged && std::isfinite(Near) && std::isfinite(Far) && Near > 0.0f && Far > Near)
        Client.SetClipPlanes(Near, Far);

    SectionHeader("Transform");
    FVector Location = Client.GetViewLocation();
    if (ImGui::DragFloat3("Location", &Location.X, 0.1f) &&
        std::isfinite(Location.X) && std::isfinite(Location.Y) && std::isfinite(Location.Z))
        Client.SetViewLocation(Location);
    FRotator Rotation = Client.GetViewRotation();
    float Angles[] = { Rotation.Pitch, Rotation.Yaw, Rotation.Roll };
    if (ImGui::DragFloat3("Rotation", Angles, 0.1f) &&
        std::isfinite(Angles[0]) && std::isfinite(Angles[1]) && std::isfinite(Angles[2]))
        Client.SetViewRotation(FRotator(Angles[0], Angles[1], Angles[2]));
    ImGui::PopItemWidth();
}

// UE의 카메라 속도 팝업
void FViewportsPanel::DrawCameraSpeedMenu()
{
    if (!EditorSettings) return;
    using namespace ViewportToolbarWidgets;
    auto& Settings = EditorSettings->GetMutableSettings();
    SectionHeader("Camera Speed");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##Speed", &Settings.CameraSpeed, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + EditorStyle::Scale(280.0f));
    ImGui::TextDisabled("Hold the right mouse button and scroll to change the speed quickly.");
    ImGui::PopTextWrapPos();
    SectionHeader("Settings");
    ImGui::SetNextItemWidth(EditorStyle::Scale(160.0f));
    ImGui::SliderFloat("Mouse Sensitivity", &Settings.MouseSensitivity, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

// UE의 뷰 모드 메뉴: 라이팅 포함 / 라이팅 제외 / 와이어프레임 / 버퍼 시각화 / 디버그
void FViewportsPanel::DrawViewModeMenu(FEditorViewportClient& Client)
{
    using namespace ViewportToolbarWidgets;
    const EViewportMode Current = Client.GetViewportMode();
    auto Item = [&](EViewportMode Mode)
    {
        const int Index = static_cast<int>(Mode);
        if (IconMenuItem(ViewModeIcons[Index], ViewModeLabels[Index], Current == Mode, EIndicator::Radio))
            Client.SetViewportMode(Mode);
    };
    Item(EViewportMode::Solid);
    Item(EViewportMode::Unlit);
    Item(EViewportMode::Wireframe);
    SectionHeader("Buffer Visualization");
    Item(EViewportMode::SceneDepth);
    Item(EViewportMode::WorldNormal);
    SectionHeader("Debug");
    Item(EViewportMode::LightCulling);
    Item(EViewportMode::Cascade);

    if (EditorSettings && Current == EViewportMode::SceneDepth)
    {
        SectionHeader("Scene Depth");
        ImGui::SetNextItemWidth(EditorStyle::Scale(120.0f));
        if (ImGui::DragFloat("Max Distance", &EditorSettings->GetMutableSettings().SceneDepthMaxRange,
            0.5f, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
            EditorSettings->ApplySceneDepthRange();
    }
}

// UE의 뷰포트 레이아웃 팝업. 큰 패널은 항상 View 0(원근)이 맡는다.
void FViewportsPanel::DrawLayoutMenu(int32 ViewIndex)
{
    using namespace ViewportToolbarWidgets;
    auto Request = [&](ELayoutMode Mode)
    {
        if (Mode != ELayoutMode::Single) LastMultiPaneLayout = Mode;
        RequestedLayoutMode = Mode;
        RequestedSingleViewIndex = ViewIndex;
        bHasLayoutRequest = true;
        ImGui::CloseCurrentPopup();
    };
    auto Row = [&](const char* Title, std::initializer_list<ELayoutMode> Modes)
    {
        SectionHeader(Title);
        bool bFirst = true;
        for (const ELayoutMode Mode : Modes)
        {
            if (!bFirst) ImGui::SameLine();
            bFirst = false;
            const int Index = static_cast<int>(Mode);
            if (LayoutButton(LayoutIcons[Index], Mode == CurrentLayoutMode, LayoutTooltips[Index]))
                Request(Mode);
        }
    };
    Row("One Pane", { ELayoutMode::Single });
    Row("Two Panes", { ELayoutMode::TwoPanes, ELayoutMode::TwoPanesStacked });
    Row("Three Panes", { ELayoutMode::ThreePanesLeft, ELayoutMode::ThreePanesRight,
        ELayoutMode::ThreePanesTop, ELayoutMode::ThreePanesBottom });
    Row("Four Panes", { ELayoutMode::QuadSplit, ELayoutMode::FourPanesLeft, ELayoutMode::FourPanesRight,
        ELayoutMode::FourPanesTop, ELayoutMode::FourPanesBottom });

    ImGui::Separator();
    // UE의 "뷰포트 최대화": 이 뷰만 크게 보거나 이전 다중 패널 레이아웃으로 돌아간다.
    const bool bMaximized = CurrentLayoutMode == ELayoutMode::Single;
    if (IconMenuItem(nullptr, "Maximize Viewport", bMaximized, EIndicator::Check))
        Request(bMaximized ? LastMultiPaneLayout : ELayoutMode::Single);
}

void FViewportsPanel::DrawSpawnMenu(FEditorViewportClient& Client)
{
    ImGui::BeginDisabled(!World || (IsPlayingQuery && IsPlayingQuery()));
    auto Spawn = [&](const char* Label, UClass* Class)
    {
        if (ImGui::MenuItem(Label))
        {
            AActor* Actor = World->SpawnActor(Class);
            if (Actor)
            {
                if (USceneComponent* Root = Actor->GetRootComponent())
                    Root->SetRelativeLocation(Client.GetViewLocation() + Client.GetViewForward() * 5.0f);
                if (OnActorSpawned) OnActorSpawned(Actor);
            }
        }
    };
    ImGui::SeparatorText("Basic");
    Spawn("Camera", ACameraActor::StaticClass());
    Spawn("Character", ACharacter::StaticClass());
    Spawn("Text", ATextRenderActor::StaticClass());
    ImGui::SeparatorText("Meshes");
    Spawn("Skeletal Mesh", ASkeletalMeshActor::StaticClass());
    Spawn("Static Mesh", AStaticMeshActor::StaticClass());
    ImGui::SeparatorText("Lights");
    Spawn("Ambient Light", AAmbientLightActor::StaticClass());
    Spawn("Directional Light", ADirectionalLightActor::StaticClass());
    Spawn("Point Light", APointLightActor::StaticClass());
    Spawn("Spot Light", ASpotLightActor::StaticClass());
    ImGui::SeparatorText("Effects");
    Spawn("Decal", ADecalActor::StaticClass());
    Spawn("Height Fog", AExponentialHeightFogActor::StaticClass());
    Spawn("Particle", AParticleActor::StaticClass());
    ImGui::EndDisabled();
    if (World) ImGui::TextDisabled("%d actors", World->GetActorNum());
}

// 언리얼의 그리드 크기 버튼처럼 자주 쓰는 간격을 고르거나 직접 입력한다.
void FViewportsPanel::DrawSpawnMenuForActiveView()
{
    if (FEditorViewportClient* Client = GetViewportClient(ActiveViewIndex))
        DrawSpawnMenu(*Client);
}

int32 FViewportsPanel::GetGridSpacing() const
{
    return EditorSettings ? EditorSettings->GetSettings().GridSpacing : 1;
}

void FViewportsPanel::DrawGridMenu()
{
    if (!EditorSettings) return;
    int32& Spacing = EditorSettings->GetMutableSettings().GridSpacing;
    ImGui::SeparatorText("Grid spacing");
    for (const int32 Preset : { 1, 5, 10, 50, 100 })
    {
        char Label[16];
        std::snprintf(Label, sizeof(Label), "%d", Preset);
        if (ImGui::MenuItem(Label, nullptr, Spacing == Preset)) Spacing = Preset;
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(EditorStyle::Scale(120.0f));
    ImGui::SliderInt("Custom", &Spacing, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
}
