#pragma once

#include <sol/forward.hpp>
#include "Core/Misc/Paths.h"

// sol::state 하나를 엔진 전체가 공유한다. 액터별 격리는 FLuaScriptInstance의 environment로 한다.
// 담당: A, 소비자: B (FLuaBindings::Register에 GetState를 넘긴다)
class FLuaScriptManager
{
public:
	static FLuaScriptManager& Get();

	// open_libraries, print → OutputLog 연결, FLuaBindings::Register 호출
	void Initialize();
	void Shutdown();

	sol::state& GetState();

	// 씬·액터에 저장하는 경로 형식. 프로젝트 콘텐츠 안이면 "Content/Scripts/Foo.lua",
	// 엔진 리소스면 "Engine/...", 그 밖이면 절대 경로를 돌려준다. 다른 PC와 패키지에서도 같은 문자열로 찾는다.
	static FString MakeScriptKey(const fs::path& File);
	// 저장돼 있던 경로(옛 절대 경로 포함)를 위 형식으로 바꾼다.
	// 다른 PC의 절대 경로라 파일이 없으면 프로젝트 Scripts 폴더에서 같은 이름을 찾는다.
	static FString NormalizeScriptPath(const FString& Path);
	static FString GetTemplatePath();

	// template.lua를 Path로 복제한다. 이미 있으면 덮어쓰지 않고 true
	bool CreateScriptFromTemplate(const FString& Path);
	// 기본 연결 프로그램으로 연다. ShellExecute 반환값 ≤ 32이면 false
	bool OpenScriptInEditor(const FString& Path);

	static fs::path ToPath(const FString& Utf8)
	{
		return FPaths::FromUtf8(FPaths::ResolveContentPath(Utf8));
	}

	static FString FromPath(const fs::path& Path)
	{
		const std::u8string U8 = Path.u8string();
		return FString(U8.begin(), U8.end());
	}

	bool bEnableHotReload = true;

private:
	FLuaScriptManager() = default;
	~FLuaScriptManager();

	sol::state* State = nullptr;
};
