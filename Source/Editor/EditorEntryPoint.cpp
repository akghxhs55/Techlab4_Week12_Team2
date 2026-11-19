#include "EnginePCH.h"
#include "Launch/EntryPoint.h"
#include "Core/Misc/Paths.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"
#include "Core/Windows/ResourceIds.h"

#include <ShlObj.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
    void RegisterProjectFileIcon()
    {
        // 파일 자체에는 탐색기 아이콘을 저장할 수 없으므로 현재 사용자의 확장자에 등록한다.
        // EXE에 포함한 리소스를 사용해 소스 없는 에디터 배포본에서도 같은 아이콘을 쓴다.
        std::wstring ExecutablePath(32768, L'\0');
        const DWORD Length = GetModuleFileNameW(nullptr, ExecutablePath.data(),
            static_cast<DWORD>(ExecutablePath.size()));
        if (Length == 0 || Length >= ExecutablePath.size()) return;
        ExecutablePath.resize(Length);

        const std::wstring IconLocation = L"\"" + ExecutablePath + L"\",-" + std::to_wstring(IDI_PROJECT_ICON);
        const std::wstring KeyPath = std::wstring(L"Software\\Classes\\") + FPaths::ProjectExtension + L"\\DefaultIcon";
        std::wstring CurrentValue(IconLocation.size() + 1, L'\0');
        DWORD ValueBytes = static_cast<DWORD>(CurrentValue.size() * sizeof(wchar_t));
        if (RegGetValueW(HKEY_CURRENT_USER, KeyPath.c_str(), nullptr, RRF_RT_REG_SZ,
            nullptr, CurrentValue.data(), &ValueBytes) == ERROR_SUCCESS &&
            IconLocation == CurrentValue.c_str()) return;

        HKEY Key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, KeyPath.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &Key, nullptr) != ERROR_SUCCESS)
        {
            OutputDebugStringW(L"Kurisu: Cannot register the .kproject icon.\n");
            return;
        }
        // 아이콘만 지정하고 기존 연결 프로그램 및 더블클릭 동작은 유지한다.
        const LSTATUS Result = RegSetValueExW(Key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(IconLocation.c_str()),
            static_cast<DWORD>((IconLocation.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(Key);
        if (Result == ERROR_SUCCESS)
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        else
            OutputDebugStringW(L"Kurisu: Cannot update the .kproject icon.\n");
    }
}

UClass* GetEngineClass() { return UEditorEngine::StaticClass(); }

std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine)
{
    RegisterProjectFileIcon();
    const auto ProjectFile = FromCommandLine.empty() ? ProjectBrowser::Run(hInstance) : FromCommandLine;
    if (!ProjectFile.empty()) RecentProjects::Add(ProjectFile);
    return ProjectFile;
}
