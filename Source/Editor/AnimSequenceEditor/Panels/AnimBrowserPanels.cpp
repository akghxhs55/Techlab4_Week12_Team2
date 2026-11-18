#include "EnginePCH.h"
#include "AnimBrowserPanels.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorStyle.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace
{
    // 미리보기 메시 경로는 표기가 달라도 같은 파일이면 같게 본다.
    FString ResolveMeshPath(const FString& Path)
    {
        const FString Resolved = FAssetUtils::ResolveAssetFilePath(Path);
        return Resolved.empty() ? Path : Resolved;
    }

    FString FormatAssetDisplayName(const std::filesystem::path& FilePath)
    {
        FString Name = FPaths::ToUtf8(FilePath.filename());
        constexpr const char* AnimExt = ".anim.bin";
        const size_t ExtLen = std::strlen(AnimExt);
        if (Name.size() > ExtLen && Name.compare(Name.size() - ExtLen, ExtLen, AnimExt) == 0)
        {
            Name.resize(Name.size() - ExtLen);
            return Name;
        }
        return FPaths::ToUtf8(FilePath.stem());
    }

    FString FormatFileSizeText(uintmax_t Bytes)
    {
        char Buffer[32];
        if (Bytes >= 1024ull * 1024ull)
        {
            std::snprintf(Buffer, sizeof(Buffer), "%.2f MiB", static_cast<double>(Bytes) / (1024.0 * 1024.0));
        }
        else if (Bytes >= 1024ull)
        {
            std::snprintf(Buffer, sizeof(Buffer), "%.2f KiB", static_cast<double>(Bytes) / 1024.0);
        }
        else
        {
            std::snprintf(Buffer, sizeof(Buffer), "%llu B", static_cast<unsigned long long>(Bytes));
        }
        return Buffer;
    }

    void DrawAnimAssetIcon(ImDrawList* DL, ImVec2 Center, float Size)
    {
        const float W = Size * 0.72f;
        const float H = Size * 0.92f;
        const ImVec2 Min(Center.x - W * 0.5f, Center.y - H * 0.5f);
        const ImVec2 Max(Center.x + W * 0.5f, Center.y + H * 0.5f);
        const float Fold = W * 0.35f;
        const ImU32 FillColor = IM_COL32(108, 176, 86, 255);
        const ImU32 FoldColor = IM_COL32(76, 128, 60, 255);

        const ImVec2 Poly[5] = {
            Min,
            ImVec2(Max.x - Fold, Min.y),
            ImVec2(Max.x, Min.y + Fold),
            Max,
            ImVec2(Min.x, Max.y)
        };
        DL->AddConvexPolyFilled(Poly, 5, FillColor);
        DL->AddTriangleFilled(
            ImVec2(Max.x - Fold, Min.y),
            ImVec2(Max.x, Min.y + Fold),
            ImVec2(Max.x - Fold, Min.y + Fold),
            FoldColor);
    }
}

void FAnimSkeletonTreePanel::DrawContent()
{
    FBoneHierarchyPanel::DrawTree(Editor, SearchBuffer, sizeof(SearchBuffer), &LastSelectedBone);
}

