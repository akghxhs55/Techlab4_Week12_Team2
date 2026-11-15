#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"

#include <cctype>
#include <fstream>

#include <d3d11.h>
#include <dxgi.h>
#include <ShlObj.h>
#include <wrl/client.h>

#include "imgui.h"
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "Core/Misc/Paths.h"
#include "Core/Window.h"
#include "Editor/EditorUI/EditorTitleBar.h"
#include "Render/ImageLoader.h"

#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// 최근 프로젝트 목록
// ---------------------------------------------------------------------------

namespace
{
	constexpr int32 MaxRecentProjects = 16;

	fs::path RecentProjectsFile()
	{
		return FPaths::EngineSavedDir() / L"RecentProjects.json";
	}

	bool SamePath(const fs::path& A, const fs::path& B)
	{
		std::error_code Ec;
		if (fs::equivalent(A, B, Ec)) return true;
		return _wcsicmp(A.lexically_normal().c_str(), B.lexically_normal().c_str()) == 0;
	}

	TArray<fs::path> ReadRecentFile()
	{
		TArray<fs::path> Result;
		std::ifstream In(RecentProjectsFile());
		if (!In) return Result;
		const json Root = json::parse(In, nullptr, false);
		if (!Root.is_array()) return Result;
		for (const json& Item : Root)
		{
			if (Item.is_string()) Result.Add(FPaths::FromUtf8(Item.get<FString>()));
		}
		return Result;
	}

	void WriteRecentFile(const TArray<fs::path>& Projects)
	{
		std::error_code Ec;
		fs::create_directories(FPaths::EngineSavedDir(), Ec);
		json Root = json::array();
		for (int32 i = 0; i < Projects.Num() && i < MaxRecentProjects; ++i)
		{
			Root.push_back(FPaths::ToUtf8(Projects[i]));
		}
		std::ofstream Out(RecentProjectsFile());
		if (Out) Out << Root.dump(4) << '\n';
	}
}

TArray<fs::path> RecentProjects::Load()
{
	TArray<fs::path> Result;
	std::error_code Ec;
	for (const fs::path& Path : ReadRecentFile())
	{
		if (fs::exists(Path, Ec)) Result.Add(Path);
	}
	return Result;
}

void RecentProjects::Add(const fs::path& ProjectFile)
{
	std::error_code Ec;
	const fs::path Absolute = fs::absolute(ProjectFile, Ec).lexically_normal();
	TArray<fs::path> Projects;
	Projects.Add(Absolute);
	for (const fs::path& Path : ReadRecentFile())
	{
		if (!SamePath(Path, Absolute)) Projects.Add(Path);
	}
	WriteRecentFile(Projects);
}

void RecentProjects::Remove(const fs::path& ProjectFile)
{
	TArray<fs::path> Projects;
	for (const fs::path& Path : ReadRecentFile())
	{
		if (!SamePath(Path, ProjectFile)) Projects.Add(Path);
	}
	WriteRecentFile(Projects);
}

// ---------------------------------------------------------------------------
// 새 프로젝트 만들기
// ---------------------------------------------------------------------------

namespace
{
	FString MakeDefaultGameIni(const FString& Name)
	{
		return
			"; HitoriGame 실행 설정. 프로젝트 폴더의 Config에서 읽는다.\n"
			"; 항목이 없거나 값이 잘못되면 코드의 기본값을 쓴다.\n"
			"\n"
			"[Game]\n"
			"; 시작할 씬. 프로젝트 폴더 기준 상대 경로. 에디터에서 이 경로로 씬을 저장한다.\n"
			"GameDefaultMap=Content/Scenes/Main.scene\n"
			"\n"
			"[Window]\n"
			"Title=" + Name + "\n"
			"ResolutionSizeX=1600\n"
			"ResolutionSizeY=900\n"
			"Borderless=0\n"
			"VSync=1\n";
	}
}

FString ProjectCreator::ValidateName(const FString& Name)
{
	if (Name.empty()) return "Enter a project name.";
	if (Name.size() > 32) return "Project name must be 32 characters or fewer.";
	if (!std::isalpha(static_cast<unsigned char>(Name[0]))) return "Project name must start with a letter.";
	for (const char C : Name)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		if (!(std::isalnum(U) || C == '_' || C == '-'))
			return "Use only letters, digits, '_' and '-' in the project name.";
	}
	return {};
}

