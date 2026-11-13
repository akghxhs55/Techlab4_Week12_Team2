#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineLog.h"

#include <functional>


struct FEditorSettings;

struct FLogData
{
	FString message;
	ImVec4 Color;
	ELogVerbosity Verbosity = ELogVerbosity::Info;
};

using FConsoleHandler = std::function<bool(const TArray<FString>& Args, const FString& Line)>;

struct FConsoleCommand
{
	FString Name;
	FString Usage;
	FConsoleHandler Handler;
};

class FOutputLogPanel : public IEditorPanel, public ILogSink
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Output Log"; }

	void ClearLog();
	// ImGui 컨텍스트가 만들어진 뒤 엔진이 부른다.
	void LoadLogFont();

	//template<typename... Args>
	//void AddLog(ELogVerbosity Verbosity, std::format_string<Args...> fmt, Args&&... args)
	//{

	//}

	void ExecCommand(const FString& command_line);

	int TextEditCallback(ImGuiInputTextCallbackData* data);
	void SetEditorSettings(FEditorSettings* InSettings) { EditorSettings = InSettings; }
private:
	void RegisterCommands();
	void AddToHistory(const FString& CommandLine);

	char                  InputBuf[256];
	TArray<FLogData>       Items;
	TArray<FConsoleCommand> ConsoleCommands;
	TArray<FString>		  Commands;
	TArray<FString>       History;
	int                   HistoryPos;
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;

	// UE Output Log의 Filters 메뉴: 종류별로 보이거나 숨긴다.
	bool bShowMessages = true;
	bool bShowWarnings = true;
	bool bShowErrors = true;
	bool bShowCommands = true;
	bool PassVerbosity(ELogVerbosity Verbosity) const;
	void SaveFilteredLog();

	// 로그 본문용 고정폭 글꼴 (Consolas + 한글은 나눔스퀘어). 없으면 기본 글꼴을 쓴다.
	ImFont* LogFont = nullptr;

	// Inherited via ILogSink
	void OnLog(ELogVerbosity, const FString& Message) override;

	FEditorSettings* EditorSettings = nullptr;
};