#include "EnginePCH.h"
#include "Launch/EntryPoint.h"
#include "Core/Misc/Paths.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"

UClass* GetEngineClass() { return UEditorEngine::StaticClass(); }

std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine)
{
    const auto ProjectFile = FromCommandLine.empty() ? ProjectBrowser::Run(hInstance) : FromCommandLine;
    if (!ProjectFile.empty()) RecentProjects::Add(ProjectFile);
    return ProjectFile;
}
