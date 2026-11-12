#pragma once

#include <filesystem>

#include "Core/EngineString.h"
#include "Core/Types.h"

// UE의 FPaths처럼 엔진 폴더와 프로젝트 폴더를 나눠서 다룬다.
// 엔진 자원은 EngineResources, 프로젝트 콘텐츠는 Content, 생성 데이터는 Build 아래에 둔다.
// 프로젝트 폴더는 .hproject 파일이 있는 곳이며 실행 중 작업 디렉터리로 사용한다.
// 프로젝트를 정하면 작업 디렉터리를 프로젝트 폴더로 바꾸므로 "Content/..." 같은 기존 상대 경로는 그대로 쓴다.
// 엔진 파일은 EngineFile()로 절대 경로를 만들어 연다.
class FPaths
{
public:
	static constexpr const wchar_t* ProjectExtension = L".hproject";

	// exe 위치에서 위로 올라가며 EngineResources/Shaders가 있는 폴더를 찾아 엔진 폴더로 정한다.
	static bool InitEngineDir();

	static const std::filesystem::path& EngineDir();
	static const std::filesystem::path& ProjectDir();
	static const std::filesystem::path& ProjectFilePath();
	static const FString& ProjectName();
	static bool HasProject();

	// .hproject 파일을 읽어 현재 프로젝트로 정하고 작업 디렉터리를 프로젝트 폴더로 바꾼다.
	static bool SetProject(const std::filesystem::path& ProjectFile, FString* OutError = nullptr);

	// 엔진 폴더 기준 경로. 절대 경로는 그대로 돌려준다.
	static std::filesystem::path EngineFile(const std::filesystem::path& Relative);
	// 엔진 콘텐츠와 엔진 셰이더 캐시 경로를 작업 디렉터리와 무관하게 해석한다.
	// 셰이더 키처럼 엔진 상대 경로가 데이터로 돌아다니는 곳에서 파일을 열 때 쓴다.
	static std::filesystem::path ResolveEngineRelative(const FString& Path);
	// 엔진 폴더 기준 경로의 UTF-8 문자열. FString을 받는 로더에 넘길 때 쓴다.
	static FString EngineFileUtf8(const std::filesystem::path& Relative);

	// 엔진 콘텐츠: 엔진 코드가 직접 쓰는 아이콘, 폰트 같은 에셋. 실제 위치는 <엔진>/EngineResources (UE의 Engine/Content).
	// UE의 /Engine/처럼 에셋 경로는 "Engine/Icons/Point.png" 형식으로 쓴다. 프로젝트마다 복사할 필요가 없다.
	static constexpr const char* EngineContentPrefix = "Engine/";
	static std::filesystem::path EngineResourcesDir();
	static bool IsEngineContentPath(const FString& Path);
	// Content 및 Engine/... 경로를 해석한다. 옛 Content/Game, Assets/Scenes/Scripts 접두 경로도 읽는다.
	static FString ResolveContentPath(const FString& Path);

	// 엔진 쪽 사용자 데이터(최근 프로젝트 목록 등)를 두는 폴더.
	static std::filesystem::path EngineSavedDir();
	static std::filesystem::path ProjectContentDir();
	static std::filesystem::path ProjectScenesDir();
	static std::filesystem::path ProjectScriptsDir();
	static std::filesystem::path EngineBuildDir();
	static std::filesystem::path ProjectBuildDir();
	static std::filesystem::path ProjectSavedDir();
	static std::filesystem::path EditorSettingsFile();
	// 원본 상대 디렉터리를 유지해 이름이 같은 메시의 캐시가 충돌하지 않게 한다.
	static std::filesystem::path MeshCacheFile(const FString& SourcePath);

	static FString ToUtf8(const std::filesystem::path& Path);
	static std::filesystem::path FromUtf8(const FString& Utf8);

	// Dir 바로 아래에 있는 첫 .hproject 파일. 없으면 빈 경로.
	static std::filesystem::path FindProjectFileIn(const std::filesystem::path& Dir);
};

// .hproject 파일 내용. JSON 형식이다.
struct FProjectDescriptor
{
	static constexpr int32 CurrentFileVersion = 1;

	int32 FileVersion = CurrentFileVersion;
	FString Name;
	FString Description;

	bool Load(const std::filesystem::path& File, FString* OutError = nullptr);
	bool Save(const std::filesystem::path& File) const;
};
