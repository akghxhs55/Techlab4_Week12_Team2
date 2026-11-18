#include "EnginePCH.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/EditorUI/EditorStyle.h"

#include "Core/StatOverlay.h"
#include "Render/SceneRenderer.h"

#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

#include "Components/SkinnedMeshComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleTypes.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <format>
#include <memory>
#include <utility>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <sstream>
#include "Core/Misc/Paths.h"
#include <fstream>
#include <chrono>

namespace
{
	FString TrimLeftCopy(const char* Text)
	{
		if (!Text)
			return {};
		while (*Text && std::isspace(static_cast<unsigned char>(*Text)))
			++Text;
		return FString(Text);
	}

	bool StartsWithIgnoreCase(const FString& Text, const FString& Prefix)
	{
		if (Prefix.size() > Text.size())
			return false;
		for (size_t i = 0; i < Prefix.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(Text[i])) != std::tolower(static_cast<unsigned char>(Prefix[i])))
				return false;
		}
		return true;
	}

	bool ContainsIgnoreCase(const FString& Text, const FString& Needle)
	{
		if (Needle.empty())
			return true;
		if (Needle.size() > Text.size())
			return false;

		for (size_t i = 0; i <= Text.size() - Needle.size(); ++i)
		{
			bool bMatch = true;
			for (size_t j = 0; j < Needle.size(); ++j)
			{
				if (std::tolower(static_cast<unsigned char>(Text[i + j])) != std::tolower(static_cast<unsigned char>(Needle[j])))
				{
					bMatch = false;
					break;
				}
			}
			if (bMatch)
				return true;
		}
		return false;
	}

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

	if (ImGui::Begin("Output Log", GetOpenFlag()))
	{
		RenderContents();
	}
	ImGui::End();
}

void FOutputLogPanel::RenderContents()
{
	using namespace ViewportToolbarWidgets;
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
	ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorColor::BgDeepest);
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
			{ 0, 0 }, { 1, 1 }, EditorColor::TextMuted);
	}

	// 드롭다운 버튼은 UE처럼 배경 없이 그린다.
	ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Transparent);
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
	ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgDeepest);
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
	ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Transparent);
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
	RenderCommandInput("##OutputLogInput", InputBuf, IM_COUNTOF(InputBuf), -1.0f, true, true);

	//////////////////////////////////////////////////////////
}