void FAnimAssetBrowserView::Refresh(const FString& PreviewMeshPath)
{
    namespace fs = std::filesystem;

    bScanned = true;
    LastPreviewMeshPath = PreviewMeshPath;
    Entries.Empty();

    if (PreviewMeshPath.empty())
    {
        return;
    }
    const FString CurrentMesh = ResolveMeshPath(PreviewMeshPath);

    // 같은 미리보기 메시로 임포트한 애니메이션 = Unreal의 "같은 스켈레톤" 필터
    std::error_code Ec;
    const fs::path ContentRoot = FPaths::ProjectContentDir();
    for (fs::recursive_directory_iterator It(ContentRoot, fs::directory_options::skip_permission_denied, Ec), End;
        !Ec && It != End; It.increment(Ec))
    {
        if (!It->is_regular_file(Ec))
        {
            continue;
        }
        const FString Path = FPaths::ToUtf8(It->path());
        if (!AnimSequenceFile::IsAnimSequencePath(Path))
        {
            continue;
        }
        const UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path);
        if (Sequence && ResolveMeshPath(Sequence->GetPreviewMeshPath()) == CurrentMesh)
        {
            FString RelPath = FPaths::ToUtf8(It->path().lexically_relative(ContentRoot));
            std::replace(RelPath.begin(), RelPath.end(), '\\', '/');
            const uintmax_t FileSize = It->file_size(Ec);

            Entries.Add(FAnimAssetEntry{
                .FullPath = Path,
                .DisplayName = FormatAssetDisplayName(It->path()),
                .DisplayPath = "/Content/" + RelPath,
                .SizeText = Ec ? FString("-") : FormatFileSizeText(FileSize)
            });
        }
    }

    std::sort(Entries.begin(), Entries.end(), [](const FAnimAssetEntry& A, const FAnimAssetEntry& B)
    {
        if (A.DisplayName != B.DisplayName)
        {
            return A.DisplayName < B.DisplayName;
        }
        return A.DisplayPath < B.DisplayPath;
    });
}

