#include "EnginePCH.h"
#include "Core/Misc/Paths.h"
#include <fstream>
#include <algorithm>
#include <Windows.h>

namespace fs = std::filesystem;
namespace
{
    fs::path GEngineDir, GProjectDir, GProjectFile;
    FString GProjectName;
    bool StartsWithFolder(const FString& Path, const char* Folder)
    {
        const size_t Length = std::strlen(Folder);
        return Path.size() > Length && _strnicmp(Path.c_str(), Folder, Length) == 0 && Path[Length] == '/';
    }
    fs::path ProjectRoot() { return GProjectDir.empty() ? GEngineDir : GProjectDir; }
    bool IsInside(const fs::path& Relative)
    {
        return !Relative.empty() && !Relative.is_absolute() && *Relative.begin() != "..";
    }
}

bool FPaths::InitEngineDir()
{
    wchar_t ExePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, ExePath, MAX_PATH)) return false;
    std::error_code Ec;
    for (fs::path Dir = fs::path(ExePath).parent_path(); !Dir.empty();)
    {
        if (fs::is_directory(Dir / L"EngineResources/Shaders", Ec)) { GEngineDir = Dir; return true; }
        const fs::path Parent = Dir.parent_path();
        if (Parent == Dir) break;
        Dir = Parent;
    }
    GEngineDir = fs::current_path(Ec);
    return fs::is_directory(GEngineDir / L"EngineResources/Shaders", Ec);
}
const fs::path& FPaths::EngineDir() { return GEngineDir; }
const fs::path& FPaths::ProjectDir() { return GProjectDir; }
const fs::path& FPaths::ProjectFilePath() { return GProjectFile; }
const FString& FPaths::ProjectName() { return GProjectName; }
bool FPaths::HasProject() { return !GProjectFile.empty(); }

bool FPaths::SetProject(const fs::path& ProjectFile, FString* OutError)
{
    std::error_code Ec;
    const fs::path Absolute = fs::absolute(ProjectFile, Ec).lexically_normal();
    FProjectDescriptor Descriptor;
    if (Ec || !Descriptor.Load(Absolute, OutError)) return false;
    const fs::path Dir = Absolute.parent_path();
    if (!SetCurrentDirectoryW(Dir.c_str()))
    {
        if (OutError) *OutError = "Cannot enter project folder: " + ToUtf8(Dir);
        return false;
    }
    GProjectFile = Absolute; GProjectDir = Dir;
    GProjectName = Descriptor.Name.empty() ? ToUtf8(Absolute.stem()) : Descriptor.Name;
    return true;
}
fs::path FPaths::EngineFile(const fs::path& Relative)
{
    return Relative.is_absolute() ? Relative : (GEngineDir / Relative).lexically_normal();
}
FString FPaths::EngineFileUtf8(const fs::path& Relative) { return ToUtf8(EngineFile(Relative)); }
fs::path FPaths::EngineResourcesDir() { return GEngineDir / L"EngineResources"; }
fs::path FPaths::ProjectContentDir()
{
    const fs::path Root = ProjectRoot();
    std::error_code Ec;
    // Older external projects can still be opened without moving their files automatically.
    if (!fs::exists(Root / L"Content", Ec) && fs::is_directory(Root / L"Assets", Ec)) return Root / L"Assets";
    return Root / L"Content";
}
fs::path FPaths::ProjectScenesDir()
{
    return ProjectContentDir().filename() == L"Assets" ? ProjectRoot() / L"Scenes" : ProjectContentDir() / L"Scenes";
}
fs::path FPaths::ProjectScriptsDir()
{
    return ProjectContentDir().filename() == L"Assets" ? ProjectRoot() / L"Scripts" : ProjectContentDir() / L"Scripts";
}
fs::path FPaths::EngineBuildDir() { return GEngineDir / L"Build"; }
fs::path FPaths::ProjectBuildDir() { return ProjectRoot() / L"Build"; }
fs::path FPaths::EngineSavedDir() { return EngineBuildDir() / L"Saved"; }
fs::path FPaths::ProjectSavedDir() { return ProjectBuildDir() / L"Saved"; }
fs::path FPaths::EditorSettingsFile() { return ProjectRoot() / L"Config/editor.ini"; }

