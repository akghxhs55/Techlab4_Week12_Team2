#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"

#include <cctype>
#include <fstream>
#include <map>

#include <d3d11.h>
#include <dxgi.h>
#include <ShlObj.h>
#include <wrl/client.h>

#include "imgui.h"
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "Core/Misc/Paths.h"
#include "Core/Misc/Version.h"
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
			"; KurisuGame launch settings. Read from the project's Config folder.\n"
			"; Missing or invalid entries fall back to the defaults in code.\n"
			"\n"
			"[Game]\n"
			"; Startup scene, relative to the project folder. The editor saves the scene to this path.\n"
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

	// 새 프로젝트는 사용자별 설정 대신 엔진에 포함된 기본 설정으로 시작한다.
	for (const wchar_t* Filename : { L"editor.ini", L"imgui.ini" })
	{
		const fs::path Source = FPaths::EngineFile(L"EngineResources/Config") / Filename;
		const fs::path Target = ProjectDir / L"Config" / Filename;
		if (!fs::copy_file(Source, Target, fs::copy_options::none, Ec))
		{
			OutError = "Cannot copy default settings: " + FPaths::ToUtf8(Source) + " (" + Ec.message() + ")";
			return false;
		}
	}

	{
		std::ofstream Ini(ProjectDir / L"Config" / L"DefaultGame.ini");
		Ini << MakeDefaultGameIni(Name);
		Ini.close();
		if (!Ini)
		{
			OutError = "Cannot write game settings: " + FPaths::ToUtf8(ProjectDir / L"Config/DefaultGame.ini");
			return false;
		}
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
	ComPtr<ID3D11ShaderResourceView> CreateLogoTexture(ID3D11Device* Device, const FString& ContentPath, ImVec2* OutSize = nullptr)
	{
		const FImageData Image = ImageLoader::Load(FPaths::ResolveContentPath(ContentPath));
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
		if (OutSize) *OutSize = ImVec2(static_cast<float>(Image.Width), static_cast<float>(Image.Height));
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

	// IFileOpenDialog로 폴더나 .kproject 파일을 고른다. 취소하면 빈 경로.
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
			const COMDLG_FILTERSPEC Filter[] = { { L"Kurisu Project (*.kproject)", L"*.kproject" } };
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
			Result = fs::path(Documents) / L"KurisuProjects";
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
		// 경로로 캐시하므로 목록에서 항목을 제거해도 다른 프로젝트 이미지와 섞이지 않는다.
		std::map<fs::path, ComPtr<ID3D11ShaderResourceView>> Thumbnails;
		int32 SelectedRecent = -1;
		fs::path PendingDoubleClickOpen;   // 더블클릭한 프로젝트. 마우스 버튼을 뗀 뒤에 연다
		EPage ActivePage = EPage::Home;
		ImTextureID LogoTexture = ImTextureID_Invalid;
		ImTextureID NewProjectIcon = ImTextureID_Invalid;
		ImTextureID MyProjectsIcon = ImTextureID_Invalid;
		ImTextureID SignatureTexture = ImTextureID_Invalid;
		ImVec2 SignatureSize = {};
		ImTextureID BackgroundTexture = ImTextureID_Invalid;
		ImVec2 BackgroundSize = {};
		char NameBuffer[64] = {};
		char LocationBuffer[1024] = {};
		FString Error;
		fs::path Chosen;

		void Init(ID3D11Device* Device)
		{
			Recent = RecentProjects::Load();
			for (const fs::path& Project : Recent)
			{
				const fs::path Thumbnail = Project.parent_path() / FPaths::ProjectThumbnailRelativePath;
				std::error_code Ec;
				if (fs::is_regular_file(Thumbnail, Ec))
					Thumbnails.emplace(Project, CreateLogoTexture(Device, FPaths::ToUtf8(Thumbnail)));
			}
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
			ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::ToVec4(EditorColor::Accent));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorColor::ToVec4(EditorColor::AccentHover));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorColor::ToVec4(EditorColor::AccentActive));
		}

		// Text가 MaxWidth보다 넓으면 앞쪽을 잘라 "..."를 붙인다. UTF-8 글자 중간에서는 자르지 않는다.
		static FString ElideLeft(const FString& Text, float MaxWidth)
		{
			if (ImGui::CalcTextSize(Text.c_str()).x <= MaxWidth) return Text;

			const char* const Ellipsis = "...";
			const float Budget = MaxWidth - ImGui::CalcTextSize(Ellipsis).x;
			size_t Start = 0;
			while (Start < Text.size())
			{
				// 다음 글자의 첫 바이트로 건너뛴다(10xxxxxx는 이어지는 바이트).
				++Start;
				while (Start < Text.size() && (static_cast<unsigned char>(Text[Start]) & 0xC0) == 0x80) ++Start;
				if (ImGui::CalcTextSize(Text.c_str() + Start).x <= Budget) break;
			}
			return Ellipsis + Text.substr(Start);
		}

		// 아이콘 이미지와 글자를 가운데에 나란히 놓는 버튼. 아이콘을 못 읽었으면 글자만 그린다.
		bool DrawIconButton(const char* Id, ImTextureID Icon, const char* Label, const ImVec2& Size)
		{
			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const bool bClicked = ImGui::Button(Id, Size);

			const bool bHasIcon = Icon != ImTextureID_Invalid;
			const float IconSize = bHasIcon ? EditorStyle::Scale(16.0f) : 0.0f;
			const float Gap = bHasIcon ? EditorStyle::Scale(8.0f) : 0.0f;
			const ImVec2 TextSize = ImGui::CalcTextSize(Label);
			const float StartX = Pos.x + (Size.x - (IconSize + Gap + TextSize.x)) * 0.5f;
			const float CenterY = Pos.y + Size.y * 0.5f;

			ImDrawList* DL = ImGui::GetWindowDrawList();
			const ImU32 Col = ImGui::GetColorU32(ImGuiCol_Text);
			if (bHasIcon)
			{
				DL->AddImage(Icon, { StartX, CenterY - IconSize * 0.5f }, { StartX + IconSize, CenterY + IconSize * 0.5f }, { 0, 0 }, { 1, 1 }, Col);
			}
			DL->AddText({ StartX + IconSize + Gap, CenterY - TextSize.y * 0.5f }, Col, Label);
			return bClicked;
		}

		void DrawSidebar()
		{
			const float SidebarW = EditorStyle::Scale(220.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgPanel);
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
			CenteredText("KURISU ENGINE", EditorColor::ToVec4(EditorColor::TextPrimary));

			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(10.0f)));

			const float ButtonH = EditorStyle::Scale(32.0f);
			PushAccentButtonColors();
			if (DrawIconButton("##NewProject", NewProjectIcon, "New Project", ImVec2(InnerW, ButtonH)))
			{
				ActivePage = EPage::NewProject;
			}
			ImGui::PopStyleColor(3);

			ImGui::Spacing();
			if (DrawIconButton("##MyProjects", MyProjectsIcon, "My Projects", ImVec2(InnerW, ButtonH)))
			{
				ActivePage = EPage::Home;
			}

			// 버튼 아래 여백에만 그려 캐릭터가 가려지지 않게 한다. 원본 비율과 알파를 유지한다.
			if (SignatureTexture != ImTextureID_Invalid && SignatureSize.x > 0.0f && SignatureSize.y > 0.0f)
			{
				const ImVec2 Available = ImGui::GetContentRegionAvail();
				const float Gap = EditorStyle::Scale(24.0f);
				const float AvailableH = Available.y - Gap;
				if (Available.x > 0.0f && AvailableH > 0.0f)
				{
					const float Scale = std::min(Available.x / SignatureSize.x, AvailableH / SignatureSize.y);
					const ImVec2 Size(SignatureSize.x * Scale, SignatureSize.y * Scale);
					const ImVec2 Cursor = ImGui::GetCursorScreenPos();
					const ImVec2 Min(Cursor.x + (Available.x - Size.x) * 0.5f, Cursor.y + Available.y - Size.y);
					ImGui::GetWindowDrawList()->AddImage(SignatureTexture, Min,
						{ Min.x + Size.x, Min.y + Size.y }, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, 64));
				}
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
			if (bHovered)
			{
				ImGui::SetTooltip("%s", FPaths::ToUtf8(fs::path(ProjectPath).make_preferred()).c_str());
			}

			ImDrawList* DL = ImGui::GetWindowDrawList();
			const float Radius = EditorStyle::Scale(6.0f);
			const float ThumbH = EditorStyle::Scale(132.0f);
			const ImVec2 Max = { Pos.x + CardW, Pos.y + CardH };

			// 상단 썸네일 영역. 테두리가 둥글어서 위쪽 모서리를 같은 반경으로 깎아야 밖으로 삐져나오지 않는다
			DL->AddRectFilled(Pos, { Max.x, Pos.y + ThumbH }, EditorColor::BgDeepest, Radius, ImDrawFlags_RoundCornersTop);

			const auto Thumbnail = Thumbnails.find(ProjectPath);
			if (Thumbnail != Thumbnails.end() && Thumbnail->second)
			{
				// 이미지 비율을 유지한 채 카드 영역을 채운다(중앙 기준 crop).
				ComPtr<ID3D11Resource> Resource;
				Thumbnail->second->GetResource(&Resource);
				ComPtr<ID3D11Texture2D> Texture;
				Resource.As(&Texture);
				D3D11_TEXTURE2D_DESC Desc{};
				Texture->GetDesc(&Desc);
				const float ImageAspect = static_cast<float>(Desc.Width) / Desc.Height;
				const float CardAspect = CardW / ThumbH;
				const float U = ImageAspect > CardAspect ? (1.0f - CardAspect / ImageAspect) * 0.5f : 0.0f;
				const float V = ImageAspect < CardAspect ? (1.0f - ImageAspect / CardAspect) * 0.5f : 0.0f;
				DL->AddImageRounded((ImTextureID)Thumbnail->second.Get(), Pos, { Max.x, Pos.y + ThumbH },
					{ U, V }, { 1.0f - U, 1.0f - V }, IM_COL32_WHITE, Radius, ImDrawFlags_RoundCornersTop);
			}
			else if (LogoTexture != ImTextureID_Invalid)
			{
				const float MarkSize = EditorStyle::Scale(56.0f);
				const ImVec2 Center = { Pos.x + CardW * 0.5f, Pos.y + ThumbH * 0.5f };
				const ImU32 Tint = bSelected ? EditorColor::White(210) : EditorColor::White(85);
				DL->AddImage(LogoTexture,
					{ Center.x - MarkSize * 0.5f, Center.y - MarkSize * 0.5f },
					{ Center.x + MarkSize * 0.5f, Center.y + MarkSize * 0.5f },
					{ 0, 0 }, { 1, 1 }, Tint);
			}

			// 하단 정보 밴드 (선택 시 마키세 크리스 적갈색 강조)
			const ImU32 FooterCol = bSelected
				? EditorColor::Accent
				: (bHovered ? EditorColor::BgAccentHover : EditorColor::BgElevated);
			DL->AddRectFilled({ Pos.x, Pos.y + ThumbH }, Max, FooterCol, Radius, ImDrawFlags_RoundCornersBottom);

			const float Pad = EditorStyle::Scale(10.0f);
			DL->PushClipRect({ Pos.x + Pad, Pos.y + ThumbH }, { Max.x - Pad, Max.y }, true);
			// 경로는 선택했을 때 Open 버튼 옆에 나오므로 카드에는 이름만 밴드 가운데 높이에 그린다.
			const float NameY = Pos.y + ThumbH + (CardH - ThumbH - ImGui::GetTextLineHeight()) * 0.5f;
			DL->AddText({ Pos.x + Pad, NameY }, EditorColor::TextPrimary, Name.c_str());
			DL->PopClipRect();

			const ImU32 BorderCol = bSelected
				? EditorColor::AccentBright
				: (bHovered ? EditorColor::AccentDim : EditorColor::Border);
			DL->AddRect(Pos, Max, BorderCol, Radius, 0, bSelected ? EditorStyle::Scale(1.8f) : 1.0f);

			if (bClicked) SelectedRecent = Index;
			return bDoubleClicked;
		}

		void DrawProjectBackground()
		{
			if (BackgroundTexture == ImTextureID_Invalid || BackgroundSize.x <= 0.0f || BackgroundSize.y <= 0.0f) return;

			// 창 좌표에 고정해 목록을 스크롤해도 배경은 움직이지 않는다.
			const ImVec2 PanelPos = ImGui::GetWindowPos();
			const ImVec2 PanelSize = ImGui::GetWindowSize();
			const float Inset = EditorStyle::Scale(2.0f);
			const float AvailableW = std::max(0.0f, PanelSize.x - Inset * 2.0f);
			const float AvailableH = std::max(0.0f, PanelSize.y - Inset * 2.0f);
			const float Scale = std::min(AvailableW * 0.8f / BackgroundSize.x, AvailableH / BackgroundSize.y);
			if (Scale <= 0.0f) return;
			const ImVec2 Size(BackgroundSize.x * Scale, BackgroundSize.y * Scale);
			const ImVec2 Max(PanelPos.x + PanelSize.x - Inset, PanelPos.y + PanelSize.y - Inset);
			const ImVec2 Min(Max.x - Size.x, Max.y - Size.y);
			ImDrawList* DL = ImGui::GetWindowDrawList();
			DL->PushClipRect({ PanelPos.x + Inset, PanelPos.y + Inset }, Max, true);
			DL->AddImage(BackgroundTexture, Min, Max, { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, 80));
			// 캐릭터를 패널 색으로 서서히 덮어 카드 영역과 자연스럽게 연결한다.
			const ImU32 Opaque = EditorColor::BgPanel;
			const ImU32 Transparent = Opaque & ~IM_COL32_A_MASK;
			DL->AddRectFilledMultiColor(Min, Max, Opaque, Transparent, Transparent, Opaque);
			DL->AddRectFilledMultiColor(Min, { Max.x, Min.y + Size.y * 0.22f }, Opaque, Opaque, Transparent, Transparent);
			DL->PopClipRect();
		}

		void DrawHomePage(HWND Hwnd, float BodyHeight)
		{
			ImGui::TextUnformatted("My Projects");
			ImGui::SameLine();

			const float RemoveW = ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - RemoveW));

			ImGui::BeginDisabled(SelectedRecent < 0 || SelectedRecent >= Recent.Num());
			if (ImGui::Button("Remove", ImVec2(RemoveW, 0)))
			{
				RecentProjects::Remove(Recent[SelectedRecent]);
				Recent = RecentProjects::Load();
				SelectedRecent = Recent.Num() > 0 ? 0 : -1;
			}
			ImGui::EndDisabled();
			// 위에서 목록이 바뀔 수 있으므로 선택 여부는 제거 처리 뒤에 구한다. 마지막 항목을 지우면 선택이 없어진다.
			const bool bHasSelection = SelectedRecent >= 0 && SelectedRecent < Recent.Num();

			ImGui::Spacing();

			const float FooterH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
			const float GridH = std::max(EditorStyle::Scale(180.0f), BodyHeight - ImGui::GetFrameHeightWithSpacing() - FooterH - EditorStyle::Scale(8.0f));

			ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgPanel);
			ImGui::PushStyleColor(ImGuiCol_Border, EditorColor::Border);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(6.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(18.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##RecentCardsPanel", ImVec2(0.0f, GridH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
			DrawProjectBackground();

			if (Recent.IsEmpty())
			{
				ImGui::TextDisabled("No projects yet.");
			}
			else
			{
				const float CardW = EditorStyle::Scale(148.0f);
				const float CardH = EditorStyle::Scale(172.0f);
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
						PendingDoubleClickOpen = Recent[i];
					}
					ImGui::PopID();
				}
			}

			// 더블클릭은 두 번째 누름에서 잡히므로 바로 열면 브라우저 창이 닫힌 뒤 버튼 떼기가
			// 뒤에 있던 창(탐색기 등)으로 가서 그 창이 활성화되고 스플래시가 뒤로 밀린다.
			// Open 버튼처럼 버튼을 뗀 뒤에 연다
			if (!PendingDoubleClickOpen.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				TryOpen(PendingDoubleClickOpen);
				PendingDoubleClickOpen.clear();
			}

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			ImGui::Spacing();

			// 아래 줄: 선택한 프로젝트 경로, 그 오른쪽 끝에 Browse...와 Open.
			const float OpenW = EditorStyle::Scale(96.0f);
			const float BrowseW = OpenW;
			const float ButtonsW = BrowseW + ImGui::GetStyle().ItemSpacing.x + OpenW;
			if (bHasSelection)
			{
				const float PathMaxW = std::max(60.0f, ImGui::GetContentRegionAvail().x - ButtonsW - EditorStyle::Scale(12.0f));
				// 경로는 한 줄로만 그린다. 길면 앞쪽을 ...로 줄여 파일 이름이 보이게 하고, 전체 경로는 툴팁으로 보여준다.
				const FString FullPath = FPaths::ToUtf8(fs::path(Recent[SelectedRecent]).make_preferred());
				const FString ShownPath = ElideLeft(FullPath, PathMaxW);
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%s", ShownPath.c_str());
				if (ShownPath.size() != FullPath.size() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", FullPath.c_str());
				}
				ImGui::SameLine();
			}
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ButtonsW));
			if (ImGui::Button("Browse...", ImVec2(BrowseW, 0)))
			{
				const fs::path File = ShowOpenDialog(Hwnd, false, {});
				if (!File.empty()) TryOpen(File);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!bHasSelection);
			PushAccentButtonColors();
			if (ImGui::Button("Open", ImVec2(OpenW, 0)) && bHasSelection)
			{
				TryOpen(Recent[SelectedRecent]);
			}
			ImGui::PopStyleColor(3);
			ImGui::EndDisabled();
		}

		void DrawNewProjectPage(HWND Hwnd, float BodyHeight)
		{
			// 버튼이 없어도 제목 줄 높이를 My Projects 페이지와 같게 맞춰 아래 패널 위치가 페이지마다 달라지지 않게 한다.
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("New Project");

			ImGui::Spacing();

			const float FooterH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
			const float PanelH = std::max(EditorStyle::Scale(220.0f), BodyHeight - ImGui::GetFrameHeightWithSpacing() - FooterH - EditorStyle::Scale(8.0f));

			ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgPanel);
			ImGui::PushStyleColor(ImGuiCol_Border, EditorColor::Border);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(6.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(EditorStyle::Scale(20.0f), EditorStyle::Scale(18.0f)));
			ImGui::BeginChild("##CreateProjectPanel", ImVec2(0.0f, PanelH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
			DrawProjectBackground();

			// 템플릿 배너 카드
			{
				const char* const Description = "Creates a new Kurisu Engine project with a default scene and settings.";
				const ImVec2 BannerMin = ImGui::GetCursorScreenPos();
				const float BannerW = ImGui::GetContentRegionAvail().x;
				const float SidePad = EditorStyle::Scale(18.0f);
				const float IconSize = LogoTexture != ImTextureID_Invalid ? EditorStyle::Scale(44.0f) : 0.0f;
				const float IconGap = LogoTexture != ImTextureID_Invalid ? EditorStyle::Scale(14.0f) : 0.0f;
				// 설명은 배너 안쪽 폭에서 줄바꿈한다. 창이 좁아 여러 줄이 되면 배너를 그만큼 키운다.
				const float WrapW = std::max(EditorStyle::Scale(40.0f), BannerW - SidePad * 2.0f - IconSize - IconGap);
				const float DescriptionH = ImGui::CalcTextSize(Description, nullptr, false, WrapW).y;
				const float BannerH = std::max(EditorStyle::Scale(74.0f), EditorStyle::Scale(38.0f) + DescriptionH + EditorStyle::Scale(16.0f));
				ImDrawList* DL = ImGui::GetWindowDrawList();
				DL->AddRectFilled(BannerMin, { BannerMin.x + BannerW, BannerMin.y + BannerH }, EditorColor::BgRaised, EditorStyle::Scale(6.0f));
				DL->AddRectFilled(BannerMin, { BannerMin.x + EditorStyle::Scale(4.0f), BannerMin.y + BannerH }, EditorColor::Accent, EditorStyle::Scale(2.0f));
				DL->AddRect(BannerMin, { BannerMin.x + BannerW, BannerMin.y + BannerH }, EditorColor::AccentDim, EditorStyle::Scale(6.0f));

				float TextX = BannerMin.x + SidePad;
				if (LogoTexture != ImTextureID_Invalid)
				{
					const float IconY = BannerMin.y + (BannerH - IconSize) * 0.5f;
					DL->AddImage(LogoTexture, { TextX, IconY }, { TextX + IconSize, IconY + IconSize });
					TextX += IconSize + IconGap;
				}
				DL->AddText({ TextX, BannerMin.y + EditorStyle::Scale(16.0f) }, EditorColor::TextPrimary, "Blank Project");
				DL->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { TextX, BannerMin.y + EditorStyle::Scale(38.0f) },
					EditorColor::TextMuted, Description, nullptr, WrapW);
				ImGui::Dummy(ImVec2(BannerW, BannerH));
			}

			ImGui::Dummy(ImVec2(0.0f, EditorStyle::Scale(10.0f)));
			ImGui::TextUnformatted("Project Name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##Name", NameBuffer, sizeof(NameBuffer));

			ImGui::Spacing();
			ImGui::TextUnformatted("Project Location");
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

			// 이름이 잘못되면 Create 버튼이 꺼지므로 이유를 보여준다.
			if (!NameError.empty())
			{
				ImGui::Spacing();
				ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "%s", NameError.c_str());
			}

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			ImGui::Spacing();
			// My Projects 페이지의 Open 버튼과 같은 폭.
			const float CreateW = EditorStyle::Scale(96.0f);
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - CreateW));
			ImGui::BeginDisabled(!NameError.empty() || Location.empty());
			PushAccentButtonColors();
			if (ImGui::Button("Create", ImVec2(CreateW, 0)))
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
			ImGui::PushStyleColor(ImGuiCol_WindowBg, EditorColor::BgDeepest);
			ImGui::Begin("Project Browser", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);

			DrawSidebar();
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
				ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "%s", Error.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Close")) Error.clear();
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
	if (MainWindow.Create(hInstance, Width, Height, L"Kurisu Engine " KURISU_VERSION_STRING))
	{
		Window.Hwnd = MainWindow.GetHandle();
		// 닫기는 앱 종료(WM_QUIT)가 아니라 요청만 남긴다. WM_QUIT이 엔진 메인 루프까지 남으면 안 된다.
		MainWindow.SetQuitOnClose(false);
		// 좌측 패널과 프로젝트 카드 세 개가 한 줄에 들어가고, New Project 페이지가 스크롤 없이 보이는 최소 크기.
		MainWindow.SetMinSize(720, 480);

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
		const ComPtr<ID3D11ShaderResourceView> TitleBarLogo = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/KurisuLogoReverse.png");
		const ComPtr<ID3D11ShaderResourceView> Logo = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/KurisuLogo.png");
		if (TitleBarLogo) EditorTitleBar::SetLogo((ImTextureID)TitleBarLogo.Get());
		MainWindow.Show();

		FBrowserUI UI;
		if (Logo) UI.LogoTexture = (ImTextureID)Logo.Get();
		const ComPtr<ID3D11ShaderResourceView> NewProjectIcon = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/ProjectBrowser/NewProject.png");
		const ComPtr<ID3D11ShaderResourceView> MyProjectsIcon = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/ProjectBrowser/MyProjects.png");
		if (NewProjectIcon) UI.NewProjectIcon = (ImTextureID)NewProjectIcon.Get();
		if (MyProjectsIcon) UI.MyProjectsIcon = (ImTextureID)MyProjectsIcon.Get();
		const ComPtr<ID3D11ShaderResourceView> Signature = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/ProjectBrowser/KurisuSignature.png", &UI.SignatureSize);
		if (Signature) UI.SignatureTexture = (ImTextureID)Signature.Get();
		const ComPtr<ID3D11ShaderResourceView> Background = CreateLogoTexture(Window.Device.Get(), "Engine/Icons/ProjectBrowser/KurisuBackground.png", &UI.BackgroundSize);
		if (Background) UI.BackgroundTexture = (ImTextureID)Background.Get();
		UI.Init(Window.Device.Get());

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
		MessageBoxW(nullptr, L"Cannot create the project browser window.", L"Kurisu Engine", MB_OK | MB_ICONERROR);
	}

	Window.RenderTarget.Reset();
	Window.SwapChain.Reset();
	Window.Context.Reset();
	Window.Device.Reset();
	MainWindow.Destroy();

	if (SUCCEEDED(ComResult)) CoUninitialize();
	return Result;
}