void FOutputLogPanel::RenderCommandInput(const char* InputId, char* Buffer, size_t BufferSize, float Width, bool bUseLogFont, bool bReclaimFocusOnSubmit)
{
	if (!InputId || !Buffer || BufferSize == 0)
		return;

	CurrentInputRenderId = InputId;
	const bool bMatchesActiveInput = (ActiveSuggestionInputId == InputId);

	if (bApplySuggestionCompletion && bMatchesActiveInput)
	{
		std::snprintf(Buffer, BufferSize, "%s", PendingSuggestionText.c_str());
		LastSuggestionQuery = PendingSuggestionText;
		bApplySuggestionCompletion = false;
	}
	if (bReclaimInputFocusNextFrame && bMatchesActiveInput)
	{
		ImGui::SetKeyboardFocusHere();
		bReclaimInputFocusNextFrame = false;
	}

	bool reclaim_focus = false;
	const ImGuiInputTextFlags input_text_flags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_EscapeClearsAll |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackHistory |
		ImGuiInputTextFlags_CallbackEdit |
		ImGuiInputTextFlags_CallbackAlways;
	ImGui::SetNextItemWidth(Width);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorColor::BgDeepest);
	if (bUseLogFont && LogFont) ImGui::PushFont(LogFont, 0.0f);
	if (ImGui::InputTextWithHint(InputId, "Enter Console Command", Buffer, BufferSize, input_text_flags, &TextEditCallbackStub, this))
	{
		if (Buffer[0] != '\0')
		{
			ExecCommand(Buffer);
		}
		Buffer[0] = '\0';
		HistoryPos = -1;
		FilteredSuggestionIndices.Reset();
		SelectedSuggestion = 0;
		bSuppressSuggestions = false;
		LastSuggestionQuery.clear();
		reclaim_focus = bReclaimFocusOnSubmit;
	}
	if (bUseLogFont && LogFont) ImGui::PopFont();
	ImGui::PopStyleColor();

	const ImVec2 InputMin = ImGui::GetItemRectMin();
	const ImVec2 InputMax = ImGui::GetItemRectMax();
	const bool bInputActive = ImGui::IsItemActive();

	if (bInputActive)
	{
		ActiveSuggestionInputId = InputId;
	}

	if ((bInputActive || bMatchesActiveInput) && Buffer[0] == '\0')
	{
		FilteredSuggestionIndices.Reset();
		SelectedSuggestion = 0;
		bSuppressSuggestions = false;
		LastSuggestionQuery.clear();
	}

	if (bReclaimFocusOnSubmit)
	{
		ImGui::SetItemDefaultFocus();
	}
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	const bool bPopupTarget = (ActiveSuggestionInputId == InputId);
	const bool bShowSuggestions =
		(bInputActive || (bSuggestionPopupHovered && bPopupTarget)) &&
		!bSuppressSuggestions &&
		!FilteredSuggestionIndices.IsEmpty();
	if (bPopupTarget)
	{
		bSuggestionPopupHovered = false;
	}
	if (bShowSuggestions)
	{
		RenderAutocompletePopup(InputMin, InputMax);
	}
	CurrentInputRenderId.clear();
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
	if (bSaved) KRS_LOG(Info, "Output saved to {}", FPaths::ToUtf8(Path));
	else KRS_LOG(Error, "Could not save output to {}", FPaths::ToUtf8(Path));
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
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackAlways:
	{
		if (bMoveCursorToEndOnFocus)
		{
			data->CursorPos = data->BufTextLen;
			data->SelectionStart = data->BufTextLen;
			data->SelectionEnd = data->BufTextLen;
			bMoveCursorToEndOnFocus = false;
		}
		if (LastSuggestionQuery != data->Buf || ActiveSuggestionInputId != CurrentInputRenderId)
		{
			ActiveSuggestionInputId = CurrentInputRenderId;
			bSuppressSuggestions = false;
			HistoryPos = -1;
			UpdateSuggestions(data->Buf);
		}
		break;
	}
	case ImGuiInputTextFlags_CallbackEdit:
	{
		ActiveSuggestionInputId = CurrentInputRenderId;
		bSuppressSuggestions = false;
		HistoryPos = -1;
		UpdateSuggestions(data->Buf);
		break;
	}
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		if (!bSuppressSuggestions && !FilteredSuggestionIndices.IsEmpty())
		{
			const int32 ClampedIndex = std::clamp(SelectedSuggestion, 0, FilteredSuggestionIndices.Num() - 1);
			const FConsoleSuggestion& Choice = AllSuggestions[FilteredSuggestionIndices[ClampedIndex]];
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, Choice.InsertText.c_str());
			LastSuggestionQuery = Choice.InsertText;
			bSuppressSuggestions = true;
			break;
		}

		// 팝업 후보가 없을 때만 기존 단어 단위 prefix 보완으로 폴백
		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		ImVector<FString> candidates;
		for (int i = 0; i < Commands.Num(); i++)
			if (Strnicmp(Commands[i].c_str(), word_start, (int)(word_end - word_start)) == 0)
				candidates.push_back(Commands[i]);

		if (candidates.Size == 1)
		{
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
		}
		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		if (!bSuppressSuggestions && !FilteredSuggestionIndices.IsEmpty())
		{
			const int32 Count = FilteredSuggestionIndices.Num();
			if (data->EventKey == ImGuiKey_DownArrow)
				SelectedSuggestion = (SelectedSuggestion + 1) % Count;
			else if (data->EventKey == ImGuiKey_UpArrow)
				SelectedSuggestion = (SelectedSuggestion - 1 + Count) % Count;
			bScrollSuggestionToSelection = true;
			break;
		}

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

		if (prev_history_pos != HistoryPos)
		{
			const char* history_str = (HistoryPos >= 0) ? History[HistoryPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
			LastSuggestionQuery = history_str;
			bSuppressSuggestions = true;
		}
		break;
	}
	}
	return 0;
}

