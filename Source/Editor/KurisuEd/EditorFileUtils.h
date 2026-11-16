#pragma once

#include "Engine/World.h"

enum class EFbxImportMode : uint32;

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);
	// 대화상자 없이 주어진 경로의 씬을 연다 (콘텐츠 드로어 더블클릭용).
	static bool LoadScene(UWorld* World, const FString& InFilePath);
	static void ImportFbx(EFbxImportMode Mode);

	static FString CurrentScenePath;
private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

};

