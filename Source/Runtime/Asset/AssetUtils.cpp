#include "EnginePCH.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include <algorithm>

namespace
{
    bool IsBuiltinKey(const fs::path& Path) { return !Path.has_parent_path() && !Path.has_extension(); }
    bool RelativeInside(const fs::path& File, const fs::path& Root, fs::path& Relative)
    {
        const fs::path NormalFile = File.lexically_normal().make_preferred();
        const fs::path NormalRoot = Root.lexically_normal().make_preferred();
        auto Part = NormalFile.begin();
        for (const auto& RootPart : NormalRoot)
        {
            if (Part == NormalFile.end() || _wcsicmp(Part->c_str(), RootPart.c_str()) != 0) return false;
            ++Part;
        }
        Relative.clear();
        for (; Part != NormalFile.end(); ++Part) Relative /= *Part;
        return true;
    }
}
FString FAssetUtils::NormalizeAssetPath(const FString& Path)
{
    if (Path.empty()) return {};
    FString Normal = Path;
    std::replace(Normal.begin(), Normal.end(), '\\', '/');
    const fs::path Result = FPaths::FromUtf8(Normal).lexically_normal();
    return Result == "." ? FString{} : FPaths::ToUtf8(Result);
}
FString FAssetUtils::ResolveAssetFilePath(const FString& Path)
{
    const FString Normal = NormalizeAssetPath(Path);
    if (Normal.empty() || IsBuiltinKey(FPaths::FromUtf8(Normal))) return {};
    fs::path File = FPaths::FromUtf8(FPaths::ResolveContentPath(Normal));
    if (File.is_relative() && *File.begin() != "..") File = FPaths::ProjectContentDir() / File;
    std::error_code Ec;
    File = fs::absolute(File, Ec).lexically_normal();
    return Ec ? FString{} : FPaths::ToUtf8(File);
}
FString FAssetUtils::GetAssetKey(const FString& Path)
{
    const FString Normal = NormalizeAssetPath(Path);
    if (Normal.empty() || IsBuiltinKey(FPaths::FromUtf8(Normal))) return Normal;
    const FString Resolved = ResolveAssetFilePath(Normal);
    if (Resolved.empty()) return {};
    const fs::path File = FPaths::FromUtf8(Resolved);
    fs::path Relative;
    if (RelativeInside(File, FPaths::EngineResourcesDir(), Relative)) return FString(FPaths::EngineContentPrefix) + FPaths::ToUtf8(Relative);
    if (RelativeInside(File, FPaths::ProjectContentDir(), Relative)) return FPaths::ToUtf8(Relative);
    return Resolved;
}
