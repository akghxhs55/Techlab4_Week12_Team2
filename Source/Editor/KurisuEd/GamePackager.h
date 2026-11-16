#pragma once

#include "Core/EngineString.h"

// 에디터에서 Package.ps1을 자식 프로세스로 돌리고 출력을 Output Log로 옮긴다.
// UE 에디터가 Package Project에서 UAT를 별도 프로세스로 띄우는 것과 같은 구조다.
class FGamePackager
{
public:
	~FGamePackager();

	bool Start(const FString& Config);
	// 매 프레임 부른다. 파이프에 이미 쌓인 출력만 읽고 바로 돌아오므로 에디터가 멈추지 않는다.
	void Tick();
	bool IsRunning() const { return Process != nullptr; }
private:
	void DrainOutput();
	void Finish();

	HANDLE Process = nullptr;
	HANDLE OutputRead = nullptr;
	FString Pending;   // 아직 줄바꿈이 오지 않은 출력
};
