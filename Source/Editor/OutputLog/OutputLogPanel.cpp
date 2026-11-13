#include "EnginePCH.h"
#include "Editor/OutputLog/OutputLogPanel.h"

#include "Core/StatOverlay.h"
#include "Render/SceneRenderer.h"

#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

#include "Components/SkinnedMeshComponent.h"

#include <charconv>
#include <sstream>
#include "Core/Misc/Paths.h"
#include <fstream>
#include <chrono>

namespace
{
	TArray<FString> TokenizeCommand(const FString& Line)
	{
		TArray<FString> Tokens;
		std::istringstream Stream(Line);
		for (FString Token; Stream >> Token;)
			Tokens.Add(Token);
		return Tokens;
	}

	template <typename T>
	bool ParseNumber(const FString& Text, T& Out)
	{
		const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Out);

		return Error == std::errc{} && End == Text.data() + Text.size();
	}

	bool ParseToggle(const TArray<FString>& Args, bool bCurrent, bool& bOut)
	{
		if (Args.Num() == 1) { bOut = !bCurrent; return true; }
		if (Args.Num() == 2 && (Args[1] == "0" || Args[1] == "1")) { bOut = (Args[1] == "1"); return true; }
		return false;
	}
}

static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	FOutputLogPanel* OutputLog = (FOutputLogPanel*)data->UserData;
	return OutputLog->TextEditCallback(data);
}

static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }

// 콘솔 명령은 입력 대소문자를 가리지 않는다. "stat fps"와 "STAT FPS"가 같아야 한다.
static bool EqualsIgnoreCase(const FString& A, const char* B)
{
	return Strnicmp(A.c_str(), B, static_cast<int>(A.size()) + 1) == 0;
}

bool FOutputLogPanel::Init()
{
	ClearLog();
	RegisterCommands();   
	

	HistoryPos = -1;
	InputBuf[0] = '\0';

	AutoScroll = true;
	ScrollToBottom = false;

	return true;
}

void FOutputLogPanel::LoadLogFont()
{
	// 이 패널은 시작 로그를 받으려고 ImGui 컨텍스트보다 먼저 만들어진다. 글꼴은 컨텍스트가 생긴 뒤에 읽는다.
	if (LogFont || !ImGui::GetCurrentContext())
		return;

	// UE처럼 로그는 고정폭 글꼴로 보여 준다. 한글 로그가 깨지지 않도록 나눔스퀘어를 합친다.
	ImGuiIO& IO = ImGui::GetIO();
	wchar_t WindowsDir[MAX_PATH]{};
	if (::GetWindowsDirectoryW(WindowsDir, MAX_PATH) > 0)
	{
		const std::filesystem::path MonoPath = std::filesystem::path(WindowsDir) / L"Fonts" / L"consola.ttf";
		std::error_code Error;
		if (std::filesystem::exists(MonoPath, Error))
		{
			LogFont = IO.Fonts->AddFontFromFileTTF(FPaths::ToUtf8(MonoPath).c_str(), 14.0f);
			if (LogFont)
			{
				ImFontConfig Merge;
				Merge.MergeMode = true;
				IO.Fonts->AddFontFromFileTTF(FPaths::EngineFileUtf8("EngineResources/Fonts/NanumSquareR.ttf").c_str(), 14.0f,
					&Merge, IO.Fonts->GetGlyphRangesKorean());
			}
		}
	}
}

void FOutputLogPanel::Tick(float DeltaTime)
{
}