void FOutputLogPanel::RegisterSuggestion(const char* Badge, const char* DisplayText, const char* InsertText, const char* Hint, bool bIsCVar)
{
	FConsoleSuggestion Item;
	Item.Badge = Badge ? Badge : "";
	Item.DisplayText = DisplayText ? DisplayText : "";
	Item.InsertText = InsertText ? InsertText : "";
	Item.Hint = Hint ? Hint : "";
	Item.bIsCVar = bIsCVar;
	AllSuggestions.Add(std::move(Item));
}

void FOutputLogPanel::UpdateSuggestions(const char* Query)
{
	LastSuggestionQuery = Query ? Query : "";
	const FString Trimmed = TrimLeftCopy(Query);
	FilteredSuggestionIndices.Reset();
	SelectedSuggestion = 0;

	if (Trimmed.empty())
		return;

	TArray<int32> PrefixMatches;
	TArray<int32> SubstringMatches;
	const TArray<FString> QueryTokens = TokenizeCommand(Trimmed);

	for (int32 i = 0; i < AllSuggestions.Num(); ++i)
	{
		const FConsoleSuggestion& Item = AllSuggestions[i];
		if (StartsWithIgnoreCase(Item.InsertText, Trimmed) || StartsWithIgnoreCase(Item.DisplayText, Trimmed))
		{
			PrefixMatches.Add(i);
			continue;
		}

		if (ContainsIgnoreCase(Item.InsertText, Trimmed) || ContainsIgnoreCase(Item.DisplayText, Trimmed))
		{
			SubstringMatches.Add(i);
			continue;
		}

		if (QueryTokens.Num() > 1)
		{
			bool bAllTokensMatch = true;
			for (const FString& Token : QueryTokens)
			{
				if (!ContainsIgnoreCase(Item.InsertText, Token) && !ContainsIgnoreCase(Item.Badge, Token))
				{
					bAllTokensMatch = false;
					break;
				}
			}
			if (bAllTokensMatch)
				SubstringMatches.Add(i);
		}
	}

	for (int32 Index : PrefixMatches)
		FilteredSuggestionIndices.Add(Index);
	for (int32 Index : SubstringMatches)
		FilteredSuggestionIndices.Add(Index);

	bScrollSuggestionToSelection = !FilteredSuggestionIndices.IsEmpty();
}

