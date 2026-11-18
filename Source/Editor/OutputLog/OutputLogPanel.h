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

struct FConsoleSuggestion
{
	FString Badge;
	FString DisplayText;
	FString InsertText;
	FString Hint;
	bool bIsCVar = false;
};

class FOutputLogPanel : public IEditorPanel, public ILogSink
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	void RenderContents();
	const char* GetPanelName() const override { return "Output Log"; }

	void ClearLog();
	// ImGui 컨텍스트가 만들어진 뒤 엔진이 부른다.
	void LoadLogFont();

	//template<typename... Args>
	//void AddLog(ELogVerbosity Verbosity, std::format_string<Args...> fmt, Args&&... args)
	//{

	//}

	void ExecCommand(const FString& command_line);
	void RenderCommandInput(const char* InputId, char* Buffer, size_t BufferSize, float Width, bool bUseLogFont = true, bool bReclaimFocusOnSubmit = true);

	int TextEditCallback(ImGuiInputTextCallbackData* data);
	void SetEditorSettings(FEditorSettings* InSettings) { EditorSettings = InSettings; }
private:
	void RegisterCommands();
	void RegisterSuggestion(const char* Badge, const char* DisplayText, const char* InsertText, const char* Hint = "", bool bIsCVar = false);
	void UpdateSuggestions(const char* Query);
	void RenderAutocompletePopup(const ImVec2& InputMin, const ImVec2& InputMax);
	void AddToHistory(const FString& CommandLine);

	char                  InputBuf[256];
	TArray<FLogData>       Items;
	TArray<FConsoleCommand> ConsoleCommands;
	TArray<FString>		  Commands;
	TArray<FConsoleSuggestion> AllSuggestions;
	TArray<int32>         FilteredSuggestionIndices;
	TArray<FString>       History;
	int                   HistoryPos;
	int32                 SelectedSuggestion = 0;
	bool                  bSuppressSuggestions = false;
	bool                  bScrollSuggestionToSelection = false;
	bool                  bSuggestionPopupHovered = false;
	bool                  bApplySuggestionCompletion = false;
	bool                  bReclaimInputFocusNextFrame = false;
	bool                  bMoveCursorToEndOnFocus = false;
	FString               PendingSuggestionText;
	FString               LastSuggestionQuery;
	FString               ActiveSuggestionInputId;
	FString               CurrentInputRenderId;
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