void FOutputLogPanel::OnRender()
{
	using namespace ViewportToolbarWidgets;
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

	ImGui::Begin("Output Log", GetOpenFlag());
	const ImGuiStyle& Style = ImGui::GetStyle();
	bool CopyToClipboard = false;

	//////////////////////////////////////////////////////////
	// 상단 바 (UE): [돋보기 Search Log][Filters v] ............ [Settings v]

	const float FiltersWidth = IconButtonWidth("Filters", true);
	const float SettingsWidth = IconButtonWidth("Settings", true);
	// 검색창은 적당한 길이로 두고, 창이 좁을 때만 남은 폭에 맞춰 줄인다.
	const float SearchRoom = ImGui::GetContentRegionAvail().x - FiltersWidth - SettingsWidth - Style.ItemSpacing.x * 2.0f - EditorStyle::Scale(24.0f);
	const float SearchWidth = std::clamp(SearchRoom, EditorStyle::Scale(120.0f), EditorStyle::Scale(320.0f));

	// 왼쪽에 돋보기를 그릴 자리만큼 안쪽 여백을 준다.
	const ImVec2 SearchPos = ImGui::GetCursorScreenPos();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { Style.FramePadding.x + IconSize() + EditorStyle::Scale(4.0f), Style.FramePadding.y });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorStyle::Scale(4.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(15, 15, 15, 255));
	ImGui::SetNextItemWidth(SearchWidth);
	if (ImGui::InputTextWithHint("##SearchLog", "Search Log", Filter.InputBuf, IM_COUNTOF(Filter.InputBuf)))
		Filter.Build();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
		ImGui::SetTooltip("Filter syntax:\n"
			"error        Include matching lines\n"
			"-warning     Exclude matching lines\n"
			"error,-warn  Combine filters");
	if (const ImTextureID SearchIcon = GetIcon("Search"); SearchIcon != ImTextureID_Invalid)
	{
		const float CenterY = SearchPos.y + ImGui::GetFrameHeight() * 0.5f;
		const float X = SearchPos.x + Style.FramePadding.x;
		ImGui::GetWindowDrawList()->AddImage(SearchIcon, { X, CenterY - IconSize() * 0.5f }, { X + IconSize(), CenterY + IconSize() * 0.5f },
			{ 0, 0 }, { 1, 1 }, IM_COL32(150, 150, 150, 255));
	}

	// 드롭다운 버튼은 UE처럼 배경 없이 그린다.
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
	ImGui::SameLine();
	if (IconButton("##LogFilters", "Filter", "Filters", true, FiltersWidth))
		ImGui::OpenPopup("LogFiltersPopup");
	const EditorStyle::FPopupAnchor FiltersAnchor = EditorStyle::LastItemAnchor();

	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - SettingsWidth));
	if (IconButton("##LogSettings", "Settings", "Settings", true, SettingsWidth))
		ImGui::OpenPopup("LogSettingsPopup");
	const EditorStyle::FPopupAnchor SettingsAnchor = EditorStyle::LastItemAnchor();
	ImGui::PopStyleColor();

	if (EditorStyle::BeginButtonPopup("LogFiltersPopup", FiltersAnchor, EditorStyle::Scale(180.0f)))
	{
		SectionHeader("Verbosity");
		ImGui::Checkbox("Messages", &bShowMessages);
		ImGui::Checkbox("Warnings", &bShowWarnings);
		ImGui::Checkbox("Errors", &bShowErrors);
		ImGui::Checkbox("Commands", &bShowCommands);
		ImGui::Separator();
		if (ImGui::MenuItem("Show All"))
			bShowMessages = bShowWarnings = bShowErrors = bShowCommands = true;
		ImGui::EndPopup();
	}
	if (EditorStyle::BeginButtonPopup("LogSettingsPopup", SettingsAnchor, EditorStyle::Scale(200.0f)))
	{
		ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::Separator();
		if (ImGui::MenuItem("Copy")) CopyToClipboard = true;
		if (ImGui::MenuItem("Clear Log")) ClearLog();
		if (ImGui::MenuItem("Save to Logs")) SaveFilteredLog();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Save filtered output. Full session logs are also saved automatically to the project's Logs folder.");
		ImGui::EndPopup();
	}

	//////////////////////////////////////////////////////////
	// 로그 본문: 어두운 배경 + 고정폭 글꼴

	const float FooterHeight = Style.ItemSpacing.y + ImGui::GetFrameHeight();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(15, 15, 15, 255));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { EditorStyle::Scale(8.0f), EditorStyle::Scale(4.0f) });
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeight), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::Selectable("Copy")) CopyToClipboard = true;
			if (ImGui::Selectable("Clear Log"))
				ClearLog();
			ImGui::EndPopup();
		}

		if (LogFont) ImGui::PushFont(LogFont, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Style.ItemSpacing.x, 0.0f));
		if (CopyToClipboard)
			ImGui::LogToClipboard();
		for (const FLogData& item : Items)
		{
			if (!PassVerbosity(item.Verbosity) || !Filter.PassFilter(item.message.c_str()))
				continue;

			ImGui::PushStyleColor(ImGuiCol_Text, item.Color);
			ImGui::TextUnformatted(item.message.c_str());
			ImGui::PopStyleColor();
		}
		if (CopyToClipboard)
			ImGui::LogFinish();
		if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
			ImGui::SetScrollHereY(1.0f);
		ScrollToBottom = false;

		ImGui::PopStyleVar();
		if (LogFont) ImGui::PopFont();
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	//////////////////////////////////////////////////////////
	// 하단 (UE): [콘솔 Cmd v][Enter Console Command]

	const float CmdWidth = IconButtonWidth("Cmd", true);
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
	if (IconButton("##LogCmd", "Console", "Cmd", true, CmdWidth))
		ImGui::OpenPopup("LogCmdPopup");
	const EditorStyle::FPopupAnchor CmdAnchor = EditorStyle::LastItemAnchor();
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("LogCmdPopup"))
		ImGui::SetTooltip("Console command executor");
	if (EditorStyle::BeginButtonPopup("LogCmdPopup", CmdAnchor, EditorStyle::Scale(200.0f)))
	{
		ImGui::MenuItem("Cmd", nullptr, true);
		ImGui::Separator();
		if (ImGui::MenuItem("Show Commands (HELP)"))
			ExecCommand("HELP");
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	bool reclaim_focus = false;
	ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(15, 15, 15, 255));
	if (LogFont) ImGui::PushFont(LogFont, 0.0f);
	if (ImGui::InputTextWithHint("##OutputLogInput", "Enter Console Command", InputBuf, IM_COUNTOF(InputBuf), input_text_flags, &TextEditCallbackStub, this))
	{
		char* s = InputBuf;
		if (s[0])
		{
			ExecCommand(s);
		}
		s[0] = 0;
		reclaim_focus = true;
	}
	if (LogFont) ImGui::PopFont();
	ImGui::PopStyleColor();

	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	//////////////////////////////////////////////////////////

	ImGui::End();
}