void FOutputLogPanel::RenderAutocompletePopup(const ImVec2& InputMin, const ImVec2& InputMax)
{
	if (FilteredSuggestionIndices.IsEmpty())
		return;

	SelectedSuggestion = std::clamp(SelectedSuggestion, 0, FilteredSuggestionIndices.Num() - 1);

	const float RowHeight = EditorStyle::Scale(16.5f);
	const float BadgeWidth = EditorStyle::Scale(39.0f);
	const float PopupBorderPad = EditorStyle::Scale(2.0f);
	const int32 MaxVisibleRows = 16;
	const int32 VisibleRows = std::min(FilteredSuggestionIndices.Num(), MaxVisibleRows);
	const float PopupHeight = VisibleRows * RowHeight + PopupBorderPad * 2.0f;
	const float PopupWidth = std::max(EditorStyle::Scale(360.0f), InputMax.x - InputMin.x);

	ImGuiViewport* HostViewport = ImGui::GetWindowViewport();
	ImVec2 PopupPos(InputMin.x, InputMin.y - PopupHeight - EditorStyle::Scale(2.0f));
	if (HostViewport)
	{
		const ImVec2 WorkMin = HostViewport->WorkPos;
		const ImVec2 WorkMax(HostViewport->WorkPos.x + HostViewport->WorkSize.x,
			HostViewport->WorkPos.y + HostViewport->WorkSize.y);
		if (PopupPos.y < WorkMin.y)
			PopupPos.y = std::min(InputMax.y + EditorStyle::Scale(2.0f), WorkMax.y - PopupHeight);
		if (PopupPos.x + PopupWidth > WorkMax.x)
			PopupPos.x = std::max(WorkMin.x, WorkMax.x - PopupWidth);
		ImGui::SetNextWindowViewport(HostViewport->ID);
	}

	ImGui::SetNextWindowPos(PopupPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(PopupWidth, PopupHeight), ImGuiCond_Always);

	const ImGuiWindowFlags PopupFlags =
		ImGuiWindowFlags_Tooltip |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoDocking;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, PopupBorderPad));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 24, 28, 248));
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(72, 76, 84, 255));
	ImGui::PushFont(LogFont, ImGui::GetStyle().FontSizeBase * 0.75f);

	if (ImGui::Begin("##OutputLogAutocompletePopup", nullptr, PopupFlags))
	{
		// Output Log가 SetKeyboardFocusHere로 다시 앞으로 오더라도 팝업이 뒤에 가려지지 않게 최상단 유지
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		bSuggestionPopupHovered = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
			ImGuiHoveredFlags_ChildWindows);

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const float ContentWidth = ImGui::GetContentRegionAvail().x;
		const float FontHeight = ImGui::GetFontSize();

		for (int32 Row = 0; Row < FilteredSuggestionIndices.Num(); ++Row)
		{
			const FConsoleSuggestion& Item = AllSuggestions[FilteredSuggestionIndices[Row]];
			const bool bSelected = (Row == SelectedSuggestion);

			ImGui::PushID(Row);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			const ImVec2 RowMax(RowMin.x + ContentWidth, RowMin.y + RowHeight);

			const bool bClicked = ImGui::InvisibleButton("##SuggestionRow", ImVec2(ContentWidth, RowHeight),
				ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoHoldingActiveId);
			const bool bHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if (bHovered)
			{
				bSuggestionPopupHovered = true;
				const ImVec2 MouseDelta = ImGui::GetIO().MouseDelta;
				if (MouseDelta.x != 0.0f || MouseDelta.y != 0.0f)
					SelectedSuggestion = Row;
			}
			if (bClicked)
			{
				PendingSuggestionText = Item.InsertText;
				bApplySuggestionCompletion = true;
				bReclaimInputFocusNextFrame = true;
				bMoveCursorToEndOnFocus = true;
				bSuppressSuggestions = true;
			}

			if (bSelected && bScrollSuggestionToSelection)
			{
				ImGui::SetScrollHereY(0.5f);
				bScrollSuggestionToSelection = false;
			}

			// 왼쪽 뱃지 색상 (Stat: 라임 그린, CSM: 앰버, CVar: 블루, Cmd: 민트)
			ImU32 BadgeColor = IM_COL32(108, 186, 128, 255);
			if (Item.Badge == "Stat")
				BadgeColor = IM_COL32(142, 204, 62, 255);
			else if (Item.Badge == "CSM")
				BadgeColor = IM_COL32(214, 162, 68, 255);
			else if (Item.bIsCVar)
				BadgeColor = IM_COL32(92, 166, 214, 255);

			if (bSelected)
				DrawList->AddRectFilled(ImVec2(RowMin.x + BadgeWidth, RowMin.y), RowMax, EditorColor::Accent);
			else if (bHovered)
				DrawList->AddRectFilled(ImVec2(RowMin.x + BadgeWidth, RowMin.y), RowMax, EditorColor::BgAccentHover);

			DrawList->AddRectFilled(RowMin, ImVec2(RowMin.x + BadgeWidth, RowMax.y), BadgeColor);

			const ImVec2 BadgeTextSize = ImGui::CalcTextSize(Item.Badge.c_str());
			const ImVec2 BadgeTextPos(
				RowMin.x + std::max(EditorStyle::Scale(3.0f), (BadgeWidth - BadgeTextSize.x) * 0.5f),
				RowMin.y + (RowHeight - FontHeight) * 0.5f);
			DrawList->AddText(BadgeTextPos, IM_COL32(18, 22, 16, 255), Item.Badge.c_str());

			const float LabelX = RowMin.x + BadgeWidth + EditorStyle::Scale(6.0f);
			const float TextY = RowMin.y + (RowHeight - FontHeight) * 0.5f;
			DrawList->AddText(ImVec2(LabelX, TextY), IM_COL32(232, 234, 238, 255), Item.DisplayText.c_str());

			if (!Item.Hint.empty())
			{
				const ImVec2 LabelSize = ImGui::CalcTextSize(Item.DisplayText.c_str());
				const ImVec2 HintSize = ImGui::CalcTextSize(Item.Hint.c_str());
				const float HintRightX = RowMax.x - EditorStyle::Scale(6.0f) - HintSize.x;
				if (HintRightX > LabelX + LabelSize.x + EditorStyle::Scale(10.0f))
					DrawList->AddText(ImVec2(HintRightX, TextY), IM_COL32(140, 146, 156, 255), Item.Hint.c_str());
			}

			ImGui::PopID();
		}
	}
	ImGui::End();

	ImGui::PopFont();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(5);
}

