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
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.27f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.13f, 0.11f, 1.0f));
    }

    inline void PopSelectedButton(bool bSelected) { if (bSelected) ImGui::PopStyleColor(3); }

    // 아웃라이너처럼 패널 맨 위에 두는 꽉 찬 폭의 검색창. 내용이 바뀌면 true.
    inline bool SearchBox(const char* Id, char* Buffer, size_t BufferSize)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::InputTextWithHint(Id, "Search...", Buffer, BufferSize);
    }

    // 대소문자를 무시한 부분 문자열 검색. 빈 패턴은 항상 일치한다.
    inline bool ContainsIgnoreCase(const char* Text, const char* Pattern)
    {
        if (!Pattern || !*Pattern) return true;
        if (!Text) return false;
        for (; *Text; ++Text)
        {
            const char* T = Text;
            const char* P = Pattern;
            while (*T && *P && ImToUpper(*T) == ImToUpper(*P)) { ++T; ++P; }
            if (!*P) return true;
        }
        return false;
    }

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

        // Accent (마키세 크리스 적갈색)
        const ImVec4 Accent = ImVec4(0.62f, 0.20f, 0.17f, 1.00f);
        const ImVec4 AccentHover = ImVec4(0.75f, 0.27f, 0.23f, 1.00f);
        const ImVec4 AccentActive = ImVec4(0.45f, 0.13f, 0.11f, 1.00f);

        // Text
        colors[ImGuiCol_Text] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] =
            ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_TextLink] = AccentHover;
        colors[ImGuiCol_TextSelectedBg] =
            ImVec4(1.00f, 1.00f, 1.00f, 0.156f);

        // Window Background (딥 블랙 계층)
        colors[ImGuiCol_WindowBg] =
            ImVec4(0.065f, 0.065f, 0.072f, 1.00f);
        colors[ImGuiCol_ChildBg] =
            ImVec4(0.080f, 0.080f, 0.088f, 0.00f);
        colors[ImGuiCol_PopupBg] =
            ImVec4(0.110f, 0.110f, 0.120f, 1.00f);
        colors[ImGuiCol_DockingEmptyBg] =
            colors[ImGuiCol_WindowBg];

        // Borders
        colors[ImGuiCol_Border] =
            ImVec4(0.180f, 0.170f, 0.175f, 1.00f);
        colors[ImGuiCol_BorderShadow] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Input Frames
        colors[ImGuiCol_FrameBg] =
            ImVec4(0.100f, 0.100f, 0.110f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.145f, 0.145f, 0.155f, 1.00f);
        colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.200f, 0.175f, 0.178f, 1.00f);
        colors[ImGuiCol_InputTextCursor] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

        // Title & Menu
        colors[ImGuiCol_TitleBg] =
            ImVec4(0.050f, 0.050f, 0.056f, 1.00f);
        colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.050f, 0.050f, 0.056f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.050f, 0.050f, 0.056f, 1.00f);
        colors[ImGuiCol_MenuBarBg] =
            ImVec4(0.075f, 0.075f, 0.082f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.055f, 0.055f, 0.060f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.200f, 0.200f, 0.210f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.300f, 0.280f, 0.285f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = AccentActive;

        // Checkbox
        // 선택된 박스는 적갈색, 체크 표시는 흰색
        colors[ImGuiCol_CheckboxSelectedBg] =
            ImVec4(Accent.x, Accent.y, Accent.z, 0.85f);
        colors[ImGuiCol_CheckMark] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

        // Slider
        colors[ImGuiCol_SliderGrab] =
            ImVec4(0.391f, 0.391f, 0.391f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = AccentHover;

        // Button
        colors[ImGuiCol_Button] = ImVec4(0.140f, 0.140f, 0.150f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.215f, 0.205f, 0.210f, 1.0f);
        colors[ImGuiCol_ButtonActive] = AccentActive;

        // Header, TreeNode, Selectable
        colors[ImGuiCol_Header] =
            ImVec4(0.145f, 0.145f, 0.155f, 1.00f);
        colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.220f, 0.185f, 0.185f, 1.00f);
        colors[ImGuiCol_HeaderActive] =
            ImVec4(0.300f, 0.170f, 0.165f, 1.00f);

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
            ImVec4(0.055f, 0.055f, 0.060f, 1.00f);
        colors[ImGuiCol_TabHovered] =
            ImVec4(0.185f, 0.160f, 0.165f, 1.00f);
        colors[ImGuiCol_TabSelected] =
            ImVec4(0.130f, 0.130f, 0.140f, 1.00f);
        colors[ImGuiCol_TabDimmed] =
            ImVec4(0.050f, 0.050f, 0.055f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] =
            ImVec4(0.105f, 0.105f, 0.115f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline] = Accent;
        colors[ImGuiCol_TabDimmedSelectedOverline] =
            ImVec4(0.42f, 0.14f, 0.12f, 1.00f);

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
            ImVec4(0.115f, 0.115f, 0.125f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] =
            colors[ImGuiCol_Border];
        colors[ImGuiCol_TableBorderLight] =
            ImVec4(0.145f, 0.145f, 0.155f, 1.00f);
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
