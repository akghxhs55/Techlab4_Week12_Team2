#include "EnginePCH.h"

#include "Core/EngineStatics.h"
#include "Launch/Windows/WindowsCrashHandler.h"

// Launch.cpp에 있다.
extern int32 GuardedMain(HINSTANCE hInstance);

// Windows 전용 진입점. 플랫폼 일만 하고 나머지는 GuardedMain에 맡긴다. UE의 LaunchWindows.cpp와 같은 자리다.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// 엔진 초기화 전에 등록해 시작 중 발생하는 예외도 처리할 수 있게 한다.
	const auto PreviousExceptionHandler = RegisterWindowsCrashHandler();
	const int32 ErrorLevel = GuardedMain(hInstance);

	// GuardedMain이 끝나면 엔진 루프와 모든 UObject가 정리된 상태여야 한다.
	if (FEngineStatics::TotalAllocationCount > 0)
		OutputDebugStringA("leaked UObjects\n");

	// EXCEPTION_CONTINUE_SEARCH를 반환하므로, Windows의 기본 예외 처리가 이어진다.
	RestoreWindowsCrashHandler(PreviousExceptionHandler);

	return ErrorLevel;
}