bool FOutputLogPanel::PassVerbosity(ELogVerbosity Verbosity) const
{
	switch (Verbosity)
	{
	case ELogVerbosity::Input:   return bShowCommands;
	case ELogVerbosity::Warning: return bShowWarnings;
	case ELogVerbosity::Error:   return bShowErrors;
	default:                     return bShowMessages;
	}
}

void FOutputLogPanel::SaveFilteredLog()
{
	const auto Directory = FPaths::ProjectDir() / "Logs";
	std::error_code Error;
	std::filesystem::create_directories(Directory, Error);
	const auto Stamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	const auto Path = Directory / ("Output-" + std::to_string(Stamp) + ".log");
	std::ofstream File(Path, std::ios::out);
	for (const auto& Item : Items)
		if (PassVerbosity(Item.Verbosity) && Filter.PassFilter(Item.message.c_str())) File << Item.message << '\n';
	File.flush();
	const bool bSaved = !Error && File.good();
	File.close();
	if (bSaved) HTR_LOG(Info, "Output saved to {}", FPaths::ToUtf8(Path));
	else HTR_LOG(Error, "Could not save output to {}", FPaths::ToUtf8(Path));
}

void FOutputLogPanel::ClearLog()
{
	Items.Reset();
}

void FOutputLogPanel::ExecCommand(const FString& CommandLine)
{
	FLog::Log(ELogVerbosity::Input, "# {}\n", CommandLine);
	AddToHistory(CommandLine);
	ScrollToBottom = true;

	const TArray<FString> Args = TokenizeCommand(CommandLine);
	if (Args.Num() == 0) return;

	for (const FConsoleCommand& Cmd : ConsoleCommands)
	{
		if (!EqualsIgnoreCase(Args[0], Cmd.Name.c_str())) continue;
		if(!Cmd.Handler(Args, CommandLine))
			FLog::Log(ELogVerbosity::Info, "Usage: {}", Cmd.Usage);
		return;
	}
	FLog::Log(ELogVerbosity::Info, "Unknown command: '{}'", CommandLine);
}