bool FPaths::IsEngineContentPath(const FString& Path)
{
    FString Normal = Path; std::replace(Normal.begin(), Normal.end(), '\\', '/');
    return StartsWithFolder(Normal, "Engine");
}
FString FPaths::ResolveContentPath(const FString& Path)
{
    FString Normal = Path; std::replace(Normal.begin(), Normal.end(), '\\', '/');
    if (FromUtf8(Normal).is_absolute()) return ToUtf8(FromUtf8(Normal).lexically_normal());
    if (StartsWithFolder(Normal, "Engine")) return ToUtf8(EngineResourcesDir() / FromUtf8(Normal.substr(7)));
    if (StartsWithFolder(Normal, "EngineResources")) return EngineFileUtf8(FromUtf8(Normal));
    // Previously saved engine file paths still resolve after the folder rename.
    if (StartsWithFolder(Normal, "Content/Engine")) return ToUtf8(EngineResourcesDir() / FromUtf8(Normal.substr(15)));
    // Content/Game was merged into Content; scenes saved with the old prefix still resolve.
    if (StartsWithFolder(Normal, "Content/Game")) return ToUtf8(ProjectContentDir() / FromUtf8(Normal.substr(13)));
    if (StartsWithFolder(Normal, "Content")) return ToUtf8(ProjectContentDir() / FromUtf8(Normal.substr(8)));
    if (StartsWithFolder(Normal, "Assets")) return ToUtf8(ProjectContentDir() / FromUtf8(Normal.substr(7)));
    if (StartsWithFolder(Normal, "Scenes")) return ToUtf8(ProjectScenesDir() / FromUtf8(Normal.substr(7)));
    if (StartsWithFolder(Normal, "Scripts")) return ToUtf8(ProjectScriptsDir() / FromUtf8(Normal.substr(8)));
    return Normal;
}
fs::path FPaths::ResolveEngineRelative(const FString& Path)
{
    if (StartsWithFolder(Path, "EngineResources") || StartsWithFolder(Path, "Build/Cache/Shaders")) return EngineFile(FromUtf8(Path));
    return FromUtf8(ResolveContentPath(Path));
}
fs::path FPaths::MeshCacheFile(const FString& SourcePath)
{
    const fs::path Source = fs::absolute(FromUtf8(ResolveContentPath(SourcePath))).lexically_normal();
    fs::path CacheRoot = ProjectBuildDir() / L"Cache/Meshes";
    fs::path Relative = Source.lexically_relative(ProjectContentDir());
    fs::path Result;
    if (IsInside(Relative)) Result = CacheRoot / L"Content" / Relative;
    else
    {
        Relative = Source.lexically_relative(EngineResourcesDir());
        if (IsInside(Relative)) Result = EngineBuildDir() / L"Cache/Meshes/Engine" / Relative;
        else
        {
            uint64 Hash = 14695981039346656037ull;
            for (unsigned char C : ToUtf8(Source)) { Hash ^= C; Hash *= 1099511628211ull; }
            Result = CacheRoot / L"External" / std::to_string(Hash) / Source.filename();
        }
    }
    Result += L".bin";
    std::error_code Ec; fs::create_directories(Result.parent_path(), Ec);
    return Result;
}

FString FPaths::ToUtf8(const fs::path& Path)
{
	const std::u8string Utf8 = Path.generic_u8string();
	return FString(Utf8.begin(), Utf8.end());
}

fs::path FPaths::FromUtf8(const FString& Utf8)
{
	return fs::path(std::u8string(Utf8.begin(), Utf8.end()));
}

fs::path FPaths::FindProjectFileIn(const fs::path& Dir)
{
	std::error_code Ec;
	for (const fs::directory_entry& Entry : fs::directory_iterator(Dir, Ec))
	{
		if (Entry.is_regular_file(Ec) && _wcsicmp(Entry.path().extension().c_str(), ProjectExtension) == 0)
			return Entry.path();
	}
	return {};
}

bool FProjectDescriptor::Load(const fs::path& File, FString* OutError)
{
	std::ifstream In(File);
	if (!In)
	{
		if (OutError) *OutError = "Cannot open project file: " + FPaths::ToUtf8(File);
		return false;
	}

	const json Root = json::parse(In, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		if (OutError) *OutError = "Project file is not valid JSON: " + FPaths::ToUtf8(File);
		return false;
	}

	FileVersion = Root.value("FileVersion", CurrentFileVersion);
	Name = Root.value("Name", FString());
	Description = Root.value("Description", FString());
	return true;
}

bool FProjectDescriptor::Save(const fs::path& File) const
{
	json Root;
	Root["FileVersion"] = FileVersion;
	Root["Name"] = Name;
	Root["Description"] = Description;

	std::ofstream Out(File);
	if (!Out) return false;
	Out << Root.dump(4) << '\n';
	return static_cast<bool>(Out);
}
