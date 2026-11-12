#pragma once

// UE 뷰포트 툴바 모양의 ImGui 위젯.
// 아이콘은 UE5 에디터(Starship) SVG를 PNG로 바꾼 것이며 EngineResources/Icons/Viewport에 있다.
#include "Editor/EditorUI/EditorStyle.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include <unordered_map>
#include <string>

namespace ViewportToolbarWidgets
{
    // 아이콘 이름 → 텍스처. 없는 파일은 한 번만 찾고 Invalid로 기억한다.
    inline ImTextureID GetIcon(const char* Name)
    {
        if (!Name) return ImTextureID_Invalid;
        static std::unordered_map<std::string, ImTextureID> Cache;
        const auto Found = Cache.find(Name);
        if (Found != Cache.end()) return Found->second;
        ImTextureID Id = ImTextureID_Invalid;
        if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(FString("Engine/Icons/Viewport/") + Name + ".png"))
            if (Texture->GetResource() && Texture->GetResource()->GetSRV())
                Id = (ImTextureID)Texture->GetResource()->GetSRV();
        Cache.emplace(Name, Id);
        return Id;
    }

    inline float IconSize() { return EditorStyle::Scale(16.0f); }
    inline float ChevronSize() { return EditorStyle::Scale(8.0f); }
    inline ImU32 TextColor(bool bEnabled = true)
    {
        return ImGui::GetColorU32(bEnabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    }

    // 아래를 향한 작은 화살표 (UE의 small-chevron-down)
    inline void DrawChevron(ImDrawList* DrawList, ImVec2 Center, float Size, ImU32 Color)
    {
        const float Half = Size * 0.5f;
        DrawList->AddTriangleFilled({ Center.x - Half, Center.y - Half * 0.5f }, { Center.x + Half, Center.y - Half * 0.5f },
            { Center.x, Center.y + Half * 0.5f }, Color);
    }

    // [아이콘][라벨][v] 버튼 폭. 라벨이 없으면 아이콘만 둔다.
    inline float IconButtonWidth(const char* Label, bool bChevron)
    {
        const float Pad = ImGui::GetStyle().FramePadding.x;
        const float Gap = EditorStyle::Scale(5.0f);
        float Width = Pad * 2.0f + IconSize();
        if (Label && *Label) Width += Gap + ImGui::CalcTextSize(Label).x;
        if (bChevron) Width += Gap + ChevronSize();
        return Width;
    }

    inline bool IconButton(const char* Id, const char* Icon, const char* Label, bool bChevron, float Width)
    {
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        const float Height = ImGui::GetFrameHeight();
        const bool bClicked = ImGui::Button(Id, { Width, Height });
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const bool bEnabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0;
        const ImU32 Color = TextColor(bEnabled);
        const float Gap = EditorStyle::Scale(5.0f);
        float X = Pos.x + ImGui::GetStyle().FramePadding.x;
        const float CenterY = Pos.y + Height * 0.5f;
        if (const ImTextureID Texture = GetIcon(Icon); Texture != ImTextureID_Invalid)
            DrawList->AddImage(Texture, { X, CenterY - IconSize() * 0.5f }, { X + IconSize(), CenterY + IconSize() * 0.5f },
                { 0, 0 }, { 1, 1 }, bEnabled ? IM_COL32(220, 220, 220, 255) : IM_COL32(120, 120, 120, 255));
        X += IconSize();
        if (Label && *Label)
        {
            X += Gap;
            DrawList->AddText({ X, CenterY - ImGui::GetTextLineHeight() * 0.5f }, Color, Label);
            X += ImGui::CalcTextSize(Label).x;
        }
        if (bChevron)
            DrawChevron(DrawList, { X + Gap + ChevronSize() * 0.5f, CenterY }, ChevronSize(), Color);
        return bClicked;
    }

    // UE 메뉴의 회색 구역 제목과 가로줄
    inline void SectionHeader(const char* Title)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::SeparatorText(Title);
        ImGui::PopStyleColor();
    }

    enum class EIndicator { None, Radio, Check };

    // [표시][아이콘][라벨] 한 줄 메뉴 항목. Check 항목은 눌러도 팝업을 닫지 않는다.
    inline bool IconMenuItem(const char* Icon, const char* Label, bool bSelected, EIndicator Indicator)
    {
        const float Height = ImGui::GetFrameHeight();
        const float Indent = EditorStyle::Scale(22.0f);
        const float Gap = EditorStyle::Scale(8.0f);
        const float Needed = Indent + IconSize() + Gap + ImGui::CalcTextSize(Label).x + EditorStyle::Scale(12.0f);
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(Label);
        const ImGuiSelectableFlags Flags = Indicator == EIndicator::Check ? ImGuiSelectableFlags_NoAutoClosePopups : ImGuiSelectableFlags_None;
        const bool bClicked = ImGui::Selectable("##Item", false, Flags, { std::max(Needed, ImGui::GetContentRegionAvail().x), Height });
        ImGui::PopID();

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImU32 Color = TextColor();
        const float CenterY = Pos.y + Height * 0.5f;
        const float MarkX = Pos.x + EditorStyle::Scale(9.0f);
        if (Indicator == EIndicator::Radio)
        {
            // UE처럼 선택된 항목은 흰 점, 나머지는 어두운 점
            DrawList->AddCircleFilled({ MarkX, CenterY }, EditorStyle::Scale(3.0f),
                bSelected ? IM_COL32(240, 240, 240, 255) : IM_COL32(20, 20, 20, 255));
        }
        else if (Indicator == EIndicator::Check)
        {
            const float Half = EditorStyle::Scale(6.0f);
            const ImVec2 Min{ MarkX - Half, CenterY - Half }, Max{ MarkX + Half, CenterY + Half };
            DrawList->AddRectFilled(Min, Max, IM_COL32(20, 20, 20, 255), EditorStyle::Scale(2.0f));
            if (bSelected)
                ImGui::RenderCheckMark(DrawList, { Min.x + EditorStyle::Scale(2.0f), Min.y + EditorStyle::Scale(2.0f) },
                    IM_COL32(240, 240, 240, 255), Half * 2.0f - EditorStyle::Scale(4.0f));
        }
        float X = Pos.x + Indent;
        if (const ImTextureID Texture = GetIcon(Icon); Texture != ImTextureID_Invalid)
            DrawList->AddImage(Texture, { X, CenterY - IconSize() * 0.5f }, { X + IconSize(), CenterY + IconSize() * 0.5f },
                { 0, 0 }, { 1, 1 }, IM_COL32(200, 200, 200, 255));
        if (Icon) X += IconSize() + Gap;
        DrawList->AddText({ X, CenterY - ImGui::GetTextLineHeight() * 0.5f }, Color, Label);
        return bClicked;
    }

    // 레이아웃 미리보기 버튼. 지금 레이아웃은 파란 배경으로 표시한다.
    inline bool LayoutButton(const char* Icon, bool bSelected, const char* Tooltip)
    {
        const float Side = EditorStyle::Scale(36.0f);
        EditorStyle::PushSelectedButton(bSelected);
        ImGui::PushID(Icon);
        bool bClicked = false;
        if (const ImTextureID Texture = GetIcon(Icon); Texture != ImTextureID_Invalid)
            bClicked = ImGui::ImageButton("##Layout", Texture, { Side, Side });
        else
            bClicked = ImGui::Button("?", { Side, Side });
        ImGui::PopID();
        EditorStyle::PopSelectedButton(bSelected);
        if (Tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tooltip);
        return bClicked;
    }
}