int FOutputLogPanel::TextEditCallback(ImGuiInputTextCallbackData* data)
{
	//AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		// Example of TEXT COMPLETION

		// Locate beginning of current word
		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		// Build a list of candidates
		ImVector<FString> candidates;
		for (int i = 0; i < Commands.Num(); i++)
			if (Strnicmp(Commands[i].c_str(), word_start, (int)(word_end - word_start)) == 0)
				candidates.push_back(Commands[i]);

		if (candidates.Size == 0)
		{
			// No match
			FLog::Log(ELogVerbosity::Input, "No match for \"{}\"!\n", word_start);
		}
		else if (candidates.Size == 1)
		{
			// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
		}
		else
		{
			// Multiple matches. Complete as much as we can..
			// So inputting "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
			int match_len = (int)(word_end - word_start);
			for (;;)
			{
				int c = 0;
				bool all_candidates_matches = true;
				for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
					if (i == 0)
						c = toupper(candidates[i][match_len]);
					else if (c == 0 || c != toupper(candidates[i][match_len]))
						all_candidates_matches = false;
				if (!all_candidates_matches)
					break;
				match_len++;
			}

			if (match_len > 0)
			{
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0].c_str(), candidates[0].c_str() + match_len);
			}

			// List matches
			FLog::Log(ELogVerbosity::Info, "Possible matches:\n");
			for (int i = 0; i < candidates.Size; i++)
				FLog::Log(ELogVerbosity::Info, "- {}\n", candidates[i]);
		}

		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		// Example of HISTORY
		const int prev_history_pos = HistoryPos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (HistoryPos == -1)
				HistoryPos = History.Num() - 1;
			else if (HistoryPos > 0)
				HistoryPos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (HistoryPos != -1)
				if (++HistoryPos >= History.Num())
					HistoryPos = -1;
		}

		// A better implementation would preserve the data on the current input line along with cursor position.
		if (prev_history_pos != HistoryPos)
		{
			const char* history_str = (HistoryPos >= 0) ? History[HistoryPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
		}
	}
	}
	return 0;
}

