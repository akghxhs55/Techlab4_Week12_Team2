#include "EnginePCH.h"
#include "Editor/KurisuEd/GamePackager.h"

#include <algorithm>

#include "Core/Misc/Paths.h"

FGamePackager::~FGamePackager()
{
	if (Process) CloseHandle(Process);
	if (OutputRead) CloseHandle(OutputRead);
}

bool FGamePackager::Start(const FString& Config)
{
	if (IsRunning()) return false;
	// 엔진(Setup/Packaging/Package.ps1, EngineResources)과 프로젝트(Content)는 서로 다른 폴더일 수 있다.
	// 소스 체크아웃과 배포 에디터 모두 같은 Package.ps1로 패키징한다. 빌드 여부는 스크립트가 정한다.
	const std::filesystem::path Root = FPaths::EngineDir();
	const std::filesystem::path ScriptPath = Root / L"Setup/Packaging/Package.ps1";
	std::error_code Ec;
	if (!std::filesystem::exists(ScriptPath, Ec))
	{
		KRS_LOG(Error, "Packaging canceled: missing Setup/Packaging/Package.ps1. Replace the editor with a complete Release distribution.");
		return false;
	}

	//Win32 API에서 커널 객체(파일, 프로세스, 스레드, 이벤트, 뮤텍스, 파이프 등)를 만들 때 넘기는 구조체로, 두 가지를 정합니다.

	//이 객체에 누가 접근할 수 있는지(보안 디스크립터)
	//이 객체의 핸들을 자식 프로세스가 물려받을 수 있는지(상속 여부)
	SECURITY_ATTRIBUTES Security{ sizeof(Security), nullptr, TRUE };
	HANDLE OutputWrite = nullptr;
	if (!CreatePipe(&OutputRead, &OutputWrite, &Security, 0)) return false;
	// 읽는 쪽 핸들은 자식에게 물려주지 않는다.
	SetHandleInformation(OutputRead, HANDLE_FLAG_INHERIT, 0);

	// 엔진 폴더의 Package.ps1을 절대 경로로 부르고 프로젝트 파일을 넘긴다.
	const std::wstring ConfigW(Config.begin(), Config.end());
	std::wstring CommandLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + ScriptPath.wstring()
		+ L"\" -Configuration " + ConfigW + L" -Project \"" + FPaths::ProjectFilePath().wstring() + L"\"";
	STARTUPINFOW StartupW{};
	StartupW.cb = sizeof(StartupW);
	StartupW.dwFlags = STARTF_USESTDHANDLES;
	StartupW.hStdOutput = OutputWrite;
	StartupW.hStdError = OutputWrite;
	PROCESS_INFORMATION Info{};
	const BOOL bCreated = CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, Root.c_str(), &StartupW, &Info);
	// 쓰기 핸들은 자식만 갖고 있어야, 자식이 끝날 때 파이프가 닫힌다.
	CloseHandle(OutputWrite);
	if (!bCreated)
	{
		KRS_LOG(Error, "Failed to start Package.ps1 (error {})", GetLastError());
		CloseHandle(OutputRead);
		OutputRead = nullptr;
		return false;
	}
	CloseHandle(Info.hThread);
	Process = Info.hProcess;
	KRS_LOG(Info, "Packaging started ({})", Config);
	return true;
}

void FGamePackager::Tick()
{
	if (!Process) return;
	// 종료 여부를 먼저 본다. 끝났다면 출력은 이미 전부 파이프에 들어와 있으므로 아래에서 다 읽힌다.
	// DrainOutput이 먼저인 경우 생길 수 있는 문제
	// DrainOutput()으로 읽음 (이때까지의 출력)
	// 그 사이 자식이 마지막 줄을 쓰고 종료
	// 종료 확인 -> 끝났네 -> Finish()에서 파이프 닫음
	// 마지막 줄은 읽지 못하고 사라짐
	const bool bExited = WaitForSingleObject(Process, 0) == WAIT_OBJECT_0;
	DrainOutput();
	if (bExited) Finish();
}


void FGamePackager::DrainOutput()
{
	DWORD Available = 0;
	// ReadFile은 읽을 데이터가 없으면 
	// 메인 스레드에서 그냥 부르면 에디터가 멈추기 때문에 PeekNamedPipe로 지금 몇 바이트가 쌓여 있는지 먼저 보고, 그만큼만 읽음. 
	// 이름에 Named가 들어가지만 익명 파이프에도 사용 가능
	while (PeekNamedPipe(OutputRead, nullptr, 0, nullptr, &Available, nullptr) && Available > 0)
	{
		char Buffer[1024];
		DWORD Read = 0;
		if (!ReadFile(OutputRead, Buffer, std::min<DWORD>(Available, sizeof(Buffer)), &Read, nullptr) || Read == 0)
			break;
		Pending.append(Buffer, Read);
	}

	// 줄 단위로 Output Log에 넘긴다.
	size_t NewLine;
	while ((NewLine = Pending.find('\n')) != FString::npos)
	{
		FString Line = Pending.substr(0, NewLine);
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();
		if (!Line.empty()) KRS_LOG(Info, "[Package] {}", Line);
		Pending.erase(0, NewLine + 1);
	}
}

void FGamePackager::Finish()
{
	if (!Pending.empty()) KRS_LOG(Info, "[Package] {}", Pending);
	Pending.clear();

	DWORD ExitCode = 1;
	GetExitCodeProcess(Process, &ExitCode);
	CloseHandle(Process);
	CloseHandle(OutputRead);
	Process = nullptr;
	OutputRead = nullptr;

	if (ExitCode == 0) KRS_LOG(Info, "Packaging finished: {}", FPaths::ToUtf8(FPaths::ProjectDir() / "Build" / "Package" / FPaths::FromUtf8(FPaths::ProjectName())));
	else KRS_LOG(Error, "Packaging failed (exit code {}). See the log above.", ExitCode);
}
