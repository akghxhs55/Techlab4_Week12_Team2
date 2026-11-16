#include "EnginePCH.h"
#include "AnimBrowserPanels.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorStyle.h"

#include <algorithm>
#include <filesystem>

namespace
{
    // 미리보기 메시 경로는 표기가 달라도 같은 파일이면 같게 본다.
    FString ResolveMeshPath(const FString& Path)
    {
        const FString Resolved = FAssetUtils::ResolveAssetFilePath(Path);
        return Resolved.empty() ? Path : Resolved;
    }
}

void FAnimSkeletonTreePanel::DrawContent()
{
    FBoneHierarchyPanel::DrawTree(Editor, SearchBuffer, sizeof(SearchBuffer));
}

void FAnimAssetBrowserPanel::Refresh()
{
    namespace fs = std::filesystem;

    bScanned = true;
    Entries.Empty();

    const UAnimSequence* Current = Editor.GetSequence();
    if (!Current || Current->GetPreviewMeshPath().empty())
    {
        return;
    }
    const FString CurrentMesh = ResolveMeshPath(Current->GetPreviewMeshPath());

    // 같은 미리보기 메시로 임포트한 애니메이션 = Unreal의 "같은 스켈레톤" 필터
    std::error_code Ec;
    for (fs::recursive_directory_iterator It(FPaths::ProjectContentDir(), fs::directory_options::skip_permission_denied, Ec), End;
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
            Entries.Add(Path);
        }
    }

    std::sort(Entries.begin(), Entries.end());
}

void FAnimAssetBrowserPanel::DrawContent()
{
    if (!bScanned)
    {
        Refresh();
    }

    // 다른 패널과 같은 "Search..." 검색창. Refresh는 오른쪽 끝에 둔다.
    const char* RefreshLabel = "Refresh";
    const float RefreshWidth = ImGui::CalcTextSize(RefreshLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - RefreshWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##AnimSearch", "Search...", SearchBuffer, sizeof(SearchBuffer));
    ImGui::SameLine();
    if (ImGui::Button(RefreshLabel))
    {
        Refresh();
    }

    if (Editor.IsDirty())
    {
        ImGui::TextDisabled("Save or revert to open another animation.");
    }

    if (ImGui::BeginChild("AnimAssetList"))
    {
        for (const FString& Path : Entries)
        {
            const FString Name = std::filesystem::path(Path).filename().string();
            if (!EditorStyle::ContainsIgnoreCase(Name.c_str(), SearchBuffer))
            {
                continue;
            }

            const bool bCurrent = Path == Editor.GetAssetPath();
            ImGui::PushID(Path.c_str());
            if (ImGui::Selectable(Name.c_str(), bCurrent, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                Editor.OpenFromAssetBrowser(Path);
            }
            ImGui::SetItemTooltip("%s", Path.c_str());
            ImGui::PopID();
        }
        if (Entries.IsEmpty())
        {
            ImGui::TextDisabled("No animations for this preview mesh.");
        }
    }
    ImGui::EndChild();
}
