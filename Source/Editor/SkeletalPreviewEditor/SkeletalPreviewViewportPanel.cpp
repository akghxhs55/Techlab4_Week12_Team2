#include "EnginePCH.h"
#include "SkeletalPreviewViewportPanel.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const char* ProjectionLabels[] = { "Perspective", "Top", "Front", "Right", "Bottom", "Left", "Back" };
    const char* ProjectionIcons[] = { "ViewPerspective", "ViewTop", "ViewFront", "ViewRight", "ViewBottom", "ViewLeft", "ViewBack" };
    static_assert(IM_ARRAYSIZE(ProjectionLabels) == static_cast<int>(ELevelViewportType::Count));

    const ELevelViewportType OrthoOrder[] = {
        ELevelViewportType::Top, ELevelViewportType::Bottom,
        ELevelViewportType::Left, ELevelViewportType::Right,
        ELevelViewportType::Front, ELevelViewportType::Back
    };

    bool BeginIconMenu(const char* Icon, const char* Label)
    {
        const FString PaddedLabel = FString("      ") + Label;
        const bool bOpen = ImGui::BeginMenu(PaddedLabel.c_str());
        const ImVec2 Min = ImGui::GetItemRectMin();
        const ImVec2 Max = ImGui::GetItemRectMax();
        const float CenterY = (Min.y + Max.y) * 0.5f;
        const float X = Min.x + EditorStyle::Scale(4.0f);
        const float Size = ViewportToolbarWidgets::IconSize();
        if (const ImTextureID Tex = ViewportToolbarWidgets::GetIcon(Icon); Tex != ImTextureID_Invalid)
        {
            ImGui::GetWindowDrawList()->AddImage(Tex,
                ImVec2(X, CenterY - Size * 0.5f),
                ImVec2(X + Size, CenterY + Size * 0.5f),
                ImVec2(0, 0), ImVec2(1, 1), EditorColor::TextSecondary);
        }
        return bOpen;
    }

    void DrawIconFieldRow(const char* Icon, const char* Label, const std::function<void()>& DrawWidget)
    {
        const float Height = ImGui::GetFrameHeight();
        const float Size = ViewportToolbarWidgets::IconSize();
        const float LabelWidth = EditorStyle::Scale(118.0f);
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        if (const ImTextureID Tex = ViewportToolbarWidgets::GetIcon(Icon); Tex != ImTextureID_Invalid)
        {
            const float CenterY = Pos.y + Height * 0.5f;
            ImGui::GetWindowDrawList()->AddImage(Tex,
                ImVec2(Pos.x, CenterY - Size * 0.5f),
                ImVec2(Pos.x + Size, CenterY + Size * 0.5f),
                ImVec2(0, 0), ImVec2(1, 1), EditorColor::TextSecondary);
        }
        ImGui::SetCursorScreenPos(ImVec2(Pos.x + Size + EditorStyle::Scale(6.0f), Pos.y));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Label);
        ImGui::SameLine(LabelWidth + Size + EditorStyle::Scale(10.0f));
        ImGui::SetNextItemWidth(EditorStyle::Scale(90.0f));
        DrawWidget();
    }

    void DrawCameraSpeedPopupContent(FSkeletalPreviewEditor& Editor)
    {
        using namespace ViewportToolbarWidgets;
        SectionHeader("Camera Speed");
        float Speed = Editor.GetCameraSpeed();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##PreviewCamSpeedSlider", &Speed, 0.1f, 150.0f, "%.1f",
            ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            Editor.SetCameraSpeed(Speed);
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + EditorStyle::Scale(250.0f));
        ImGui::TextDisabled("Hold the right mouse button and scroll to change the speed quickly.");
        ImGui::PopTextWrapPos();
    }

    void DrawProjectionMenu(FSkeletalPreviewEditor& Editor, FEditorViewportClient& Client)
    {
        using namespace ViewportToolbarWidgets;
        const ELevelViewportType Current = Client.GetViewportType();

        SectionHeader("Perspective");
        if (IconMenuItem(ProjectionIcons[0], ProjectionLabels[0], Current == ELevelViewportType::Perspective, EIndicator::Radio))
        {
            Client.SetViewportType(ELevelViewportType::Perspective);
            Editor.FocusMesh();
        }

        SectionHeader("Orthographic");
        for (const ELevelViewportType Type : OrthoOrder)
        {
            const int Index = static_cast<int>(Type);
            if (IconMenuItem(ProjectionIcons[Index], ProjectionLabels[Index], Current == Type, EIndicator::Radio))
            {
                Client.SetViewportType(Type);
                Editor.FocusMesh();
            }
        }

        SectionHeader("Movement");
        if (BeginIconMenu("CameraSpeed", "Camera Speed"))
        {
            DrawCameraSpeedPopupContent(Editor);
            ImGui::EndMenu();
        }
        if (IconMenuItem("FocusMesh", "Focus Mesh (F)", false, EIndicator::None))
        {
            Editor.FocusMesh();
        }

        SectionHeader("View");
        if (Client.IsPerspective())
        {
            DrawIconFieldRow("FieldOfView", "Field of View", [&]()
            {
                float Fov = Client.GetViewFOV();
                if (ImGui::DragFloat("##PreviewFOV", &Fov, 0.1f, 1.0f, 179.0f, "%.2f") && std::isfinite(Fov))
                {
                    Client.SetViewFOV(FMath::Clamp(Fov, 1.0f, 179.0f));
                }
            });
        }
        else
        {
            DrawIconFieldRow("FieldOfView", "Ortho Width", [&]()
            {
                float OrthoWidth = Client.GetOrthoWidth();
                if (ImGui::DragFloat("##PreviewOrthoW", &OrthoWidth, 0.1f, 0.01f, 1000000.0f, "%.2f") && std::isfinite(OrthoWidth))
                {
                    Client.SetOrthoWidth(FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f));
                }
            });
        }

        float Near = Client.GetNearClip();
        float Far = Client.GetFarClip();
        bool bClipChanged = false;
        DrawIconFieldRow("NearClipPlane", "Near Clip Plane", [&]()
        {
            bClipChanged |= ImGui::InputFloat("##PreviewNearClip", &Near, 0.0f, 0.0f, "%.2f");
        });
        DrawIconFieldRow("FarClipPlane", "Far Clip Plane", [&]()
        {
            bClipChanged |= ImGui::InputFloat("##PreviewFarClip", &Far, 0.0f, 0.0f, "%.1f");
        });
        if (bClipChanged && std::isfinite(Near) && std::isfinite(Far) && Near > 0.0f && Far > Near)
        {
            Client.SetClipPlanes(Near, Far);
        }
    }

    void DrawViewModeMenu(FEditorViewportClient& Client)
    {
        using namespace ViewportToolbarWidgets;
        const EViewportMode Current = Client.GetViewportMode();

        SectionHeader("View Mode");
        if (IconMenuItem("Lit", "Lit", Current == EViewportMode::Solid, EIndicator::Radio))
        {
            Client.SetViewportMode(EViewportMode::Solid);
        }
        if (IconMenuItem("Unlit", "Unlit", Current == EViewportMode::Unlit, EIndicator::Radio))
        {
            Client.SetViewportMode(EViewportMode::Unlit);
        }
        if (IconMenuItem("Wireframe", "Wireframe", Current == EViewportMode::Wireframe, EIndicator::Radio))
        {
            Client.SetViewportMode(EViewportMode::Wireframe);
        }

        SectionHeader("Buffer Visualization");
        if (IconMenuItem("BufferVisualization", "World Normal", Current == EViewportMode::WorldNormal, EIndicator::Radio))
        {
            Client.SetViewportMode(EViewportMode::WorldNormal);
        }
    }

    void DrawShowFlagsMenu(FSkeletalPreviewEditor& Editor)
    {
        using namespace ViewportToolbarWidgets;

        SectionHeader("Scene Elements");
        if (BeginIconMenu("SkeletalMesh", "Mesh"))
        {
            SectionHeader("Mesh");
            const bool bShowMesh = Editor.IsMeshVisible();
            if (IconMenuItem("SkeletalMesh", "Mesh", bShowMesh, EIndicator::Check))
            {
                Editor.SetMeshVisible(!bShowMesh);
            }

            SectionHeader("Mesh Overlay Drawing");
            const bool bHeatmap = Editor.IsWeightHeatmapEnabled();
            if (IconMenuItem(nullptr, "None", !bHeatmap, EIndicator::Radio))
            {
                Editor.SetWeightHeatmap(false);
            }
            if (IconMenuItem(nullptr, "Selected Bone Weight", bHeatmap, EIndicator::Radio))
            {
                Editor.SetWeightHeatmap(true);
            }
            ImGui::EndMenu();
        }

        if (BeginIconMenu("Bone", "Bones"))
        {
            SectionHeader("Bone Drawing");
            const EBoneDrawMode BoneMode = Editor.GetBoneDrawMode();
            if (IconMenuItem("Bone", "All Hierarchy", BoneMode == EBoneDrawMode::AllHierarchy, EIndicator::Radio))
            {
                Editor.SetBoneDrawMode(EBoneDrawMode::AllHierarchy);
            }
            if (IconMenuItem("Bone", "Selected Only", BoneMode == EBoneDrawMode::SelectedOnly, EIndicator::Radio))
            {
                Editor.SetBoneDrawMode(EBoneDrawMode::SelectedOnly);
            }
            if (IconMenuItem("Bone", "Selected and Parents", BoneMode == EBoneDrawMode::SelectedAndParents, EIndicator::Radio))
            {
                Editor.SetBoneDrawMode(EBoneDrawMode::SelectedAndParents);
            }
            if (IconMenuItem(nullptr, "None", BoneMode == EBoneDrawMode::None, EIndicator::Radio))
            {
                Editor.SetBoneDrawMode(EBoneDrawMode::None);
            }

            SectionHeader("Bone Color");
            FVector4 BoneColor = Editor.GetBoneVisualizerColor();
            ImGui::SetNextItemWidth(EditorStyle::Scale(170.0f));
            if (ImGui::ColorEdit3("##BoneColor", &BoneColor.X))
            {
                Editor.SetBoneVisualizerColor(BoneColor);
            }
            ImGui::EndMenu();
        }

        SectionHeader("Common Show Flags");
        const bool bShowGrid = Editor.IsGridVisible();
        if (IconMenuItem("Grid", "Grid", bShowGrid, EIndicator::Check))
        {
            Editor.SetGridVisible(!bShowGrid);
        }
    }
}