bool ProjectCreator::Create(const FString& Name, const fs::path& ParentDir, fs::path& OutProjectFile, FString& OutError)
{
	OutError = ValidateName(Name);
	if (!OutError.empty()) return false;

	std::error_code Ec;
	if (ParentDir.empty() || !fs::is_directory(ParentDir, Ec))
	{
		OutError = "Location folder does not exist.";
		return false;
	}

	const fs::path ProjectDir = ParentDir / FPaths::FromUtf8(Name);
	if (fs::exists(ProjectDir, Ec) && !fs::is_empty(ProjectDir, Ec))
	{
		OutError = "Folder already exists and is not empty: " + FPaths::ToUtf8(ProjectDir);
		return false;
	}

	for (const wchar_t* Folder : { L"Content/Scenes", L"Content/Scripts", L"Config" })
	{
		fs::create_directories(ProjectDir / Folder, Ec);
		if (Ec)
		{
			OutError = "Cannot create folder: " + FPaths::ToUtf8(ProjectDir / Folder) + " (" + Ec.message() + ")";
			return false;
		}
	}

	// 아이콘, 폰트, Lua 템플릿 같은 엔진용 파일은 엔진 콘텐츠(Engine/...)에서 바로 읽으므로 복사하지 않는다.

	{
		std::ofstream Ini(ProjectDir / L"Config" / L"DefaultGame.ini");
		Ini << MakeDefaultGameIni(Name);
	}

	{
		std::ofstream Scene(ProjectDir / L"Content/Scenes/Main.scene");
		Scene << R"({"Version":4,"World":{"PersistentLevel":{"Actors":[],"Properties":{}},"Properties":{}}})";
	}

	FProjectDescriptor Descriptor;
	Descriptor.Name = Name;
	OutProjectFile = ProjectDir / (FPaths::FromUtf8(Name).native() + FPaths::ProjectExtension);
	if (!Descriptor.Save(OutProjectFile))
	{
		OutError = "Cannot write project file: " + FPaths::ToUtf8(OutProjectFile);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 프로젝트 브라우저 창
// ---------------------------------------------------------------------------

namespace
{
	// 창 자체는 FWindow가 맡고, 여기에는 브라우저만 쓰는 D3D 자원을 둔다.
	struct FBrowserWindow
	{
		HWND Hwnd = nullptr;
		ComPtr<ID3D11Device> Device;
		ComPtr<ID3D11DeviceContext> Context;
		ComPtr<IDXGISwapChain> SwapChain;
		ComPtr<ID3D11RenderTargetView> RenderTarget;
	};

	void CreateRenderTarget(FBrowserWindow& Window)
	{
		ComPtr<ID3D11Texture2D> BackBuffer;
		if (SUCCEEDED(Window.SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
			Window.Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &Window.RenderTarget);
	}

	// 엔진 콘텐츠의 로고 이미지를 텍스처로 만든다. 파일이 없거나 읽지 못하면 nullptr이고, 제목 표시줄은 빈 칸으로 남는다.
	ComPtr<ID3D11ShaderResourceView> CreateLogoTexture(ID3D11Device* Device)
	{
		const FImageData Image = ImageLoader::Load(FPaths::ResolveContentPath("Engine/Icons/KrisLogo.png"));
		if (!Image.IsValid()) return nullptr;

		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Image.Width;
		Desc.Height = Image.Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_IMMUTABLE;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA Data{};
		Data.pSysMem = Image.Pixels.GetData();
		Data.SysMemPitch = Image.GetRowPitch();

		ComPtr<ID3D11Texture2D> Texture;
		ComPtr<ID3D11ShaderResourceView> View;
		if (FAILED(Device->CreateTexture2D(&Desc, &Data, &Texture))) return nullptr;
		if (FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, &View))) return nullptr;
		return View;
	}

	bool CreateDevice(FBrowserWindow& Window)
	{
		DXGI_SWAP_CHAIN_DESC Desc{};
		Desc.BufferCount = 2;
		Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Desc.OutputWindow = Window.Hwnd;
		Desc.SampleDesc.Count = 1;
		Desc.Windowed = TRUE;
		Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0 };
		const HRESULT Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			Levels, 1, D3D11_SDK_VERSION, &Desc, &Window.SwapChain, &Window.Device, nullptr, &Window.Context);
		if (FAILED(Hr)) return false;
		CreateRenderTarget(Window);
		return true;
	}

	// 브라우저가 떠 있는 동안만 FWindow의 WndProc 훅으로 등록한다. 에디터의 ImGui 훅은 엔진 초기화 때 따로 등록된다.
	bool BrowserWndProcHook(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
	{
		return ImGui_ImplWin32_WndProcHandler(Hwnd, Msg, WParam, LParam) != 0;
	}

	// IFileOpenDialog로 폴더나 .hproject 파일을 고른다. 취소하면 빈 경로.
	fs::path ShowOpenDialog(HWND Owner, bool bPickFolder, const fs::path& InitialDir)
	{
		ComPtr<IFileOpenDialog> Dialog;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
			return {};

		DWORD Options = 0;
		Dialog->GetOptions(&Options);
		Options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
		if (bPickFolder)
		{
			Options |= FOS_PICKFOLDERS;
			Dialog->SetTitle(L"Select Project Location");
		}
		else
		{
			Options |= FOS_FILEMUSTEXIST;
			const COMDLG_FILTERSPEC Filter[] = { { L"Kris Project (*.hproject)", L"*.hproject" } };
			Dialog->SetFileTypes(1, Filter);
			Dialog->SetTitle(L"Open Project");
		}
		Dialog->SetOptions(Options);

		std::error_code Ec;
		if (!InitialDir.empty() && fs::is_directory(InitialDir, Ec))
		{
			ComPtr<IShellItem> Folder;
			if (SUCCEEDED(SHCreateItemFromParsingName(InitialDir.c_str(), nullptr, IID_PPV_ARGS(&Folder))))
				Dialog->SetFolder(Folder.Get());
		}

		if (FAILED(Dialog->Show(Owner))) return {};

		ComPtr<IShellItem> Result;
		if (FAILED(Dialog->GetResult(&Result))) return {};
		PWSTR Path = nullptr;
		if (FAILED(Result->GetDisplayName(SIGDN_FILESYSPATH, &Path))) return {};
		fs::path Out(Path);
		CoTaskMemFree(Path);
		return Out;
	}

	fs::path DefaultProjectLocation()
	{
		PWSTR Documents = nullptr;
		fs::path Result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &Documents)))
			Result = fs::path(Documents) / L"KrisProjects";
		CoTaskMemFree(Documents);
		return Result;
	}

	FString NextFreeProjectName(const fs::path& Location)
	{
		std::error_code Ec;
		for (int32 i = 1; i < 1000; ++i)
		{
			const FString Name = "MyProject" + std::to_string(i);
			if (!fs::exists(Location / FPaths::FromUtf8(Name), Ec)) return Name;
		}
		return "MyProject";
	}

	void CopyToBuffer(const FString& Text, char* Buffer, size_t Size)
	{
		strncpy_s(Buffer, Size, Text.c_str(), _TRUNCATE);
	}

	// 창 안의 화면 상태와 그리기.
	struct FBrowserUI
	{
		enum class EPage
		{
			Home,
			NewProject
		};

		TArray<fs::path> Recent;
		int32 SelectedRecent = -1;
		EPage ActivePage = EPage::Home;
		ImTextureID LogoTexture = ImTextureID_Invalid;
		char NameBuffer[64] = {};
		char LocationBuffer[1024] = {};
		FString Error;
		fs::path Chosen;

		void Init()
		{
			Recent = RecentProjects::Load();
			SelectedRecent = Recent.Num() > 0 ? 0 : -1;
			const fs::path Location = DefaultProjectLocation();
			CopyToBuffer(FPaths::ToUtf8(fs::path(Location).make_preferred()), LocationBuffer, sizeof(LocationBuffer));
			CopyToBuffer(NextFreeProjectName(Location), NameBuffer, sizeof(NameBuffer));
		}

		void TryOpen(const fs::path& ProjectFile)
		{
			FProjectDescriptor Descriptor;
			FString LoadError;
			if (!Descriptor.Load(ProjectFile, &LoadError))
			{
				Error = LoadError;
				return;
			}
			Chosen = ProjectFile;
		}

		static void PushAccentButtonColors()
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.20f, 0.17f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.27f, 0.23f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.13f, 0.11f, 1.00f));
		}

		bool DrawSidebarTab(const char* Label, bool bSelected, float Width)
		{
			const float Height = EditorStyle::Scale(32.0f);
			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const bool bClicked = ImGui::InvisibleButton(Label, ImVec2(Width, Height));
			const bool bHovered = ImGui::IsItemHovered();

			ImDrawList* DL = ImGui::GetWindowDrawList();
			if (bSelected || bHovered)
			{
				const ImU32 Bg = bSelected ? IM_COL32(46, 26, 26, 255) : IM_COL32(34, 34, 38, 255);
				DL->AddRectFilled(Pos, { Pos.x + Width, Pos.y + Height }, Bg, EditorStyle::Scale(4.0f));
			}
			if (bSelected)
			{
				const float BarW = EditorStyle::Scale(3.0f);
				DL->AddRectFilled(Pos, { Pos.x + BarW, Pos.y + Height }, IM_COL32(184, 58, 48, 255), EditorStyle::Scale(2.0f));
			}

			const ImU32 TextCol = bSelected ? IM_COL32(246, 242, 242, 255) : IM_COL32(190, 190, 196, 255);
			const float TextY = Pos.y + (Height - ImGui::GetTextLineHeight()) * 0.5f;
			DL->AddText({ Pos.x + EditorStyle::Scale(14.0f), TextY }, TextCol, Label);
			return bClicked;
		}

		void DrawSidebar(HWND Hwnd)
		{
			const float SidebarW = EditorStyle::Scale(220.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 24, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(16.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##BrowserSidebar", ImVec2(SidebarW, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);

			const float InnerW = ImGui::GetContentRegionAvail().x;
			if (LogoTexture != ImTextureID_Invalid)
			{
				const float LogoSize = EditorStyle::Scale(72.0f);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (InnerW - LogoSize) * 0.5f);
				ImGui::Image(LogoTexture, ImVec2(LogoSize, LogoSize));
				ImGui::Spacing();
			}

			auto CenteredText = [InnerW](const char* Text, ImVec4 Color)
			{
				const float TextW = ImGui::CalcTextSize(Text).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (InnerW - TextW) * 0.5f));
				ImGui::TextColored(Color, "%s", Text);
			};
			CenteredText("KRIS ENGINE", ImVec4(0.96f, 0.94f, 0.94f, 1.0f));
			CenteredText("크리스 엔진 1.0", ImVec4(0.68f, 0.44f, 0.41f, 1.0f));

			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(10.0f)));

			const float ButtonH = EditorStyle::Scale(32.0f);
			PushAccentButtonColors();
			if (ImGui::Button("+ 새 프로젝트", ImVec2(InnerW, ButtonH)))
			{
				ActivePage = EPage::NewProject;
			}
			ImGui::PopStyleColor(3);

			ImGui::Spacing();
			if (ImGui::Button("내 프로젝트 열기", ImVec2(InnerW, ButtonH)))
			{
				const fs::path File = ShowOpenDialog(Hwnd, false, {});
				if (!File.empty()) TryOpen(File);
			}

			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(12.0f)));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(8.0f)));

			if (DrawSidebarTab("최근 프로젝트", ActivePage == EPage::Home, InnerW))
			{
				ActivePage = EPage::Home;
			}
			ImGui::Spacing();
			if (DrawSidebarTab("새 프로젝트 생성", ActivePage == EPage::NewProject, InnerW))
			{
				ActivePage = EPage::NewProject;
			}

			const float BottomHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + EditorStyle::Scale(8.0f);
			if (ImGui::GetContentRegionAvail().y > BottomHeight)
			{
				ImGui::SetCursorPosY(ImGui::GetWindowHeight() - BottomHeight);
			}
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::TextDisabled("Kris Engine 1.0");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Engine: %s", FPaths::ToUtf8(FPaths::EngineDir()).c_str());
			}

			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		bool DrawProjectCard(int32 Index, const fs::path& ProjectPath, float CardW, float CardH, bool bSelected)
		{
			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const bool bClicked = ImGui::InvisibleButton("##Card", ImVec2(CardW, CardH));
			const bool bDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			const bool bHovered = ImGui::IsItemHovered();

			const FString Name = FPaths::ToUtf8(ProjectPath.stem());
			const FString Folder = FPaths::ToUtf8(fs::path(ProjectPath.parent_path()).make_preferred());
			if (bHovered)
			{
				ImGui::SetTooltip("%s", FPaths::ToUtf8(fs::path(ProjectPath).make_preferred()).c_str());
			}

			ImDrawList* DL = ImGui::GetWindowDrawList();
			const float Radius = EditorStyle::Scale(6.0f);
			const float ThumbH = EditorStyle::Scale(132.0f);
			const ImVec2 Max = { Pos.x + CardW, Pos.y + CardH };

			// 상단 썸네일 영역
			DL->AddRectFilledMultiColor(
				Pos, { Max.x, Pos.y + ThumbH },
				IM_COL32(28, 26, 28, 255), IM_COL32(22, 22, 25, 255),
				IM_COL32(14, 14, 16, 255), IM_COL32(18, 16, 18, 255));

			if (LogoTexture != ImTextureID_Invalid)
			{
				const float MarkSize = EditorStyle::Scale(56.0f);
				const ImVec2 Center = { Pos.x + CardW * 0.5f, Pos.y + ThumbH * 0.5f };
				const ImU32 Tint = bSelected ? IM_COL32(255, 235, 235, 210) : IM_COL32(255, 255, 255, 85);
				DL->AddImage(LogoTexture,
					{ Center.x - MarkSize * 0.5f, Center.y - MarkSize * 0.5f },
					{ Center.x + MarkSize * 0.5f, Center.y + MarkSize * 0.5f },
					{ 0, 0 }, { 1, 1 }, Tint);
			}

			// 하단 정보 밴드 (선택 시 마키세 크리스 적갈색 강조)
			const ImU32 FooterCol = bSelected
				? IM_COL32(158, 51, 43, 255)
				: (bHovered ? IM_COL32(44, 34, 34, 255) : IM_COL32(30, 30, 34, 255));
			DL->AddRectFilled({ Pos.x, Pos.y + ThumbH }, Max, FooterCol, Radius, ImDrawFlags_RoundCornersBottom);

			const float Pad = EditorStyle::Scale(10.0f);
			DL->PushClipRect({ Pos.x + Pad, Pos.y + ThumbH }, { Max.x - Pad, Max.y }, true);
			DL->AddText({ Pos.x + Pad, Pos.y + ThumbH + EditorStyle::Scale(8.0f) },
				IM_COL32(246, 244, 244, 255), Name.c_str());
			const ImU32 SubCol = bSelected ? IM_COL32(240, 210, 206, 255) : IM_COL32(150, 150, 156, 255);
			DL->AddText({ Pos.x + Pad, Pos.y + ThumbH + EditorStyle::Scale(30.0f) },
				SubCol, Folder.c_str());
			DL->PopClipRect();

			const ImU32 BorderCol = bSelected
				? IM_COL32(215, 86, 74, 255)
				: (bHovered ? IM_COL32(110, 62, 58, 255) : IM_COL32(46, 46, 52, 255));
			DL->AddRect(Pos, Max, BorderCol, Radius, 0, bSelected ? EditorStyle::Scale(1.8f) : 1.0f);

			if (bClicked) SelectedRecent = Index;
			return bDoubleClicked;
		}

		void DrawHomePage(HWND Hwnd, float BodyHeight)
		{
			ImGui::TextUnformatted("최근 프로젝트");
			ImGui::SameLine();

			const float RemoveW = ImGui::CalcTextSize("목록에서 제거").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float BrowseW = ImGui::CalcTextSize("찾아보기...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float RightGap = ImGui::GetStyle().ItemSpacing.x;
			const float ActionsW = RemoveW + RightGap + BrowseW;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ActionsW));

			const bool bHasSelection = SelectedRecent >= 0 && SelectedRecent < Recent.Num();
			ImGui::BeginDisabled(!bHasSelection);
			if (ImGui::Button("목록에서 제거", ImVec2(RemoveW, 0)))
			{
				RecentProjects::Remove(Recent[SelectedRecent]);
				Recent = RecentProjects::Load();
				SelectedRecent = Recent.Num() > 0 ? 0 : -1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("찾아보기...", ImVec2(BrowseW, 0)))
			{
				const fs::path File = ShowOpenDialog(Hwnd, false, {});
				if (!File.empty()) TryOpen(File);
			}

			ImGui::Spacing();

			const float FooterH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
			const float GridH = std::max(EditorStyle::Scale(180.0f), BodyHeight - ImGui::GetFrameHeightWithSpacing() - FooterH - EditorStyle::Scale(8.0f));

			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 18, 21, 255));
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(40, 38, 42, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(6.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(18.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##RecentCardsPanel", ImVec2(0.0f, GridH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

			if (Recent.IsEmpty())
			{
				ImGui::TextDisabled("최근에 연 프로젝트가 없습니다.");
				ImGui::Spacing();
				PushAccentButtonColors();
				if (ImGui::Button("새 프로젝트 만들기"))
				{
					ActivePage = EPage::NewProject;
				}
				ImGui::PopStyleColor(3);
			}
			else
			{
				const float CardW = EditorStyle::Scale(148.0f);
				const float CardH = EditorStyle::Scale(196.0f);
				const float Gap = EditorStyle::Scale(14.0f);
				const float AvailW = ImGui::GetContentRegionAvail().x;
				const int32 Columns = std::max(1, static_cast<int32>((AvailW + Gap) / (CardW + Gap)));

				for (int32 i = 0; i < Recent.Num(); ++i)
				{
					if (i > 0 && (i % Columns) != 0)
					{
						ImGui::SameLine(0.0f, Gap);
					}
					else if (i > 0)
					{
						ImGui::Dummy(ImVec2(0.0f, Gap - ImGui::GetStyle().ItemSpacing.y));
					}

					ImGui::PushID(i);
					if (DrawProjectCard(i, Recent[i], CardW, CardH, SelectedRecent == i))
					{
						TryOpen(Recent[i]);
					}
					ImGui::PopID();
				}
			}

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			ImGui::Spacing();

			const float OpenW = EditorStyle::Scale(116.0f);
			if (bHasSelection)
			{
				const float PathMaxW = std::max(60.0f, ImGui::GetContentRegionAvail().x - OpenW - EditorStyle::Scale(12.0f));
				ImGui::AlignTextToFramePadding();
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + PathMaxW);
				ImGui::TextDisabled("%s", FPaths::ToUtf8(fs::path(Recent[SelectedRecent]).make_preferred()).c_str());
				ImGui::PopTextWrapPos();
				ImGui::SameLine();
			}
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - OpenW));
			ImGui::BeginDisabled(!bHasSelection);
			PushAccentButtonColors();
			if (ImGui::Button("열기", ImVec2(OpenW, 0)) && bHasSelection)
			{
				TryOpen(Recent[SelectedRecent]);
			}
			ImGui::PopStyleColor(3);
			ImGui::EndDisabled();
		}

		void DrawNewProjectPage(HWND Hwnd, float BodyHeight)
		{
			ImGui::TextUnformatted("새 프로젝트");
			ImGui::SameLine();
			const float BackW = ImGui::CalcTextSize("최근 프로젝트로").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - BackW));
			if (ImGui::Button("최근 프로젝트로", ImVec2(BackW, 0)))
			{
				ActivePage = EPage::Home;
			}

			ImGui::Spacing();

			const float FooterH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
			const float PanelH = std::max(EditorStyle::Scale(220.0f), BodyHeight - ImGui::GetFrameHeightWithSpacing() - FooterH - EditorStyle::Scale(8.0f));

			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 18, 21, 255));
			ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(40, 38, 42, 255));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(6.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(20.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##CreateProjectPanel", ImVec2(0.0f, PanelH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

			// 템플릿 배너 카드
			{
				const ImVec2 BannerMin = ImGui::GetCursorScreenPos();
				const float BannerW = ImGui::GetContentRegionAvail().x;
				const float BannerH = EditorStyle::Scale(74.0f);
				ImDrawList* DL = ImGui::GetWindowDrawList();
				DL->AddRectFilled(BannerMin, { BannerMin.x + BannerW, BannerMin.y + BannerH }, IM_COL32(28, 22, 24, 255), EditorStyle::Scale(6.0f));
				DL->AddRectFilled(BannerMin, { BannerMin.x + EditorStyle::Scale(4.0f), BannerMin.y + BannerH }, IM_COL32(168, 52, 42, 255), EditorStyle::Scale(2.0f));
				DL->AddRect(BannerMin, { BannerMin.x + BannerW, BannerMin.y + BannerH }, IM_COL32(95, 42, 38, 255), EditorStyle::Scale(6.0f));

				float TextX = BannerMin.x + EditorStyle::Scale(18.0f);
				if (LogoTexture != ImTextureID_Invalid)
				{
					const float IconSize = EditorStyle::Scale(44.0f);
					const float IconY = BannerMin.y + (BannerH - IconSize) * 0.5f;
					DL->AddImage(LogoTexture, { TextX, IconY }, { TextX + IconSize, IconY + IconSize });
					TextX += IconSize + EditorStyle::Scale(14.0f);
				}
				DL->AddText({ TextX, BannerMin.y + EditorStyle::Scale(16.0f) }, IM_COL32(246, 244, 244, 255), "기본 프로젝트 (Blank)");
				DL->AddText({ TextX, BannerMin.y + EditorStyle::Scale(38.0f) }, IM_COL32(175, 170, 172, 255), "기본 씬과 설정이 포함된 새 크리스 엔진 프로젝트를 생성합니다.");
				ImGui::Dummy(ImVec2(BannerW, BannerH));
			}

			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(10.0f)));
			ImGui::TextUnformatted("프로젝트 이름");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##Name", NameBuffer, sizeof(NameBuffer));

			ImGui::Spacing();
			ImGui::TextUnformatted("저장 위치");
			const float BrowseWidth = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 4.0f;
			ImGui::SetNextItemWidth(-(BrowseWidth + ImGui::GetStyle().ItemSpacing.x));
			ImGui::InputText("##Location", LocationBuffer, sizeof(LocationBuffer));
			ImGui::SameLine();
			if (ImGui::Button("...", ImVec2(BrowseWidth, 0)))
			{
				const fs::path Folder = ShowOpenDialog(Hwnd, true, FPaths::FromUtf8(LocationBuffer));
				if (!Folder.empty()) CopyToBuffer(FPaths::ToUtf8(Folder), LocationBuffer, sizeof(LocationBuffer));
			}

			const FString Name = NameBuffer;
			const fs::path Location = FPaths::FromUtf8(LocationBuffer);
			const FString NameError = ProjectCreator::ValidateName(Name);

			ImGui::Spacing();
			ImGui::TextDisabled("생성될 프로젝트 파일");
			if (NameError.empty())
			{
				fs::path Preview = Location / FPaths::FromUtf8(Name) / (FPaths::FromUtf8(Name).native() + FPaths::ProjectExtension);
				ImGui::TextWrapped("%s", FPaths::ToUtf8(Preview.make_preferred()).c_str());
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.36f, 1.0f), "%s", NameError.c_str());
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Content 및 Config 폴더를 생성하며, 엔진 아이콘과 폰트는 엔진 리소스를 공유합니다.");

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			ImGui::Spacing();
			const float CreateW = EditorStyle::Scale(132.0f);
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - CreateW));
			ImGui::BeginDisabled(!NameError.empty() || Location.empty());
			PushAccentButtonColors();
			if (ImGui::Button("프로젝트 생성", ImVec2(CreateW, 0)))
			{
				std::error_code Ec;
				fs::create_directories(Location, Ec);
				fs::path ProjectFile;
				FString CreateError;
				if (ProjectCreator::Create(Name, Location, ProjectFile, CreateError))
					Chosen = ProjectFile;
				else
					Error = CreateError;
			}
			ImGui::PopStyleColor(3);
			ImGui::EndDisabled();
		}

		void Draw(HWND Hwnd)
		{
			// 에디터와 같은 제목 표시줄을 맨 위에 그린다. 그 높이만큼 WorkPos가 내려가 본문이 그 아래에 온다.
			EditorTitleBar::Draw();

			const ImGuiViewport* Viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(Viewport->WorkPos);
			ImGui::SetNextWindowSize(Viewport->WorkSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(13, 13, 16, 255));
			ImGui::Begin("Project Browser", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);

			DrawSidebar(Hwnd);
			ImGui::SameLine();

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(22.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##BrowserMain", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);

			const float ErrorHeight = Error.empty() ? 0.0f : (ImGui::GetTextLineHeightWithSpacing() * 2.0f);
			const float BodyHeight = ImGui::GetContentRegionAvail().y - ErrorHeight;

			if (ActivePage == EPage::Home)
				DrawHomePage(Hwnd, BodyHeight);
			else
				DrawNewProjectPage(Hwnd, BodyHeight);

			if (!Error.empty())
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.34f, 1.0f), "%s", Error.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("닫기")) Error.clear();
			}

			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::End();
		}
	};

	void ApplyStyle(float Scale)
	{
		EditorStyle::Apply();
		ImGui::GetStyle().ScaleAllSizes(Scale);
		ImGui::GetStyle().FontScaleDpi = Scale;
	}

}

