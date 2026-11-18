#pragma once

#include <Windows.h>
#include <filesystem>

// 엔진 시작 전에 크래시 필터를 등록하고, 정상 종료 때 복구할 이전 필터를 반환한다.
LPTOP_LEVEL_EXCEPTION_FILTER RegisterWindowsCrashHandler();
void RestoreWindowsCrashHandler(LPTOP_LEVEL_EXCEPTION_FILTER PreviousFilter);

// 정상 시작 중에 Crashes 폴더를 만들고, 준비가 끝난 경로를 예외 콜백에 공개한다.
void PrepareCrashDumpDirectory(const std::filesystem::path& SavedDirectory, bool bIsProject);