void DrawPreviewViewToolbar(FSkeletalPreviewEditor& Editor, FPreviewViewportToolbarState& State)
{
    using namespace ViewportToolbarWidgets;

    FEditorViewportClient* Client = Editor.GetViewportClient();
    const float PadX = EditorStyle::Scale(6.0f);
    const float PadY = EditorStyle::Scale(4.0f);
    const float Gap = EditorStyle::Scale(4.0f);
    const float BarHeight = ImGui::GetFrameHeight() + PadY * 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PadX, PadY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgRaised);
    const bool bToolbarOpen = ImGui::BeginChild("##PreviewViewToolbar", ImVec2(0.0f, BarHeight),
        ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (bToolbarOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Gap, PadY));

        const int ProjIdx = Client
            ? std::clamp(static_cast<int>(Client->GetViewportType()), 0, static_cast<int>(ELevelViewportType::Count) - 1)
            : 0;
        const char* ProjLabel = ProjectionLabels[ProjIdx];

        char SpeedLabel[16];
        std::snprintf(SpeedLabel, sizeof(SpeedLabel), "%.1f", Editor.GetCameraSpeed());

        const EViewportMode CurrentMode = Client ? Client->GetViewportMode() : EViewportMode::Solid;
        const char* ModeLabel = "Lit";
        const char* ModeIcon = "Lit";
        switch (CurrentMode)
        {
        case EViewportMode::Unlit:       ModeLabel = "Unlit"; ModeIcon = "Unlit"; break;
        case EViewportMode::Wireframe:   ModeLabel = "Wireframe"; ModeIcon = "Wireframe"; break;
        case EViewportMode::WorldNormal: ModeLabel = "World Normal"; ModeIcon = "BufferVisualization"; break;
        default: break;
        }

        const float ProjWidth = Client ? IconButtonWidth(ProjLabel, true) : 0.0f;
        const float SpeedWidth = IconButtonWidth(SpeedLabel, true);
        const float ModeWidth = Client ? IconButtonWidth(ModeLabel, true) : 0.0f;
        const float ShowWidth = IconButtonWidth(nullptr, true);

        float TotalWidth = SpeedWidth + Gap + ShowWidth;
        if (Client)
        {
            TotalWidth += ProjWidth + Gap + ModeWidth + Gap;
        }

        const float StartX = std::max(PadX, ImGui::GetWindowWidth() - PadX - TotalWidth);
        ImGui::SetCursorPosX(StartX);

        EditorStyle::FPopupAnchor ProjAnchor{};
        EditorStyle::FPopupAnchor SpeedAnchor{};
        EditorStyle::FPopupAnchor ModeAnchor{};
        EditorStyle::FPopupAnchor ShowAnchor{};

        // 1. 카메라 투영 방식 버튼 (Perspective / Orthographic / FOV / Clip)
        if (Client)
        {
            if (IconButton("##PreviewProjBtn", "Camera", ProjLabel, true, ProjWidth))
            {
                ImGui::OpenPopup("PreviewProjectionPopup");
            }
            ProjAnchor = EditorStyle::LastItemAnchor();
            if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("PreviewProjectionPopup"))
            {
                ImGui::SetTooltip("Viewport type and camera");
            }
            ImGui::SameLine();
        }

        // 2. 카메라 이동 속도 버튼 (클릭 시 팝업, 좌우 드래그 시 실시간 속도 조절)
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            State.bCameraSpeedDragged = false;
        }
        const bool bSpeedClicked = IconButton("##PreviewCamSpeedBtn", "CameraSpeed", SpeedLabel, true, SpeedWidth);
        SpeedAnchor = EditorStyle::LastItemAnchor();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
        {
            Editor.SetCameraSpeed(Editor.GetCameraSpeed() + ImGui::GetIO().MouseDelta.x * 0.1f);
            State.bCameraSpeedDragged = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        else if (bSpeedClicked && !State.bCameraSpeedDragged)
        {
            ImGui::OpenPopup("PreviewCameraSpeedPopup");
        }
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("PreviewCameraSpeedPopup"))
        {
            ImGui::SetTooltip("Camera speed\nClick to adjust or hold right mouse button and scroll.");
        }

        // 3. 라이팅 / 뷰 모드 버튼 (Lit / Unlit / Wireframe / World Normal)
        if (Client)
        {
            ImGui::SameLine();
            if (IconButton("##PreviewViewModeBtn", ModeIcon, ModeLabel, true, ModeWidth))
            {
                ImGui::OpenPopup("PreviewViewModePopup");
            }
            ModeAnchor = EditorStyle::LastItemAnchor();
            if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("PreviewViewModePopup"))
            {
                ImGui::SetTooltip("View mode");
            }
        }

        // 4. Visible (Show Flags) 버튼
        ImGui::SameLine();
        if (IconButton("##PreviewShowFlagsBtn", "visible", nullptr, true, ShowWidth))
        {
            ImGui::OpenPopup("PreviewShowFlagsPopup");
        }
        ShowAnchor = EditorStyle::LastItemAnchor();
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("PreviewShowFlagsPopup"))
        {
            ImGui::SetTooltip("Show flags (Mesh, Bones, Grid)");
        }

        if (Client && EditorStyle::BeginButtonPopup("PreviewProjectionPopup", ProjAnchor, EditorStyle::Scale(270.0f)))
        {
            DrawProjectionMenu(Editor, *Client);
            ImGui::EndPopup();
        }
        if (EditorStyle::BeginButtonPopup("PreviewCameraSpeedPopup", SpeedAnchor, EditorStyle::Scale(270.0f)))
        {
            DrawCameraSpeedPopupContent(Editor);
            ImGui::EndPopup();
        }
        if (Client && EditorStyle::BeginButtonPopup("PreviewViewModePopup", ModeAnchor, EditorStyle::Scale(200.0f)))
        {
            DrawViewModeMenu(*Client);
            ImGui::EndPopup();
        }
        if (EditorStyle::BeginButtonPopup("PreviewShowFlagsPopup", ShowAnchor, EditorStyle::Scale(220.0f)))
        {
            DrawShowFlagsMenu(Editor);
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar();
    }

    const ImVec2 BarMin = ImGui::GetWindowPos();
    const ImVec2 BarSize = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(BarMin.x, BarMin.y + BarSize.y - 1.0f),
        ImVec2(BarMin.x + BarSize.x, BarMin.y + BarSize.y - 1.0f),
        EditorColor::Border);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void FSkeletalPreviewViewportPanel::DrawContent()
{
    DrawPreviewViewToolbar(Editor, ToolbarState);

    FEditorViewportClient* Client = Editor.GetViewportClient();
    const ImVec2 Size = ImGui::GetContentRegionAvail();
    if (!Client || Size.x < 1.0f || Size.y < 1.0f)
    {
        return;
    }

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    // 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지한다
    ImGui::InvisibleButton("##AnimViewport", Size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (FTexture2D* Target = Client->GetColorTarget(); Target && Target->GetSRV())
    {
        ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin,
            ImVec2(Origin.x + Size.x, Origin.y + Size.y));
    }

    const bool bHovered = ImGui::IsItemHovered();
    if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        ImGui::SetWindowFocus();
    }

    const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        Editor.FocusMesh();
    }

    const ImGuiIO& IO = ImGui::GetIO();
    const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const FViewportPointerInput PointerInput
    {
        .MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
        .bHovered = bHovered,
        .bFocused = bFocused && !IO.AppFocusLost,
        .bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        .bMouseDown = bMouseDown,
        .bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
    };

    const FViewportCameraInput CameraInput
    {
        .bCaptured = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown,
        .bAllowWheel = bHovered && !bMouseDown,
        .bForward = ImGui::IsKeyDown(ImGuiKey_W),
        .bBackward = ImGui::IsKeyDown(ImGuiKey_S),
        .bRight = ImGui::IsKeyDown(ImGuiKey_D),
        .bLeft = ImGui::IsKeyDown(ImGuiKey_A),
        .bUp = ImGui::IsKeyDown(ImGuiKey_E),
        .bDown = ImGui::IsKeyDown(ImGuiKey_Q),
        .MouseX = IO.MouseDelta.x,
        .MouseY = IO.MouseDelta.y,
        // ImGui의 휠 단위를 카메라 이동 함수가 쓰는 Windows 휠 단위로 바꾼다
        .Wheel = IO.MouseWheel * 120.0f,
    };

    Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), CameraInput, PointerInput);
}