fs::path ProjectBrowser::Run(HINSTANCE hInstance)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	// 주 모니터 DPI에 맞춰 창 크기와 글자 크기를 키운다.
	const float Scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
	const int32 Width = static_cast<int32>(1080 * Scale);
	const int32 Height = static_cast<int32>(660 * Scale);

	// 에디터 메인 창과 같은 FWindow를 쓴다. 기본 제목 표시줄 제거와 히트 테스트도 그대로 가져온다.
	FWindow MainWindow;
	FBrowserWindow Window;
	if (MainWindow.Create(hInstance, Width, Height, L"크리스 엔진"))
	{
		Window.Hwnd = MainWindow.GetHandle();
		// 닫기는 앱 종료(WM_QUIT)가 아니라 요청만 남긴다. WM_QUIT이 엔진 메인 루프까지 남으면 안 된다.
		MainWindow.SetQuitOnClose(false);

		RECT Rect;
		GetWindowRect(Window.Hwnd, &Rect);
		const int32 X = (GetSystemMetrics(SM_CXSCREEN) - (Rect.right - Rect.left)) / 2;
		const int32 Y = (GetSystemMetrics(SM_CYSCREEN) - (Rect.bottom - Rect.top)) / 2;
		SetWindowPos(Window.Hwnd, nullptr, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	fs::path Result;
	if (Window.Hwnd && CreateDevice(Window))
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		// 에디터의 imgui.ini와 섞이지 않게 창 배치를 저장하지 않는다.
		IO.IniFilename = nullptr;
		IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		float AppliedScale = ImGui_ImplWin32_GetDpiScaleForHwnd(Window.Hwnd);
		ApplyStyle(AppliedScale);
		const FString FontPath = FPaths::EngineFileUtf8(L"EngineResources/Fonts/NanumSquareR.ttf");
		std::error_code Ec;
		if (!fs::exists(FPaths::EngineFile(L"EngineResources/Fonts/NanumSquareR.ttf"), Ec) ||
			!IO.Fonts->AddFontFromFileTTF(FontPath.c_str(), 17.0f, nullptr, IO.Fonts->GetGlyphRangesKorean()))
		{
			ImFontConfig Config;
			Config.SizePixels = 15.0f;
			// Keep the fallback font stable when the window DPI changes.
			IO.Fonts->AddFontDefaultVector(&Config);
		}

		ImGui_ImplWin32_Init(Window.Hwnd);
		ImGui_ImplDX11_Init(Window.Device.Get(), Window.Context.Get());
		FWindow::SetWndProcHook(&BrowserWndProcHook);

		// 기본 제목 표시줄을 없애고 메뉴 바를 제목 표시줄로 쓴다. 클라이언트 크기가 바뀌므로 아래 루프에서 스왑체인을 맞춘다.
		EditorTitleBar::Install(MainWindow);
		// 에디터와 같은 로고를 왼쪽 위에 그린다. 엔진(UAssetManager)이 아직 없으므로 이 창의 장치로 직접 텍스처를 만든다.
		const ComPtr<ID3D11ShaderResourceView> Logo = CreateLogoTexture(Window.Device.Get());
		if (Logo) EditorTitleBar::SetLogo((ImTextureID)Logo.Get());
		MainWindow.Show();

		FBrowserUI UI;
		if (Logo) UI.LogoTexture = (ImTextureID)Logo.Get();
		UI.Init();

		bool bRunning = true;
		while (bRunning && UI.Chosen.empty())
		{
			MainWindow.ProcessMessage(bRunning);
			if (MainWindow.ConsumeCloseRequest()) break;

			if (MainWindow.CheckResized() && MainWindow.GetWidth() != 0 && MainWindow.GetHeight() != 0)
			{
				Window.RenderTarget.Reset();
				Window.SwapChain->ResizeBuffers(0, MainWindow.GetWidth(), MainWindow.GetHeight(), DXGI_FORMAT_UNKNOWN, 0);
				CreateRenderTarget(Window);
			}

			const float CurrentScale = ImGui_ImplWin32_GetDpiScaleForHwnd(Window.Hwnd);
			if (CurrentScale != AppliedScale)
			{
				ApplyStyle(CurrentScale);
				AppliedScale = CurrentScale;
			}
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			UI.Draw(Window.Hwnd);
			ImGui::Render();

			const float Clear[4] = { 0.050f, 0.050f, 0.056f, 1.0f };
			ID3D11RenderTargetView* Target = Window.RenderTarget.Get();
			Window.Context->OMSetRenderTargets(1, &Target, nullptr);
			Window.Context->ClearRenderTargetView(Target, Clear);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			Window.SwapChain->Present(1, 0);
		}

		Result = UI.Chosen;

		// 에디터가 자기 창으로 다시 Install하고 자기 훅을 등록하기 전까지 브라우저 것을 남기지 않는다.
		EditorTitleBar::Uninstall();
		FWindow::SetWndProcHook(nullptr);
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
	else
	{
		MessageBoxW(nullptr, L"Cannot create the project browser window.", L"크리스 엔진", MB_OK | MB_ICONERROR);
	}

	Window.RenderTarget.Reset();
	Window.SwapChain.Reset();
	Window.Context.Reset();
	Window.Device.Reset();
	MainWindow.Destroy();

	if (SUCCEEDED(ComResult)) CoUninitialize();
	return Result;
}
