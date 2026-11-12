#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

// 공통 색상과 레이아웃 단위. 크기는 96 DPI를 기준으로 정의한다.
namespace EditorStyle
{
    inline float Scale(float Value) { return Value * ImGui::GetStyle().FontScaleDpi; }

    struct FPopupAnchor
    {
        ImVec2 Min;
        ImVec2 Max;
    };

    inline FPopupAnchor LastItemAnchor()
    {
        return { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
    }

    // 버튼 아래에 붙이고, 아래 공간이 부족하면 위로 연다. 긴 목록은 스크롤한다.
    inline bool BeginButtonPopup(const char* Id, const FPopupAnchor& Anchor, float Width = 0.0f)
    {
        if (!ImGui::IsPopupOpen(Id)) return false;

        const ImGuiViewport* Viewport = ImGui::GetWindowViewport();
        ImVec2 WorkMin = Viewport->WorkPos;
        ImVec2 WorkMax(WorkMin.x + Viewport->WorkSize.x, WorkMin.y + Viewport->WorkSize.y);
        const ImVec2 Center((Anchor.Min.x + Anchor.Max.x) * 0.5f, (Anchor.Min.y + Anchor.Max.y) * 0.5f);
        for (const ImGuiPlatformMonitor& Monitor : ImGui::GetPlatformIO().Monitors)
        {
            if (Center.x >= Monitor.MainPos.x && Center.y >= Monitor.MainPos.y &&
                Center.x < Monitor.MainPos.x + Monitor.MainSize.x && Center.y < Monitor.MainPos.y + Monitor.MainSize.y)
            {
                WorkMin = Monitor.WorkPos;
                WorkMax = ImVec2(WorkMin.x + Monitor.WorkSize.x, WorkMin.y + Monitor.WorkSize.y);
                break;
            }
        }

        const float Gap = Scale(4.0f);
        const float Below = ImMax(1.0f, WorkMax.y - Anchor.Max.y - Gap);
        const float Above = ImMax(1.0f, Anchor.Min.y - WorkMin.y - Gap);
        const bool bAbove = Below < ImMin(Scale(320.0f), Above);
        const float MaxWidth = ImMax(1.0f, WorkMax.x - WorkMin.x);
        const float MinWidth = ImMin(MaxWidth, ImMax(Anchor.Max.x - Anchor.Min.x, Width));
        const float MaxHeight = ImMin(Scale(480.0f), bAbove ? Above : Below);
        const ImGuiContext& Context = *ImGui::GetCurrentContext();
        const ImGuiWindow* PopupWindow = Context.OpenPopupStack[Context.BeginPopupStack.Size].Window;
        const float PopupWidth = ImClamp(PopupWindow ? PopupWindow->SizeFull.x : MinWidth, MinWidth, MaxWidth);
        ImGui::SetNextWindowSizeConstraints(ImVec2(MinWidth, 0.0f), ImVec2(MaxWidth, MaxHeight));
        ImGui::SetNextWindowPos(ImVec2(ImClamp(Anchor.Min.x, WorkMin.x, WorkMax.x - PopupWidth),
            bAbove ? Anchor.Min.y - Gap : Anchor.Max.y + Gap), ImGuiCond_Always, ImVec2(0.0f, bAbove ? 1.0f : 0.0f));
        return ImGui::BeginPopup(Id, ImGuiWindowFlags_NoMove);
    }

    inline void PushSelectedButton(bool bSelected)
    {
        if (!bSelected) return;
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_CheckboxSelectedBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.48f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.04f, 0.26f, 0.52f, 1.0f));
    }

    inline void PopSelectedButton(bool bSelected) { if (bSelected) ImGui::PopStyleColor(3); }

    inline void SameLineOrWrap(float NextWidth)
    {
        const float Right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + NextWidth <= Right)
            ImGui::SameLine();
    }

    inline void Apply()
    {
        ImGui::GetStyle() = ImGuiStyle();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Accent
        const ImVec4 Accent = ImVec4(0.06f, 0.37f, 0.70f, 1.00f);
        const ImVec4 AccentHover = ImVec4(0.12f, 0.48f, 0.85f, 1.00f);
        const ImVec4 AccentActive = ImVec4(0.04f, 0.26f, 0.52f, 1.00f);

        // Text
        colors[ImGuiCol_Text] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] =
            ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_TextLink] = AccentHover;
        colors[ImGuiCol_TextSelectedBg] =
            ImVec4(1.00f, 1.00f, 1.00f, 0.156f);

        // Window Background
        colors[ImGuiCol_WindowBg] =
            ImVec4(0.180f, 0.180f, 0.180f, 1.00f);
        colors[ImGuiCol_ChildBg] =
            ImVec4(0.280f, 0.280f, 0.280f, 0.00f);
        colors[ImGuiCol_PopupBg] =
            ImVec4(0.313f, 0.313f, 0.313f, 1.00f);
        colors[ImGuiCol_DockingEmptyBg] =
            colors[ImGuiCol_WindowBg];

        // Borders
        colors[ImGuiCol_Border] =
            ImVec4(0.266f, 0.266f, 0.266f, 1.00f);
        colors[ImGuiCol_BorderShadow] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Input Frames
        colors[ImGuiCol_FrameBg] =
            ImVec4(0.160f, 0.160f, 0.160f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.280f, 0.280f, 0.280f, 1.00f);
        colors[ImGuiCol_InputTextCursor] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

        // Title & Menu
        colors[ImGuiCol_TitleBg] =
            ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
        colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
        colors[ImGuiCol_MenuBarBg] =
            ImVec4(0.195f, 0.195f, 0.195f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.160f, 0.160f, 0.160f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.277f, 0.277f, 0.277f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.360f, 0.360f, 0.360f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = AccentActive;

        // Checkbox
        // 선택된 박스는 파랑, 체크 표시는 흰색
        colors[ImGuiCol_CheckboxSelectedBg] =
            ImVec4(Accent.x, Accent.y, Accent.z, 0.80f);
        colors[ImGuiCol_CheckMark] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

        // Slider
        colors[ImGuiCol_SliderGrab] =
            ImVec4(0.391f, 0.391f, 0.391f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = AccentHover;

        // Button
        colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.34f, 0.34f, 1.0f);
        colors[ImGuiCol_ButtonActive] = AccentActive;

        // Header, TreeNode, Selectable
        colors[ImGuiCol_Header] =
            ImVec4(0.313f, 0.313f, 0.313f, 1.00f);
        colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.400f, 0.400f, 0.400f, 1.00f);
        colors[ImGuiCol_HeaderActive] =
            ImVec4(0.469f, 0.469f, 0.469f, 1.00f);

        // Separator
        colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered] =
            ImVec4(0.391f, 0.391f, 0.391f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = Accent;

        // Resize Grip
        colors[ImGuiCol_ResizeGrip] =
            ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] =
            ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = Accent;

        // Tabs
        colors[ImGuiCol_Tab] =
            ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
        colors[ImGuiCol_TabHovered] =
            ImVec4(0.352f, 0.352f, 0.352f, 1.00f);
        colors[ImGuiCol_TabSelected] =
            ImVec4(0.250f, 0.250f, 0.250f, 1.00f);
        colors[ImGuiCol_TabDimmed] =
            ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] =
            ImVec4(0.195f, 0.195f, 0.195f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline] = Accent;
        colors[ImGuiCol_TabDimmedSelectedOverline] =
            ImVec4(0.08f, 0.29f, 0.48f, 1.00f);

        // Docking
        colors[ImGuiCol_DockingPreview] =
            ImVec4(Accent.x, Accent.y, Accent.z, 0.65f);

        // Graphs
        colors[ImGuiCol_PlotLines] =
            ImVec4(0.469f, 0.469f, 0.469f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = AccentHover;
        colors[ImGuiCol_PlotHistogram] =
            ImVec4(0.586f, 0.586f, 0.586f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = AccentHover;

        // Tables
        colors[ImGuiCol_TableHeaderBg] =
            ImVec4(0.240f, 0.240f, 0.240f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] =
            colors[ImGuiCol_Border];
        colors[ImGuiCol_TableBorderLight] =
            ImVec4(0.220f, 0.220f, 0.220f, 1.00f);
        colors[ImGuiCol_TableRowBg] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] =
            ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

        // Misc
        colors[ImGuiCol_TreeLines] =
            colors[ImGuiCol_Border];
        colors[ImGuiCol_DragDropTarget] = AccentHover;
        colors[ImGuiCol_DragDropTargetBg] =
            ImVec4(Accent.x, Accent.y, Accent.z, 0.18f);
        colors[ImGuiCol_UnsavedMarker] = Accent;
        colors[ImGuiCol_NavCursor] = Accent;
        colors[ImGuiCol_NavWindowingHighlight] = Accent;
        colors[ImGuiCol_NavWindowingDimBg] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
        colors[ImGuiCol_ModalWindowDimBg] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.586f);

        // ------------------------------------------------
        // Layout & Spacing
        // ------------------------------------------------

        // Padding
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;

        // Widget Sizes
        style.GrabMinSize = 10.0f;
        style.ScrollbarSize = 14.0f;

        // Rounding
        style.WindowRounding = 0.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 7.0f;
        style.TabRounding = 2.0f;

        // Borders
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;

        // Minimum Window Size
        style.WindowMinSize = ImVec2(160.0f, 100.0f);
    }
}
