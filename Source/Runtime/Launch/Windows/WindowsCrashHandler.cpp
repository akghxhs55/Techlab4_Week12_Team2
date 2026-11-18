#include "EnginePCH.h"
#include "Launch/Windows/WindowsCrashHandler.h"

#include <atomic>
#include <DbgHelp.h>
#include <strsafe.h>
#include <utility>

#pragma comment(lib, "Dbghelp.lib")

static std::filesystem::path EngineCrashDirectory;
static std::filesystem::path ProjectCrashDirectory;
static std::atomic<const wchar_t*> ActiveCrashDirectory{ nullptr };
static std::atomic_flag CrashDumpStarted = ATOMIC_FLAG_INIT;
static wchar_t CrashExeName[MAX_PATH] = L"Kurisu";

void PrepareCrashDumpDirectory(const std::filesystem::path& SavedDirectory, bool bIsProject)
{
    auto CrashDirectory = SavedDirectory / L"Crashes";
    std::error_code Error;
    std::filesystem::create_directories(CrashDirectory, Error);
    if (Error)
    {
        // 프로젝트 폴더 준비에 실패하면 이미 준비한 엔진 경로를 유지한다.
        return;
    }

    // 두 경로를 따로 보관하고, 초기화가 끝난 경로만 예외 콜백에 공개한다.
    auto& PreparedDirectory = bIsProject ? ProjectCrashDirectory : EngineCrashDirectory;
    PreparedDirectory = std::move(CrashDirectory);
    ActiveCrashDirectory.store(PreparedDirectory.c_str(), std::memory_order_release);
}

static void PrepareCrashExecutableName()
{
    // 정상 시작 중에 현재 실행 파일의 이름을 구한다. 조회가 실패하면 기본 이름을 사용한다.
    wchar_t ModulePath[MAX_PATH]{};
    const DWORD Length = GetModuleFileNameW(nullptr, ModulePath, MAX_PATH);
    if (Length > 0 && Length < MAX_PATH)
    {
        const auto Name = std::filesystem::path(ModulePath).stem();
        StringCchCopyW(CrashExeName, MAX_PATH, Name.c_str());
    }
}

static bool BuildCrashDumpPath(wchar_t* DumpPath, size_t Capacity, const wchar_t* Directory, DWORD ThreadId)
{
    // 실행 파일명, UTC 시각, 프로세스 ID, 예외가 발생한 스레드 ID를 파일명에 남긴다.
    SYSTEMTIME Utc{};
    GetSystemTime(&Utc);

    const unsigned Date = Utc.wYear * 10000u + Utc.wMonth * 100u + Utc.wDay;
    const unsigned Time = Utc.wHour * 10000u + Utc.wMinute * 100u + Utc.wSecond;

    const DWORD ProcessId = GetCurrentProcessId();
    const wchar_t* Format = L"%ls\\%ls_%08uT%06uZ_%lu_%lu.dmp";

    // Capacity는 문자 개수다. 경로가 잘리면 해당 경로로 파일을 만들지 않는다.
    return SUCCEEDED(StringCchPrintfW(DumpPath, Capacity, Format, Directory, CrashExeName, Date, Time, ProcessId, ThreadId));
}

// 예외 시점의 CPU 상태와 스레드 정보를 현재 프로세스의 미니덤프에 저장한다.
static bool WriteCrashDump(const wchar_t* DumpPath, EXCEPTION_POINTERS* ExceptionPointers)
{
    // 같은 이름의 덤프가 이미 있으면 덮어쓰지 않고 실패한다.
    const HANDLE DumpFile = CreateFileW(DumpPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (DumpFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }


    MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo{};
    ExceptionInfo.ThreadId = GetCurrentThreadId();
    ExceptionInfo.ExceptionPointers = ExceptionPointers;
    // 예외 정보가 덤프 작성기를 호출하는 프로세스의 주소 공간에 있다.
    ExceptionInfo.ClientPointers = FALSE;

    const BOOL DumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), DumpFile, MiniDumpNormal, &ExceptionInfo, nullptr, nullptr);

    CloseHandle(DumpFile);
    if (DumpWritten == FALSE)
    {
        // 쓰기에 실패한 파일이 분석용 덤프로 남지 않도록 삭제한다.
        DeleteFileW(DumpPath);
    }

    return DumpWritten != FALSE;
}

// 처리되지 않은 Windows 예외가 최상위 필터까지 도달하면 Windows가 예외 스레드에서 호출한다.
// ExceptionPointers로 예외 코드, 발생 주소, 당시 CPU 상태를 전달받는다.
static LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* ExceptionPointers)
{
    const wchar_t* Directory = ActiveCrashDirectory.load(std::memory_order_acquire);

    // 경로와 예외 정보가 준비됐을 때, 첫 번째 스레드만 덤프 쓰기를 시도한다.
    if (Directory && ExceptionPointers && !CrashDumpStarted.test_and_set())
    {
        wchar_t DumpPath[MAX_PATH]{};
        const DWORD ThreadId = GetCurrentThreadId();
        if (!BuildCrashDumpPath(DumpPath, MAX_PATH, Directory, ThreadId) || !WriteCrashDump(DumpPath, ExceptionPointers))
        {
            OutputDebugStringA("Failed to save crash dump.\n");
        }
    }

    // 덤프 시도가 끝나면 Windows의 기본 예외 처리를 이어간다.
    return EXCEPTION_CONTINUE_SEARCH;
}

LPTOP_LEVEL_EXCEPTION_FILTER RegisterWindowsCrashHandler()
{
    PrepareCrashExecutableName();

    // 프로세스 전체에 필터를 등록하고 정상 종료 때 복구할 이전 필터를 반환한다.
    return SetUnhandledExceptionFilter(HandleUnhandledException);
}

void RestoreWindowsCrashHandler(LPTOP_LEVEL_EXCEPTION_FILTER PreviousFilter)
{
    SetUnhandledExceptionFilter(PreviousFilter);
}