bool FAnimAssetBrowserView::Draw(const FString& PreviewMeshPath, const FString& CurrentAnimPath, bool bShowDirtyNotice, FString& OutOpenedPath)
{
    if (!bScanned || LastPreviewMeshPath != PreviewMeshPath)
    {
        Refresh(PreviewMeshPath);
    }

    bool bOpened = false;

    // 돋보기 아이콘 + 검색창 + 지우기(X) + Refresh 버튼
    const char* RefreshLabel = "Refresh";
    const float RefreshWidth = ImGui::CalcTextSize(RefreshLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const ImGuiStyle& Style = ImGui::GetStyle();
    const bool bHasSearch = (SearchBuffer[0] != '\0');
    const float ClearWidth = bHasSearch ? ImGui::GetFrameHeight() : 0.0f;
    const float SearchWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - RefreshWidth - Style.ItemSpacing.x
        - (bHasSearch ? ClearWidth + Style.ItemSpacing.x : 0.0f));

    const ImTextureID SearchIcon = ViewportToolbarWidgets::GetIcon("Search");
    const float SearchIconSize = (SearchIcon != ImTextureID_Invalid) ? EditorStyle::Scale(14.0f) : 0.0f;
    const float SearchPadLeft = (SearchIcon != ImTextureID_Invalid) ? (SearchIconSize + EditorStyle::Scale(8.0f)) : 0.0f;

    ImGui::SetNextItemWidth(SearchWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style.FramePadding.x + SearchPadLeft, Style.FramePadding.y));
    ImGui::InputTextWithHint("##AnimSearch", "Search Assets...", SearchBuffer, sizeof(SearchBuffer));
    ImGui::PopStyleVar();

    if (SearchIcon != ImTextureID_Invalid)
    {
        const ImVec2 BoxMin = ImGui::GetItemRectMin();
        const ImVec2 BoxMax = ImGui::GetItemRectMax();
        const float IconX = BoxMin.x + Style.FramePadding.x + EditorStyle::Scale(2.0f);
        const float IconY = BoxMin.y + ((BoxMax.y - BoxMin.y) - SearchIconSize) * 0.5f;
        ImGui::GetWindowDrawList()->AddImage(SearchIcon,
            ImVec2(IconX, IconY), ImVec2(IconX + SearchIconSize, IconY + SearchIconSize),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(180, 185, 195, 220));
    }

    if (bHasSearch)
    {
        ImGui::SameLine();
        if (ImGui::Button("X##ClearAnimSearch", ImVec2(ClearWidth, ImGui::GetFrameHeight())))
        {
            SearchBuffer[0] = '\0';
        }
        ImGui::SetItemTooltip("Clear search");
    }

    ImGui::SameLine();
    if (ImGui::Button(RefreshLabel))
    {
        Refresh(PreviewMeshPath);
    }

    if (bShowDirtyNotice)
    {
        ImGui::TextDisabled("Save or revert to open another animation.");
    }

    if (Entries.IsEmpty())
    {
        ImGui::TextDisabled("No animations for this skeletal mesh.");
        return false;
    }

    // 이름 / 경로 / 크기 테이블 (회색-검은색 줄 번갈아 표시)
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.11f, 0.11f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    const ImGuiTableFlags TableFlags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_PadOuterX;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(EditorStyle::Scale(6.0f), EditorStyle::Scale(3.0f)));
    const bool bTableOpen = ImGui::BeginTable("##AnimAssetTable", 3, TableFlags);
    ImGui::PopStyleVar();

    if (bTableOpen)
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("Disk Size", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableHeadersRow();

        int32 ShownCount = 0;
        for (const FAnimAssetEntry& Entry : Entries)
        {
            if (!EditorStyle::ContainsIgnoreCase(Entry.DisplayName.c_str(), SearchBuffer) &&
                !EditorStyle::ContainsIgnoreCase(Entry.DisplayPath.c_str(), SearchBuffer))
            {
                continue;
            }
            ++ShownCount;

            const bool bCurrent = !CurrentAnimPath.empty() && (Entry.FullPath == CurrentAnimPath);
            const bool bSelected = bCurrent || (Entry.FullPath == SelectedPath);

            ImGui::TableNextRow();
            if (bCurrent)
            {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, EditorColor::WithAlpha(EditorColor::Accent, 180));
            }
            else if (bSelected)
            {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
            }

            // 1열: 아이콘 + 이름 (전체 행 클릭/더블클릭 선택 가능)
            ImGui::TableNextColumn();
            ImGui::PushID(Entry.FullPath.c_str());

            const float IconSide = EditorStyle::Scale(12.0f);
            const float TextIndent = IconSide + EditorStyle::Scale(6.0f);
            const ImVec2 RowStart = ImGui::GetCursorScreenPos();
            const float RowH = ImGui::GetTextLineHeight();

            const bool bClicked = ImGui::Selectable("##AnimRow", bSelected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, RowH));
            if (bClicked)
            {
                SelectedPath = Entry.FullPath;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    OutOpenedPath = Entry.FullPath;
                    bOpened = true;
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\n%s (%s)\nDouble-click to open",
                    Entry.DisplayName.c_str(), Entry.DisplayPath.c_str(), Entry.SizeText.c_str());
            }

            ImDrawList* DL = ImGui::GetWindowDrawList();
            const ImVec2 ItemMin = ImGui::GetItemRectMin();
            const ImVec2 ItemMax = ImGui::GetItemRectMax();
            const float CenterY = (ItemMin.y + ItemMax.y) * 0.5f;
            DrawAnimAssetIcon(DL, ImVec2(RowStart.x + IconSide * 0.5f, CenterY), IconSide);

            const float Col1Right = RowStart.x + ImGui::GetColumnWidth();
            const float TextX = RowStart.x + TextIndent;
            DL->PushClipRect(ImVec2(TextX, ItemMin.y), ImVec2(std::max(TextX + 1.0f, Col1Right), ItemMax.y), true);
            DL->AddText(ImVec2(TextX, CenterY - RowH * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Entry.DisplayName.c_str());
            DL->PopClipRect();

            // 2열: 경로
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Entry.DisplayPath.c_str());

            // 3열: 디스크 크기
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Entry.SizeText.c_str());

            ImGui::PopID();
        }

        if (ShownCount == 0)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("No matching animations.");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);

    return bOpened;
}

void FAnimAssetBrowserPanel::DrawContent()
{
    const UAnimSequence* Current = Editor.GetSequence();
    const FString PreviewMesh = Current ? Current->GetPreviewMeshPath() : FString();
    FString OpenedPath;
    if (BrowserView.Draw(PreviewMesh, Editor.GetAssetPath(), Editor.IsDirty(), OpenedPath))
    {
        Editor.OpenFromAssetBrowser(OpenedPath);
    }
}