void FOutputLogPanel::RegisterCommands()
{
	ConsoleCommands.Empty();
	Commands.Empty();
	AllSuggestions.Empty();

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

#if !defined(ENGINE_SHIPPING)
	Add("CauseCrash", "CauseCrash", [](const TArray<FString>& Args, const FString&)
		{
			// Args[0]은 명령 이름이다. 추가 인자가 있으면 사용법을 표시한다.
			if (Args.Num() != 1)
			{
				return false;
			}

			// 전용 Windows 예외를 일으켜 실제 크래시와 같은 덤프 저장 경로를 검증한다.
			constexpr DWORD CauseCrashExceptionCode = 0xE04B0001u;
			RaiseException(CauseCrashExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);

			return true;
		});
#endif

	Add("STAT", "STAT <fps|memory|profile|light|decal|skinning|all|none>", [](const TArray<FString>&, const FString& Line)
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

	Add("p.LogBursts", "p.LogBursts [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, FParticleEmitterInstance::bLogBursts, bOn)) return false;
			FParticleEmitterInstance::bLogBursts = bOn;
			FLog::Log(ELogVerbosity::Info, "p.LogBursts = {}", bOn ? 1 : 0);
			return true;
		});

	Add("p.ShowLOD", "p.ShowLOD [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, UParticleSystemComponent::bShowAllLODDistances, bOn)) return false;
			UParticleSystemComponent::bShowAllLODDistances = bOn;
			FLog::Log(ELogVerbosity::Info, "p.ShowLOD = {}", bOn ? 1 : 0);
			return true;
		});

	Add("p.LODInfo", "p.LODInfo", [](const TArray<FString>&, const FString&)
		{
			int32 Count = 0;
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
			{
				const UParticleSystemComponent* PSC = *It;
				if (!PSC || !PSC->IsRegistered()) continue;
				const UParticleSystem* Template = PSC->GetTemplate();
				const UWorld* World = PSC->GetWorld();

				int32 ActiveParticles = 0;
				bool bSpawning = false;
				for (const FParticleEmitterInstance* Instance : PSC->GetEmitterInstances())
				{
					if (!Instance) continue;
					ActiveParticles += Instance->ActiveParticles;
					bSpawning |= Instance->bSpawningEnabled;
				}

				FString Distances;
				if (Template)
				{
					for (float D : Template->GetLODDistances())
						Distances += std::format("{}{:.0f}", Distances.empty() ? "" : ", ", D);
				}

				FLog::Log(ELogVerbosity::Info, "[{}] {} | LOD {}/{} | camera {:.0f} | LODDistances [{}] | particles {} | {}",
					(World && World->IsGameWorld()) ? "Game" : "Editor",
					Template ? Template->GetAssetPath() : FString("None"),
					PSC->GetLODLevel(), Template ? Template->GetNumLODs() : 0,
					PSC->GetDistanceToNearestView(), Distances, ActiveParticles,
					bSpawning ? "Active" : "Deactivated");
				++Count;
			}
			FLog::Log(ELogVerbosity::Info, "p.LODInfo: {} components", Count);
			return true;
		});

	Add("p.LogEvents", "p.LogEvents [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, UParticleSystemComponent::bLogEvents, bOn)) return false;
			UParticleSystemComponent::bLogEvents = bOn;
			FLog::Log(ELogVerbosity::Info, "p.LogEvents = {}", bOn ? 1 : 0);
			return true;
		});

	Add("p.ShowCollisions", "p.ShowCollisions [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, UParticleSystemComponent::bShowCollisions, bOn)) return false;
			UParticleSystemComponent::bShowCollisions = bOn;
			FLog::Log(ELogVerbosity::Info, "p.ShowCollisions = {}", bOn ? 1 : 0);
			return true;
		});

	// 디버그: 모든 파티클 컴포넌트의 OnParticleCollide에 테스트 콜백을 건다
	//  log            : 30번째 충돌마다 로그
	//  clear-template : 첫 충돌 콜백 안에서 SetTemplate(nullptr) (콜백 중 컴포넌트 변경이 안전한지)
	//  off            : 건 콜백 해제
	Add("p.BindCollide", "p.BindCollide <log|clear-template|off>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 2) return false;
			static TArray<std::pair<UParticleSystemComponent*, FDelegateHandle>> Bound;

			// 이미 건 것은 먼저 해제한다 (살아 있는 컴포넌트만)
			for (const auto& [PSC, Handle] : Bound)
			{
				for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
				{
					if (*It == PSC) { PSC->OnParticleCollide.Remove(Handle); break; }
				}
			}
			Bound.Empty();
			if (EqualsIgnoreCase(Args[1], "OFF"))
			{
				FLog::Log(ELogVerbosity::Info, "p.BindCollide off");
				return true;
			}

			const bool bLog = EqualsIgnoreCase(Args[1], "LOG");
			if (!bLog && !EqualsIgnoreCase(Args[1], "CLEAR-TEMPLATE")) return false;

			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
			{
				UParticleSystemComponent* PSC = *It;
				if (!PSC || !PSC->IsRegistered()) continue;
				FDelegateHandle Handle = 0;
				if (bLog)
				{
					auto Counter = std::make_shared<int32>(0);
					Handle = PSC->OnParticleCollide.Add([Counter](UParticleSystemComponent* Source, const FParticleEventCollisionData& Event)
						{
							if ((*Counter)++ % 30 != 0) return;
							FLog::Log(ELogVerbosity::Info, "OnParticleCollide #{} '{}' at ({:.0f}, {:.0f}, {:.0f}) hit {}",
								*Counter, Event.EventName.ToString(), Event.Location.X, Event.Location.Y, Event.Location.Z,
								Event.HitActor ? Event.HitActor->GetName() : FString("None"));
						});
				}
				else
				{
					Handle = PSC->OnParticleCollide.Add([](UParticleSystemComponent* Source, const FParticleEventCollisionData&)
						{
							if (Source && Source->GetTemplate())
							{
								FLog::Log(ELogVerbosity::Info, "OnParticleCollide: SetTemplate(nullptr) inside callback");
								Source->SetTemplate(nullptr);
							}
						});
				}
				Bound.Add({ PSC, Handle });
			}
			FLog::Log(ELogVerbosity::Info, "p.BindCollide {}: {} components", Args[1], Bound.Num());
			return true;
		});

	// 디버그: 살아 있는 모든 파티클의 Freeze 플래그를 켜고 끈다 (Freeze 처리 확인용)
	Add("p.Freeze", "p.Freeze <0|1>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 2 || (Args[1] != "0" && Args[1] != "1")) return false;
			const bool bFreeze = Args[1] == "1";

			int32 Count = 0;
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
			{
				UParticleSystemComponent* PSC = *It;
				if (!PSC || !PSC->IsRegistered()) continue;
				for (FParticleEmitterInstance* Instance : PSC->GetEmitterInstances())
				{
					if (!Instance) continue;
					for (int32 i = 0; i < Instance->ActiveParticles; ++i)
					{
						uint8* Base = Instance->ParticleData + Instance->ParticleIndices[i] * Instance->ParticleStride;
						FBaseParticle& Particle = *reinterpret_cast<FBaseParticle*>(Base);
						if (bFreeze) Particle.Flags |= STATE_Particle_Freeze;
						else Particle.Flags &= ~STATE_Particle_Freeze;
						++Count;
					}
				}
			}
			FLog::Log(ELogVerbosity::Info, "p.Freeze {}: {} particles", bFreeze ? 1 : 0, Count);
			return true;
		});

	// 등록된 모든 파티클 컴포넌트 (에디터·PIE 월드 모두)
	Add("p.Particles", "p.Particles <Activate|Deactivate>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 2) return false;
			const bool bActivate = EqualsIgnoreCase(Args[1], "ACTIVATE");
			if (!bActivate && !EqualsIgnoreCase(Args[1], "DEACTIVATE")) return false;

			int32 Count = 0;
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
			{
				UParticleSystemComponent* PSC = *It;
				if (!PSC || !PSC->IsRegistered()) continue;
				if (bActivate) PSC->Activate();
				else PSC->Deactivate();
				++Count;
			}
			FLog::Log(ELogVerbosity::Info, "p.Particles {}: {} components", bActivate ? "Activate" : "Deactivate", Count);
			return true;
		});

	// 자동완성 미리보기 항목 등록 (명령 + 자주 쓰는 서브명령 조합)
	RegisterSuggestion("Cmd", "HELP", "HELP", "Show command list");
	RegisterSuggestion("Cmd", "CLEAR", "CLEAR", "Clear output log");
	RegisterSuggestion("Cmd", "HISTORY", "HISTORY", "Show recent commands");

	RegisterSuggestion("Stat", "FPS", "STAT FPS", "Frame rate overlay");
	RegisterSuggestion("Stat", "MEMORY", "STAT MEMORY", "Memory usage overlay");
	RegisterSuggestion("Stat", "PROFILE", "STAT PROFILE", "CPU/GPU profiler overlay");
	RegisterSuggestion("Stat", "LIGHT", "STAT LIGHT", "Lighting stats overlay");
	RegisterSuggestion("Stat", "DECAL", "STAT DECAL", "Decal stats overlay");
	RegisterSuggestion("Stat", "SKINNING", "STAT SKINNING", "Skeletal skinning overlay");
	RegisterSuggestion("Stat", "ALL", "STAT ALL", "Enable all stat overlays");
	RegisterSuggestion("Stat", "NONE", "STAT NONE", "Hide all stat overlays");

	RegisterSuggestion("CSM", "Count", "CSM Count ", "<1-4>");
	RegisterSuggestion("CSM", "LogRatio", "CSM LogRatio ", "<0.0-1.0>");

	RegisterSuggestion("CVar", "r.shadow.CacheWholeSceneShadows", "r.shadow.CacheWholeSceneShadows", "[0|1]", true);
	RegisterSuggestion("CVar", "r.GPUSkinning", "r.GPUSkinning", "[0|1]", true);
	RegisterSuggestion("CVar", "r.Skinning.ForceUpdate", "r.Skinning.ForceUpdate", "[0|1]", true);
	RegisterSuggestion("CVar", "p.LogBursts", "p.LogBursts", "[0|1] Log each particle burst", true);
	RegisterSuggestion("CVar", "p.ShowLOD", "p.ShowLOD", "[0|1] Draw LOD distance spheres for all particles", true);
	RegisterSuggestion("Cmd", "p.LODInfo", "p.LODInfo", "Print LOD, camera distance, particle count");
	RegisterSuggestion("CVar", "p.LogEvents", "p.LogEvents", "[0|1] Log particle event counts (needs EventGenerator)", true);
	RegisterSuggestion("CVar", "p.ShowCollisions", "p.ShowCollisions", "[0|1] Draw particle collision points and normals", true);
	RegisterSuggestion("Cmd", "p.BindCollide log", "p.BindCollide log", "Log OnParticleCollide callbacks");
	RegisterSuggestion("Cmd", "p.BindCollide clear-template", "p.BindCollide clear-template", "SetTemplate(nullptr) inside the callback");
	RegisterSuggestion("Cmd", "p.BindCollide off", "p.BindCollide off", "Remove test callbacks");
	RegisterSuggestion("Cmd", "p.Freeze 1", "p.Freeze 1", "Freeze all live particles in place");
	RegisterSuggestion("Cmd", "p.Freeze 0", "p.Freeze 0", "Unfreeze all live particles");
	RegisterSuggestion("Cmd", "p.Particles Activate", "p.Particles Activate", "Activate all particle components");
	RegisterSuggestion("Cmd", "p.Particles Deactivate", "p.Particles Deactivate", "Stop spawning on all particle components");
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
		LogData.Color = EditorColor::ToVec4(EditorColor::TextSecondary);
		break;
	case ELogVerbosity::Warning:
		LogData.Color = EditorColor::ToVec4(EditorColor::Warning);
		break;
	case ELogVerbosity::Error:
		LogData.Color = EditorColor::ToVec4(EditorColor::Error);
		break;
	default:
		LogData.Color = EditorColor::ToVec4(EditorColor::TextMuted);
		break;
	}
	LogData.message = Message;
	Items.Add(LogData);
	ScrollToBottom = true;
}