void FOutputLogPanel::RegisterCommands()
{
	auto Add = [this](const char* Name, const char* Usage, FConsoleHandler Handler)
		{
			ConsoleCommands.Add({ Name, Usage, std::move(Handler) });
			Commands.Add(Name);   // 자동완성용 목록도 여기서 채운다
		};

	Add("HELP", "HELP", [this](const TArray<FString>&, const FString&)
		{
			FLog::Log(ELogVerbosity::Info, "Commands:");
			for (const FConsoleCommand& Cmd : ConsoleCommands)
				FLog::Log(ELogVerbosity::Info, "- {}", Cmd.Usage);
			return true;
		});

	Add("CLEAR", "CLEAR", [this](const TArray<FString>&, const FString&)
		{
			ClearLog();
			return true;
		});

	Add("HISTORY", "HISTORY", [this](const TArray<FString>&, const FString&)
		{
			const int32 First = std::max(History.Num() - 10, 0);
			for (int32 i = First; i < History.Num(); ++i)
				FLog::Log(ELogVerbosity::Info, "{:03d}: {}", i, History[i]);
			return true;
		});

	Add("STAT", "STAT <fps|memory|profile|light|decal|all|none>", [](const TArray<FString>&, const FString& Line)
		{
			FString Message;
			FStatOverlay::ExecCommand(Line, Message);   // 해석은 엔진 쪽에 맡긴다
			FLog::Log(ELogVerbosity::Info, "{}", Message);
			return true;
		});

	Add("CSM", "CSM Count <1-4> | CSM LogRatio <0.0-1.0>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 3) return false;
			if (EqualsIgnoreCase(Args[1], "COUNT"))
			{
				uint32 Count = 0;
				if (!ParseNumber(Args[2], Count) || !FSceneRenderer::SetCascadeCount(Count)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM Count: {}", Count);
				return true;
			}
			if (EqualsIgnoreCase(Args[1], "LOGRATIO"))
			{
				float Ratio = 0.0f;
				if (!ParseNumber(Args[2], Ratio) || !FSceneRenderer::SetCascadeLogRatio(Ratio)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM LogRatio: {}", Ratio);
				return true;
			}
			return false;
		});

	Add("r.shadow.CacheWholeSceneShadows", "r.shadow.CacheWholeSceneShadows [0|1]",
		[this](const TArray<FString>& Args, const FString&)
		{
			if (!EditorSettings)
			{
				FLog::Log(ELogVerbosity::Info, "Settings not available");
				return true;
			}
			bool bOn = false;
			if (!ParseToggle(Args, EditorSettings->bCacheShadows, bOn)) return false;
			EditorSettings->bCacheShadows = bOn;
			FLog::Log(ELogVerbosity::Info, "r.shadow.CacheWholeSceneShadows = {}", bOn ? 1 : 0);
			return true;
		});

	Add("r.GPUSkinning", "r.GPUSkinning [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, USkinnedMeshComponent::bUseGPUSkinning, bOn)) return false;
			USkinnedMeshComponent::SetGPUSkinningEnabled(bOn);
			FLog::Log(ELogVerbosity::Info, "r.GPUSkinning = {}", bOn ? 1 : 0);
			return true;
		});

		Add("r.Skinning.ForceUpdate", "r.Skinning.ForceUpdate [0|1]", [](const TArray<FString>& Args, const FString&)
			{
				bool bOn = false;
				if (!ParseToggle(Args, USkinnedMeshComponent::bForceSkinUpdate, bOn)) return false;
				USkinnedMeshComponent::bForceSkinUpdate = bOn;
				FLog::Log(ELogVerbosity::Info, "r.Skinning.ForceUpdate = {}", bOn ? 1 : 0);
				return true;
			});
}

void FOutputLogPanel::AddToHistory(const FString& CommandLine)
{
	HistoryPos = -1;
	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		if (History[i] == CommandLine)
		{
			History.RemoveAt(i, 1);
			break;
		}
	}
	History.Add(CommandLine);
}

void FOutputLogPanel::OnLog(ELogVerbosity Verbosity, const FString& Message)
{
	FLogData LogData;
	LogData.Verbosity = Verbosity;

	// UE Output Log 색: 일반은 밝은 회색, 경고는 노랑, 오류는 빨강.
	switch (Verbosity)
	{
	case ELogVerbosity::Input:
		LogData.Color = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
		break;
	case ELogVerbosity::Warning:
		LogData.Color = ImVec4(0.90f, 0.78f, 0.18f, 1.0f);
		break;
	case ELogVerbosity::Error:
		LogData.Color = ImVec4(0.96f, 0.30f, 0.30f, 1.0f);
		break;
	default:
		LogData.Color = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		break;
	}
	LogData.message = Message;
	Items.Add(LogData);
	ScrollToBottom = true;
}