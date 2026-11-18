#include "EnginePCH.h"

#include "Editor/KurisuEd/EditorEngine.h"
#include "Editor/KurisuEd/Editor.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"

#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor/LightActorBase.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/TextRenderComponent.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/KurisuEd/EditorFileUtils.h"
#include "Editor/KurisuEd/EditorHierarchy.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

#include "Script/LuaScriptManager.h"
#include <sol/sol.hpp>

#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Physics/Collision.h"

#include "Audio/AudioDevice.h"
#include "Core/Misc/Paths.h"
#include "Engine/GameSettings.h"
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
	DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
	DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
	DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
	DECLARE_CYCLE_STAT("Capture World", STAT_CaptureWorld);
	DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);
	DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);

	// 좌클릭을 이만큼(픽셀) 끌면 선택이 아니라 카메라 조작으로 본다.
	// FEditorViewportClient의 클릭 선택 문턱과 같은 값이어야 한다.
	constexpr int32 LeftDragNavThreshold = 3;

	// 기본 클라이언트는 기즈모, 그리드, 선택 외곽선을 등록하지 않는다.
	class FProjectThumbnailViewport : public FEditorViewportClient
	{
	public:
		explicit FProjectThumbnailViewport(const FSceneRenderOptions& Options)
		{
			EngineShowFlags = Options;
		}
	};

	bool WriteThumbnailPNG(FTexture2D* Source, const fs::path& Path)
	{
		if (!Source || !Source->GetRawPtr() || Source->GetFormat() != DXGI_FORMAT_R8G8B8A8_UNORM)
			return false;
		ID3D11Device* Device = FRenderDevice::Get()->GetDevice();
		ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Source->GetWidth();
		Desc.Height = Source->GetHeight();
		Desc.MipLevels = Desc.ArraySize = 1;
		Desc.Format = Source->GetFormat();
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_STAGING;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> Staging;
		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Staging))) return false;
		Context->CopyResource(Staging.Get(), Source->GetRawPtr());
		D3D11_MAPPED_SUBRESOURCE Mapped{};
		if (FAILED(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped))) return false;
		const uint32 Stride = Desc.Width * 4;
		TArray<uint8> Pixels;
		Pixels.SetNum(static_cast<int32>(Stride * Desc.Height));
		for (uint32 Y = 0; Y < Desc.Height; ++Y)
		{
			uint8* Row = Pixels.GetData() + Y * Stride;
			memcpy(Row, static_cast<const uint8*>(Mapped.pData) + Y * Mapped.RowPitch, Stride);
			// WIC PNG 인코더는 32bppRGBA 요청을 BGRA로 바꾼다.
			// 렌더 타깃의 RGBA를 실제 인코더 입력인 BGRA로 변환한다.
			for (uint32 X = 0; X < Desc.Width; ++X)
			{
				std::swap(Row[X * 4], Row[X * 4 + 2]);
				Row[X * 4 + 3] = 255;
			}
		}
		Context->Unmap(Staging.Get(), 0);

		std::error_code Ec;
		fs::create_directories(Path.parent_path(), Ec);
		if (Ec) return false;
		const fs::path Temp = Path.wstring() + L".tmp";
		const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(ComResult) && ComResult != RPC_E_CHANGED_MODE) return false;
		const bool bEncoded = [&]()
		{
			ComPtr<IWICImagingFactory> Factory;
			ComPtr<IWICStream> Stream;
			ComPtr<IWICBitmapEncoder> Encoder;
			ComPtr<IWICBitmapFrameEncode> Frame;
			WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGRA;
			return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory))) &&
				SUCCEEDED(Factory->CreateStream(&Stream)) &&
				SUCCEEDED(Stream->InitializeFromFilename(Temp.c_str(), GENERIC_WRITE)) &&
				SUCCEEDED(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder)) &&
				SUCCEEDED(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache)) &&
				SUCCEEDED(Encoder->CreateNewFrame(&Frame, nullptr)) &&
				SUCCEEDED(Frame->Initialize(nullptr)) &&
				SUCCEEDED(Frame->SetSize(Desc.Width, Desc.Height)) &&
				SUCCEEDED(Frame->SetPixelFormat(&Format)) && IsEqualGUID(Format, GUID_WICPixelFormat32bppBGRA) &&
				SUCCEEDED(Frame->WritePixels(Desc.Height, Stride, Stride * Desc.Height, Pixels.GetData())) &&
				SUCCEEDED(Frame->Commit()) && SUCCEEDED(Encoder->Commit());
		}();
		if (SUCCEEDED(ComResult)) CoUninitialize();
		// 인코딩 실패 시 기존 이미지를 보존한다. 스트림을 닫은 뒤 원자적으로 교체한다.
		if (bEncoded && MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		fs::remove(Temp, Ec);
		return false;
	}
}

// 뷰포트 하나의 화면 사각형에 커서를 가둔다.
bool UEditorEngine::ClipCursorToView(int32 ViewIndex) const
{
	const FEditorViewportClient* Client = ViewportsPanel ? ViewportsPanel->GetViewportClient(ViewIndex) : nullptr;
	if (!Client) return false;
	const FRect& Rect = Client->GetRect();
	const FVector2 Origin = ViewportsPanel->GetContentOrigin();
	const RECT Clip{ static_cast<LONG>(Origin.X + Rect.X), static_cast<LONG>(Origin.Y + Rect.Y),
		static_cast<LONG>(Origin.X + Rect.X + Rect.Width), static_cast<LONG>(Origin.Y + Rect.Y + Rect.Height) };
	if (Clip.right - Clip.left < 2 || Clip.bottom - Clip.top < 2) return false;
	return ::ClipCursor(&Clip) != FALSE;
}

void UEditorEngine::BeginViewportNavCapture(int32 ViewIndex, EViewportNavButton Button, int32 AnchorX, int32 AnchorY)
{
	if (NavCaptureViewIndex != -1 || !ClipCursorToView(ViewIndex)) return;
	NavCaptureViewIndex = ViewIndex;
	NavCaptureButton = Button;
	NavAnchorX = AnchorX;
	NavAnchorY = AnchorY;
	::SetCursorPos(NavAnchorX, NavAnchorY);
	::ShowCursor(FALSE);
	// 숨은 커서가 툴바나 다른 패널을 hover하거나 누르지 않게 한다.
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
	ImGui::SetWindowFocus("Viewports");
}

void UEditorEngine::EndViewportNavCapture()
{
	if (NavCaptureViewIndex == -1) return;
	NavCaptureViewIndex = -1;
	NavCaptureButton = EViewportNavButton::None;
	::ClipCursor(nullptr);
	// UE처럼 조작을 시작한 자리에서 커서가 다시 나타난다.
	::SetCursorPos(NavAnchorX, NavAnchorY);
	::ShowCursor(TRUE);
	ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	// 되돌린 위치 기준으로 델타를 맞춰 캡처를 푼 직후 튀지 않게 한다.
	POINT Cursor{ NavAnchorX, NavAnchorY };
	if (MainWindow && ::ScreenToClient(MainWindow->GetHandle(), &Cursor))
		FInputSystem::SyncMousePosition(Cursor.x, Cursor.y);
}

#include "Core/SplashScreen.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorTitleBar.h"

// 에디터 엔진을 가리키는 전역 포인터. UE의 GEditor와 같다. Init에서 채우고 PreExit에서 비운다.
UEditorEngine* GEditor = nullptr;

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	// 창 제목에 연 프로젝트 이름을 붙인다. Title은 포인터만 넘기므로 문자열을 static으로 둔다.
	static std::wstring Title;
	Title = L"Kurisu Engine";
	if (FPaths::HasProject())
		Title += L" - " + FPaths::ProjectFilePath().stem().wstring();
	Desc.Title = Title.c_str();
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = "Engine/Splash/KurisuSplash.png";
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
		return false;

	GEditor = this;

	// FEngineLoop가 소유한 창·스왑체인·렌더러를 받아 둔다.
	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();

	// 순서에 의미가 있다. 패널은 ImGui와 렌더러를, 명령은 패널을 쓴다.
	// InitEditor만 UE에 있는 이름이다. 나머지 다섯은 UE에서 레벨 에디터 모듈과 Slate가 맡는 일이라
	// 대응하는 UE 함수가 없는, 이 엔진 고유의 단계다.
	InitEditor();
	InitLevelEditorTab();
	InitImGui();
	InitEditorRenderers();
	InitPanels();
	InitCommands();
	// 프로젝트 생성 시 이미 만들어진 대표 씬을 열어, 저장 없이 종료해도 최초 썸네일을 남긴다.
	if (FPaths::HasProject())
	{
		const FGameSettings Settings = FGameSettings::Load(FPaths::ToUtf8(FPaths::ProjectDir() / L"Config/DefaultGame.ini"));
		const fs::path Startup = FPaths::ProjectDir() / FPaths::FromUtf8(Settings.StartupMap);
		std::error_code Ec;
		if (fs::is_regular_file(Startup, Ec)) OpenSceneFromPath(FPaths::ToUtf8(Startup));
	}

	return true;
}

// 에디터 월드와 그 월드 컨텍스트를 만든다. UE의 UEditorEngine::InitEditor가 하는 일 중 이 부분에 해당한다.
void UEditorEngine::InitEditor()
{
	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();
}

// 에디터 탭 관리자와 레벨 에디터 탭을 만들고 메뉴 동작과 출력 로그를 연결한다.
void UEditorEngine::InitLevelEditorTab()
{
	EditorManager = MakeUnique<FEditorManager>(*this);
	LevelEditorTab = &EditorManager->GetLevelEditorTab();

	LevelEditorTab->OnNewScene.AddDynamic(this, &UEditorEngine::CreateNewScene);
	LevelEditorTab->OnOpenScene.AddDynamic(this, &UEditorEngine::OpenScene);
	LevelEditorTab->OnSaveScene.AddDynamic(this, &UEditorEngine::SaveCurrentScene);
	LevelEditorTab->OnSaveSceneAs.AddDynamic(this, &UEditorEngine::SaveSceneAs);
	LevelEditorTab->OnUndo.AddDynamic(this, &UEditorEngine::Undo);
	LevelEditorTab->OnRedo.AddDynamic(this, &UEditorEngine::Redo);
	LevelEditorTab->SetUndoQueries(
		[this]() { return CanUndo(); },
		[this]() { return CanRedo(); },
		[this]() { return GetUndoDescription(); },
		[this]() { return GetRedoDescription(); }
	);
	LevelEditorTab->OnImportFbx.Add([this](EFbxImportMode Mode) { FEditorFileUtils::ImportFbx(Mode); });
	LevelEditorTab->OnPackageGame.Add([this]() { Packager.Start("Release"); });
	LevelEditorTab->SetIsPackagingQuery([this]() { return Packager.IsRunning(); });

	OutputLogPanel = LevelEditorTab->AddPanel<FOutputLogPanel>();
	OutputLogPanel->SetOpen(false);
	FLog::AddSink(OutputLogPanel);
	KRS_LOG(Info, "Editor Initialize...");

	// 테스트 코드 
#ifdef ENGINE_DEBUG
	FCollision::RunSelfTest();
#endif
}

// ImGui를 초기화하고 메인 메뉴 바를 창의 제목 표시줄로 쓴다.
void UEditorEngine::InitImGui()
{
	FSplashScreen::SetText(L"Initializing Editor UI...");
	FSplashScreen::SetProgress(0.90f);

	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	KRS_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		KRS_LOG(Error, "Failed To Initialize ImGui!");
	}
	KRS_LOG(Info, "Initialize ImGui Success!");

	// 기본 제목 표시줄을 없애고 ImGui 메인 메뉴 바를 제목 표시줄로 쓴다.
	EditorTitleBar::Install(*MainWindow, true);
	// 패널이 겹치거나 잘리지 않는 최소 크기.
	MainWindow->SetMinSize(1024, 640);
	// 엔진 콘텐츠에 로고 이미지가 있으면 제목 표시줄 왼쪽 위 칸에 그린다. 없으면 빈 칸으로 둔다.
	{
		const FString LogoPath = "Engine/Icons/KurisuLogoReverse.png";
		std::error_code Ec;
		if (std::filesystem::exists(FPaths::FromUtf8(FPaths::ResolveContentPath(LogoPath)), Ec))
		{
			if (UTexture2D* Logo = UAssetManager::GetAssetByPath<UTexture2D>(LogoPath))
				EditorTitleBar::SetLogo((ImTextureID)Logo->GetResource()->GetSRV());
		}
	}
}

// 선택 상태(기즈모, 외곽선 대상)와 UI 렌더러를 만든다. 뷰포트에 그리는 도구는 뷰포트 클라이언트가 갖는다.
void UEditorEngine::InitEditorRenderers()
{
	Gizmo = MakeUnique<FGizmo>();

	Outline = MakeUnique<FOutline>();

	UIRenderer = MakeUnique<FUIRenderer>();
	UIRenderer->Init();

}

// 레벨 에디터 탭의 패널을 만들고 패널끼리, 패널과 월드·기즈모를 연결한다.
void UEditorEngine::InitPanels()
{
	DetailsPanel = LevelEditorTab->AddPanel<FDetailsPanel>();
	DetailsPanel->SetImGuiRenderer(ImGuiRenderer.get());
	// 기본 글꼴(Details 패널)이 정해진 다음에 로그 글꼴을 더한다.
	if (OutputLogPanel) OutputLogPanel->LoadLogFont();
	DetailsPanel->SetShadowPreviewQuery([this](const ULightComponent* Light, FTexture2DArray*& Atlas,
		TArray<FLightShadowRenderData>& Shadows)
		{
			Atlas = nullptr;
			Shadows.Reset();
			if (!Light || !ViewportsPanel) return false;
			const int32 Index = ViewportsPanel->GetActiveViewIndex();
			FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
			if (!Client || !Client->IsActive() || Client->GetRect().Width <= 0 || Client->GetRect().Height <= 0)
				return false;
			if (PlayWorld && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport && Index == PIEStartEditorViewIndex)
			{
				FWorldContext* Context = GetPIEWorldContext();
				if (!Context || !Context->GameViewport || Light->GetWorld() != Context->World()) return false;
				return Context->GameViewport->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
			}
			if (Light->GetWorld() != (PlayWorld ? PlayWorld : EditorWorld)) return false;
			return Client->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
		});


	ViewportsPanel = LevelEditorTab->AddPanel<FViewportsPanel>();
	ContentDrawerPanel = LevelEditorTab->AddPanel<FContentDrawerPanel>();
	ContentDrawerPanel->SetOpen(false);
	LevelEditorTab->SetDrawerPanels(ContentDrawerPanel, OutputLogPanel);

	EditorSettings = MakeUnique<FEditorSettingsManager>();
	EditorSettings->Init();
	OutputLogPanel->SetEditorSettings(&EditorSettings->GetMutableSettings());

	// 위젯 에셋은 다른 에셋처럼 EditorManager가 여는 탭에서 편집한다.
	// 새로 만들 때는 디스크에 바로 만들어 콘텐츠 드로어에 곧장 보이도록 한 뒤 연다.
	ContentDrawerPanel->SetCreateWidgetCallback([this](const FString& Path)
	{
		if (FUIEditor::CreateDefaultAsset(Path))
		{
			EditorManager->OpenEditor(Path);
		}
	});
	ContentDrawerPanel->SetOpenWidgetCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	// StaticMesh / SkeletalMesh Viewer 열기
	ContentDrawerPanel->SetOpenMeshCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	// .scene 더블클릭 → 레벨 에디터에 그 씬을 연다
	ContentDrawerPanel->SetOpenSceneCallback([this](const FString& Path) { OpenSceneFromPath(Path); });
	ContentDrawerPanel->SetImportFbxCallback([](EFbxImportMode Mode) { FEditorFileUtils::ImportFbx(Mode); });

	ContentDrawerPanel->SetBlendSpaceCreatedCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });
    ContentDrawerPanel->SetAnimBlueprintCreatedCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	// 화면 나눔 비율 설정 가져오기
	ViewportSplitRatio = {
		EditorSettings->GetSettings().MultipleViewportsHorizontal,
		EditorSettings->GetSettings().MultipleViewportsVertical
	};
	// SingleView에 사용할 인덱스 설정
	SingleViewportIndex = EditorSettings->GetSettings().MultipleViewportsSingleViewIndex;
	// 뷰포트 레이아웃 설정
	{
		const int32 SavedLayout = EditorSettings->GetSettings().ViewportLayout;
		ViewportLayoutMode = SavedLayout >= 0 && SavedLayout < static_cast<int32>(ELayoutMode::Count)
			? static_cast<ELayoutMode>(SavedLayout)
			: ELayoutMode::QuadSplit;
	}
	EditorWorld->GetMainCamera()->GetCameraComponent()->SetExternalInputManaged(true);

	OutlinerPanel = LevelEditorTab->AddPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetMultiSelectionCallback(
		[this](const TArray<AActor*>& SelectedActors)
		{
			AActor* Primary = SelectedActors.IsEmpty() ? nullptr : SelectedActors.Last();
			USceneComponent* Root = Primary ? Primary->GetRootComponent() : nullptr;
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTargets(SelectedActors);

			TArray<UPrimitiveComponent*> Primitives;
			for (AActor* Actor : SelectedActors)
			{
				if (!Actor)
				{
					continue;
				}
				for (UActorComponent* C : Actor->GetComponents())
				{
					if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
					{
						Primitives.Add(Prim);
					}
				}
			}
			Outline->SetTargets(Primitives);
		});
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
					if ((Primitive = Cast<UPrimitiveComponent>(C))) break;
			}
			Outline->SetTarget(Primitive);
		});

	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				AActor* Owner = SceneComponent->GetOwner();
				if (Owner && SceneComponent == Owner->GetRootComponent())
				{
					Gizmo->SetTarget(SceneComponent, true);
				}
				else
				{
					Gizmo->SetTarget(SceneComponent);
				}
			}
			else
			{
				Gizmo->SetTarget(nullptr);
			}
		}
	);

	OutlinerPanel->SetUndoHistory(&UndoHistory);
	DetailsPanel->SetUndoHistory(&UndoHistory);

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		}
	);
	OutlinerPanel->SetDeleteActorsCallback(
		[this](const TArray<AActor*>& Actors)
		{
			DeleteActors(Actors);
		}
	);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	DetailsPanel->SetWorld(GetActiveWorld());

	UWorld::SetOnActorDestroyedCallback(
		[this](UWorld* /*DestroyedInWorld*/, AActor* DestroyedActor)
		{
			if (!DestroyedActor)
			{
				return;
			}

			if (Gizmo && Gizmo->GetTarget() && Gizmo->GetTarget()->GetOwner() == DestroyedActor)
			{
				Gizmo->SetTarget(nullptr, false);
			}
			if (Outline)
			{
				const TArray<UPrimitiveComponent*>& CurTargets = Outline->GetTargets();
				bool bHadTarget = false;
				TArray<UPrimitiveComponent*> Remaining;
				for (UPrimitiveComponent* Prim : CurTargets)
				{
					if (Prim && Prim->GetOwner() == DestroyedActor)
					{
						bHadTarget = true;
					}
					else if (Prim)
					{
						Remaining.Add(Prim);
					}
				}
				if (bHadTarget)
				{
					Outline->SetTargets(Remaining);
				}
			}
			if (DetailsPanel)
			{
				DetailsPanel->OnActorDestroyed(DestroyedActor);
			}
			if (OutlinerPanel)
			{
				OutlinerPanel->OnActorDestroyed(DestroyedActor);
			}
		});

	ViewportsPanel->SetWorld(EditorWorld);
	ViewportsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	ViewportsPanel->SetGizmo(Gizmo.get());
	ViewportsPanel->SetEditorSettings(EditorSettings.get());
	ViewportsPanel->SetActorSpawnedCallback([this](AActor* Actor)
	{
		OutlinerPanel->SelectActor(Actor);
		if (Actor && !IsPlaying() && UndoHistory.CanRecord())
		{
			FActorUndoSnapshot Snapshot;
			if (EditorUndoUtils::CaptureActorSnapshot(Actor, Snapshot))
			{
				UndoHistory.Push(EditorUndoUtils::MakeActorCreateDeleteCommand("Spawn Actor", std::move(Snapshot), true));
			}
		}
	});

	EditorSettings->SetViewportsPanel(ViewportsPanel);
	ViewportsPanel->SetLayoutMode(ViewportLayoutMode, SingleViewportIndex);

	for (int32 i = 0; i < 4; ++i)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i))
		{
			Client->SetWorld(EditorWorld);
			AllViewportClients.Add(Client);
		}
	}
	EditorSettings->ApplyViewportSettings();
}

// 저장·열기·Play 같은 에디터 명령과 단축키를 정의하고 툴바에 넘긴다.
void UEditorEngine::InitCommands()
{
	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Crimson = { 0.86f, 0.25f, 0.22f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true } };

	Save.CanExecute = [this]()->bool {return !PlayWorld; };
	Save.Execute = [this]() { SaveCurrentScene(); };

	// UE: Save As = Ctrl+Alt+S
	SaveAs = { "Save As", "Save Current Scene As", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true, true } };

	SaveAs.CanExecute = [this]()->bool {return !PlayWorld; };
	SaveAs.Execute = [this]() { SaveSceneAs(); };

	// UE: New Level = Ctrl+N, Open Level = Ctrl+O
	NewScene = { "New Scene", "Create a new scene", nullptr, White, { EKeyCode::N, true } };

	NewScene.CanExecute = [this]()->bool {return !PlayWorld; };
	NewScene.Execute = [this]() { CreateNewScene(); };

	OpenSceneCommand = { "Open Scene", "Load an existing scene", nullptr, White, { EKeyCode::O, true } };

	OpenSceneCommand.CanExecute = [this]()->bool {return !PlayWorld; };
	OpenSceneCommand.Execute = [this]() { OpenScene(); };

	Play = { "Play",   "Play World",        UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Play.png"),  Crimson, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	// 툴바 아이콘은 UE5 Starship SVG(MainToolbar/PlaySimulate_20, eject, PlayPossess_20)를 PNG로 바꾼 것이다.
	Simulate = { "Simulate", "Simulate", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Simulate.png"), White, { EKeyCode::S, false, true } };

	Simulate.CanExecute = [this]()->bool {return !PlayWorld; };
	Simulate.IsVisible = [this]()->bool {return !PlayWorld; };
	Simulate.Execute = [this]() { RequestSimulateSession(); };

	// UE처럼 실행 중에는 Simulate 자리가 Eject(게임 → SIE) / Possess(SIE → 게임) 버튼이 된다. 단축키는 F8.
	// F8 키 자체는 TickPIEInput이 처리하므로 여기 Shortcut은 툴팁 표시용이다 (ProcessShortcuts에 넣지 않는다).
	Eject = { "Eject", "Eject from player controller", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/PIEEject.png"), White, { EKeyCode::F8 } };
	Eject.IsVisible = [this]()->bool { return PlayWorld && !bIsSimulatingInEditor; };
	Eject.CanExecute = [this]()->bool { return PlayWorld && !IsPIEInWindow(); };
	Eject.Execute = [this]() { bTogglePIESIERequested = true; };

	Possess = { "Possess", "Possess the player controller", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Possess.png"), White, { EKeyCode::F8 } };
	Possess.IsVisible = [this]()->bool { return PlayWorld && bIsSimulatingInEditor; };
	Possess.Execute = [this]() { bTogglePIESIERequested = true; };

	Pause = { "Pause",  "Pause",			UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Pause.png"), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; if (PlayWorld) PlayWorld->SetPaused(true); };


	Resume = { "Resume", "Resume",          UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Play.png"),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; if (PlayWorld) PlayWorld->SetPaused(false); };

	StepFrame = { "Step", "Step One Frame", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Step.png"),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested = true; };


	Stop = { "Stop",   "Stop",				UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Stop.png"),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	// UE처럼 Play/Pause/Resume은 한 자리에서 상태에 맞는 하나만 보인다. Step/Stop은 항상 보이고 쓸 수 없을 때 비활성.
	Play.IsVisible = [this]()->bool { return !PlayWorld; };
	Pause.IsVisible = [this]()->bool { return PlayWorld && !bIsPaused; };
	Resume.IsVisible = [this]()->bool { return PlayWorld && bIsPaused; };
	Resume.IconTint = Crimson;

	FLevelEditorToolbar& LevelEditorToolbar = *LevelEditorTab->GetToolbar();
	LevelEditorToolbar.SetCommands({ Play, Pause, Resume, StepFrame, Stop, Simulate, Eject, Possess });
	LevelEditorToolbar.SetPlacementMenus(
		[this]() { if (ViewportsPanel) ViewportsPanel->DrawSpawnMenuForActiveView(); },
		[this]() { if (ViewportsPanel) ViewportsPanel->DrawGridMenu(); },
		[this]() { return ViewportsPanel ? ViewportsPanel->GetGridSpacing() : 1; });
	LevelEditorToolbar.SetLocationSnapBinding(
		[this]() { return ViewportsPanel ? ViewportsPanel->IsLocationSnapEnabled() : false; },
		[this]() { return ViewportsPanel ? ViewportsPanel->GetLocationSnapSize() : 1.0f; },
		[this]() { if (ViewportsPanel) ViewportsPanel->DrawSnapMenu(); });
	LevelEditorToolbar.SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창 닫기 요청 처리
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
		RequestEndPlayMap();
	// 에디터 단축키 (UE와 같은 키: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Alt+S, Alt+P, Pause).
	// Stop(Esc)은 PIE 입력 처리에서 따로 받으므로 여기서 빼서 두 번 실행되지 않게 한다.
	EditorCommandUI::ProcessShortcuts({ &NewScene, &OpenSceneCommand, &Save, &SaveAs, &Play, &Simulate, &Pause, &Resume });

	if (bRequestEndPlayMapQueued) { bRequestEndPlayMapQueued = false; EndPlayMap(); }
	StartQueuedPlaySessionRequest();
	// PIE 중 OpenLevel 요청 처리. 월드 Tick 전에 해야 지운 월드를 Tick하지 않는다.
	if (PlayWorld)
		if (FWorldContext* PIEContext = GetWorldContextFromWorld(PlayWorld))
			TickWorldTravel(*PIEContext);

	// 패키징 진행, 통계 표시, 패널에 넘길 DeltaTime을 갱신하고 Delete 키를 처리한다.
	Packager.Tick();
	FStatOverlay::Tick(DeltaTime);

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteActors(OutlinerPanel->GetSelectedActors());

	// 뷰포트 레이아웃과 에디터 카메라를 갱신한다. 패널의 요청(레이아웃 변경, 분할선 드래그)을 적용하고,
	// 어느 View 위에 마우스가 있는지 찾아 활성 View를 정한 뒤 View마다 카메라 입력을 처리한다.
	{
		SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);

		const FVector2 ViewportSize = ViewportsPanel->GetContentSize();
		const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();

		ELayoutMode RequestedLayout{};
		int32 RequestedSingleViewIndex = SingleViewportIndex;

		if (ViewportsPanel->ConsumeLayoutRequest(RequestedLayout, RequestedSingleViewIndex))
		{
			if (RequestedLayout == ELayoutMode::Single)
				SingleViewportIndex = RequestedSingleViewIndex;
			ViewportLayoutMode = RequestedLayout;

			FEditorSettings& Settings = EditorSettings->GetMutableSettings();
			Settings.ViewportLayout = static_cast<int32>(RequestedLayout);
			Settings.MultipleViewportsSingleViewIndex = RequestedSingleViewIndex;
		}

		const float HorizontalDrag = ViewportsPanel->ConsumeHorizontalDrag();
		const float VerticalDrag = ViewportsPanel->ConsumeVerticalDrag();
		if (HorizontalDrag != 0.0f)
			ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Horizontal, HorizontalDrag, ViewportSize, 0.1f);
		if (VerticalDrag != 0.0f)
			ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Vertical, VerticalDrag, ViewportSize, 0.1f);
		if (HorizontalDrag != 0.0f || VerticalDrag != 0.0f)
		{
			EditorSettings->GetMutableSettings().MultipleViewportsHorizontal = ViewportSplitRatio.Horizontal;
			EditorSettings->GetMutableSettings().MultipleViewportsVertical = ViewportSplitRatio.Vertical;
		}

		FRect ViewRects[4]{};
		if (ViewportLayoutMode == ELayoutMode::Single)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				ViewRects[i] = (i == SingleViewportIndex)
					? FRect{ 0.0f, 0.0f, ViewportSize.X, ViewportSize.Y }
				: FRect{};
			}
		}
		else
		{
			ComputeLayoutRects(ViewportLayoutMode, ViewportSplitRatio, ViewportSize, ViewRects);
		}
		ViewportsPanel->SetSplitRatio(ViewportSplitRatio);

		bool bSlotActives[4]{};
		for (int32 i = 0; i < 4; ++i)
		{
			const bool bSlotActive = (ViewportLayoutMode == ELayoutMode::Single)
				? (i == SingleViewportIndex)
				: i < GetLayoutPaneCount(ViewportLayoutMode);
			bSlotActives[i] = bSlotActive;
			ViewportsPanel->SetView(i, ViewRects[i], bSlotActive);
		}

		const int32 CurrentActiveViewIndex = ViewportsPanel->GetActiveViewIndex();
		if (CurrentActiveViewIndex < 0 || CurrentActiveViewIndex >= 4 || !bSlotActives[CurrentActiveViewIndex])
		{
			for (int32 i = 0; i < 4; ++i)
			{
				if (bSlotActives[i])
				{
					ViewportsPanel->SetActiveViewIndex(i);
					break;
				}
			}
		}

		// 마우스 호버 감지
		int32 HoveredViewIndex = -1;
		if (ViewportsPanel->IsHovered())
		{
			for (int32 i = 0; i < 4; ++i)
			{
				if (ViewRects[i].Width > 0.0f && ViewRects[i].Height > 0.0f &&
					LocalMousePosition.X >= ViewRects[i].X &&
					LocalMousePosition.X < ViewRects[i].X + ViewRects[i].Width &&
					LocalMousePosition.Y >= ViewRects[i].Y &&
					LocalMousePosition.Y < ViewRects[i].Y + ViewRects[i].Height)
				{
					HoveredViewIndex = i;
					break;
				}
			}
		}

		// PIE 게임 화면을 보여 주는 뷰의 카메라는 게임이 소유한다.
		// 여기서 에디터 카메라까지 움직이면 같은 마우스 입력이 두 번 적용된다.
		// SIE에서는 그 뷰도 에디터 뷰포트로 돌아와 있으므로 다른 뷰처럼 에디터 카메라를 움직인다.
		const bool bSimulating = IsSimulatingInEditor();
		const int32 GameOwnedViewIndex = (PlayWorld && !bSimulating) ? PIEViewIndex : InvalidViewIndex;

		// 카메라 조작 캡처. UE처럼 버튼을 누르고 있는 동안 커서를 숨기고 뷰포트에 가둔다.
		// 이동량은 "지난 프레임에 되돌려 놓은 기준점에서 얼마나 움직였나"로 읽어서
		// 커서가 화면이나 뷰포트 가장자리에 닿아도 회전이 멈추지 않는다.
		if (NavCaptureViewIndex != -1)
		{
			const EMouseButton Held = NavCaptureButton == EViewportNavButton::Right ? EMouseButton::Right : EMouseButton::Left;
			if (!FInputSystem::IsMouseDown(Held) || bGameMouseCaptured || !ViewportsPanel->GetViewportClient(NavCaptureViewIndex) ||
				!ViewportsPanel->GetViewportClient(NavCaptureViewIndex)->IsActive())
				EndViewportNavCapture();
		}
		const bool bCanStartNav = NavCaptureViewIndex == -1 && !bGameMouseCaptured &&
			HoveredViewIndex != -1 && HoveredViewIndex != GameOwnedViewIndex;
		POINT CursorNow{};
		const bool bHasCursor = ::GetCursorPos(&CursorNow) != FALSE;
		if (bCanStartNav && bHasCursor && FInputSystem::IsMousePressed(EMouseButton::Right))
		{
			BeginViewportNavCapture(HoveredViewIndex, EViewportNavButton::Right, CursorNow.x, CursorNow.y);
		}
		// 좌클릭은 바로 캡처하지 않는다. 클릭(선택)과 구분하려고 조금 끌었을 때만 카메라 조작으로 바꾼다.
		// 기즈모 축 위에서 누른 경우는 기즈모 드래그이므로 제외한다.
		if (bCanStartNav && bHasCursor && FInputSystem::IsMousePressed(EMouseButton::Left) &&
			Gizmo && Gizmo->GetHoveredAxis() < 0 && !Gizmo->IsUsing() && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyCtrl)
		{
			LeftPressViewIndex = HoveredViewIndex;
			LeftPressX = CursorNow.x;
			LeftPressY = CursorNow.y;
		}
		if (!FInputSystem::IsMouseDown(EMouseButton::Left) || (Gizmo && Gizmo->IsUsing()))
			LeftPressViewIndex = -1;
		if (LeftPressViewIndex != -1 && NavCaptureViewIndex == -1 && bHasCursor)
		{
			const int32 DX = CursorNow.x - LeftPressX, DY = CursorNow.y - LeftPressY;
			if (DX * DX + DY * DY > LeftDragNavThreshold * LeftDragNavThreshold)
			{
				BeginViewportNavCapture(LeftPressViewIndex, EViewportNavButton::Left, LeftPressX, LeftPressY);
				LeftPressViewIndex = -1;
			}
		}

		float NavMouseX = 0.0f, NavMouseY = 0.0f;
		if (NavCaptureViewIndex != -1 && ::GetCursorPos(&CursorNow))
		{
			NavMouseX = static_cast<float>(CursorNow.x - NavAnchorX);
			NavMouseY = static_cast<float>(CursorNow.y - NavAnchorY);
			// 패널 이동·크기 변경을 따라가도록 매 프레임 다시 가두고 기준점으로 되돌린다.
			ClipCursorToView(NavCaptureViewIndex);
			::SetCursorPos(NavAnchorX, NavAnchorY);
		}

		const int32 CapturedViewportIndex = NavCaptureButton == EViewportNavButton::Right ? NavCaptureViewIndex : -1;
		const int32 LeftDragViewportIndex = NavCaptureButton == EViewportNavButton::Left ? NavCaptureViewIndex : -1;
		if (NavCaptureViewIndex != -1)
		{
			ViewportsPanel->SetActiveViewIndex(NavCaptureViewIndex);
		}
		else if (FInputSystem::IsMousePressed(EMouseButton::Left) && HoveredViewIndex != -1)
		{
			ViewportsPanel->SetActiveViewIndex(HoveredViewIndex);
		}

		int32 WheelDelta = FInputSystem::GetWheelDelta();
		// 우클릭 캡처 중 휠은 카메라 속도. 게임 화면 View는 캡처할 수 없으므로 PIE/SIE 중에도 그대로 허용한다.
		if (CapturedViewportIndex != -1 && WheelDelta != 0)
		{
			auto& Settings = EditorSettings->GetMutableSettings();
			Settings.CameraSpeed = FMath::Clamp(Settings.CameraSpeed * powf(1.2f, WheelDelta / 120.0f), 0.1f, 100.0f);
			WheelDelta = 0;
		}
		const float MoveSpeed = 10.0f * EditorSettings->GetSettings().CameraSpeed;
		const float MouseSens = 0.2f * EditorSettings->GetSettings().MouseSensitivity;

		for (int32 i = 0; i < 4; ++i)
		{
			FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i);
			if (!Client || !Client->IsActive() || i == GameOwnedViewIndex)
				continue;

			Client->TickInput(DeltaTime, i == CapturedViewportIndex,
				i == HoveredViewIndex, WheelDelta, MoveSpeed, MouseSens,
				NavMouseX, NavMouseY, i == LeftDragViewportIndex);
		}

		if (!PlayWorld && EditorManager->GetFocusedTab() == LevelEditorTab && !ImGui::GetIO().WantTextInput &&
			(ViewportsPanel->IsFocused() || OutlinerPanel->IsFocused()))
		{
			if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift)
			{
				if (FInputSystem::IsKeyPressed(EKeyCode::C)) CopySelectedActor();
				if (FInputSystem::IsKeyPressed(EKeyCode::V)) PasteActor();
				if (FInputSystem::IsKeyPressed(EKeyCode::D))
				{
					const TArray<AActor*> Selected = OutlinerPanel->GetSelectedActors();
					TArray<AActor*> Duplicates;
					for (AActor* Actor : Selected)
					{
						if (AActor* Duplicate = EditorWorld->DuplicateActor(Actor))
						{
							Duplicates.Add(Duplicate);
						}
					}
					if (!Duplicates.IsEmpty())
					{
						OutlinerPanel->SelectActors(Duplicates);
						if (!IsPlaying() && UndoHistory.CanRecord())
						{
							TArray<TUniquePtr<IEditorUndoCommand>> Cmds;
							TArray<FName> DupNames;
							for (AActor* Dup : Duplicates)
							{
								FActorUndoSnapshot Snapshot;
								if (EditorUndoUtils::CaptureActorSnapshot(Dup, Snapshot))
								{
									DupNames.Add(Dup->GetFName());
									Cmds.Add(EditorUndoUtils::MakeActorCreateDeleteCommand("Duplicate Actor", std::move(Snapshot), true));
								}
							}
							if (Cmds.Num() == 1)
							{
								UndoHistory.Push(std::move(Cmds[0]));
							}
							else if (Cmds.Num() > 1)
							{
								UndoHistory.Push(EditorUndoUtils::MakeCompositeCommand("Duplicate Actors", std::move(Cmds), {}, DupNames));
							}
						}
					}
				}
			}
			else if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt &&
				CapturedViewportIndex == -1 && FInputSystem::IsKeyPressed(EKeyCode::F))
			{
				const TArray<AActor*>& Selected = OutlinerPanel->GetSelectedActors();
				if (!Selected.IsEmpty())
				{
					if (auto* Client = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()))
					{
						FBox CombinedBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
						bool bHasValid = false;
						for (AActor* Actor : Selected)
						{
							if (!Actor) continue;
							FBox Box;
							if (!Actor->TryGetActorBounds(Box))
							{
								const FVector Loc = Actor->GetActorLocation();
								Box = FBox(Loc - FVector(0.5f, 0.5f, 0.5f), Loc + FVector(0.5f, 0.5f, 0.5f));
							}
							CombinedBox.Min.X = FMath::Min(CombinedBox.Min.X, Box.Min.X);
							CombinedBox.Min.Y = FMath::Min(CombinedBox.Min.Y, Box.Min.Y);
							CombinedBox.Min.Z = FMath::Min(CombinedBox.Min.Z, Box.Min.Z);
							CombinedBox.Max.X = FMath::Max(CombinedBox.Max.X, Box.Max.X);
							CombinedBox.Max.Y = FMath::Max(CombinedBox.Max.Y, Box.Max.Y);
							CombinedBox.Max.Z = FMath::Max(CombinedBox.Max.Z, Box.Max.Z);
							bHasValid = true;
						}
						if (bHasValid)
						{
							const FVector Origin = (CombinedBox.Min + CombinedBox.Max) * 0.5f;
							const FVector Extent = (CombinedBox.Max - CombinedBox.Min) * 0.5f;
							Client->FocusOnBounds({ Origin, Extent, Extent.Size() }, true);
						}
					}
				}
			}
		}

		if (!PlayWorld && EditorManager->GetFocusedTab() == LevelEditorTab && !ImGui::GetIO().WantTextInput &&
			CapturedViewportIndex == -1 && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyShift &&
			FInputSystem::IsKeyPressed(EKeyCode::G) && EditorSettings)
		{
			ViewportsPanel->SetLocationSnapEnabled(!ViewportsPanel->IsLocationSnapEnabled());
		}

		// 활성 뷰포트 시점 동기화
		// SIE 중에는 PlayWorld의 MainCamera를 건드리지 않는다. 게임으로 돌아갔을 때 게임 카메라가 그대로 있어야 한다.
		const int32 ActiveIndex = ViewportsPanel->GetActiveViewIndex();
		if (FEditorViewportClient* ActiveClient = bSimulating ? nullptr : ViewportsPanel->GetViewportClient(ActiveIndex))
		{
			if (ACameraActor* MainCam = GetActiveWorld()->GetMainCamera())
			{
				if (UCameraComponent* CamCom = MainCam->GetCameraComponent())
				{
					CamCom->SetRelativeLocation(ActiveClient->GetViewLocation());
					CamCom->SetRelativeRotation(ActiveClient->GetViewRotation());
					CamCom->SetFieldOfView(ActiveClient->GetViewFOV());
					CamCom->SetOrthoWidth(ActiveClient->GetOrthoWidth());
					CamCom->SetNearZ(ActiveClient->GetNearClip());
					CamCom->SetFarZ(ActiveClient->GetFarClip());
					CamCom->SetIsOrthogonal(!ActiveClient->IsPerspective());
				}
			}
		}
	}

	// 월드를 Tick한다. PIE 중이면 게임 입력을 먼저 넘긴다. 이어서 에셋 에디터 Tick, 피킹과 기즈모 조작을 처리한다.
	// 게임 입력 처리
	if (PlayWorld)
	{
		// 툴바의 Eject/Possess는 UI를 그리는 도중에 눌리므로 여기서 전환한다.
		if (bTogglePIESIERequested)
			ToggleBetweenPIEandSIE();
		TickPIEInput(DeltaTime);
	}
	bTogglePIESIERequested = false;

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->SetPaused(false);
			GetActiveWorld()->Tick(1.0f / 60.0f);
			GetActiveWorld()->SetPaused(true);
			bStepRequested = false;
		}
		else if (!bIsPaused)
			GetActiveWorld()->Tick(DeltaTime);

	}
	if (PlayWorld && !bIsPaused)
	{
		FWorldContext* Context = GetWorldContextFromWorld(PlayWorld);
		if (Context && Context->GameViewport)
			Context->GameViewport->TickUI(DeltaTime);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorManager->Tick(DeltaTime);
	}
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
		PlayWorld->GetScene().UpdateAllTransforms();

	// 오디오 리스너 위치를 넘기고 오디오 장치를 갱신한다.
	if (FAudioDevice* AudioDevice = GetMainAudioDevice())
	{
		APlayerController* PC = PlayWorld ? PlayWorld->GetPlayerController() : nullptr;
		if (PC)
		{
			FVector Location, Front, Right;
			//PC->GetAudioListenerPosition(Location, Front, Right);
			AudioDevice->SetListener(PlayWorld, Location, Front, Right);
		}
		else
		{
			// 에디터 뷰포트 카메라: 활성 FEditorViewportClient의 GetViewLocation() / 회전으로 채운다.
			// (뷰포트 레이아웃 재구성 작업과 겹치므로, 그 작업에서 "활성 뷰포트 클라이언트" 접근자를 쓰는 게 깔끔하다)
		}
		AudioDevice->Update(!bIsPaused);
	}

	// 뷰포트를 그린다. UE의 UEditorEngine::Tick도 이 자리에서 뷰포트를 돌며 UpdateSingleViewportClient를 부른다.
	// 먼저 그림자 미리보기 상태를 지우고, 이번 프레임에 미리 볼 라이트와 PIE 화면의 표시 설정을 넘겨 둔다.
	const bool bIsPIE = (PlayWorld != nullptr);
	const ULightComponent* PreviewLight = DetailsPanel ? DetailsPanel->GetSelectedLight() : nullptr;
	// Invalidate skipped views too: never expose a previous frame's atlas range.
	for (FEditorViewportClient* Client : AllViewportClients)
	{
		if (!Client) continue;
		Client->GetRenderData().PreviewLight = PreviewLight;
		Client->GetRenderData().PreviewShadowRange = {};
	}
	if (bIsPIE)
	{
		if (FWorldContext* Context = GetPIEWorldContext())
		{
			if (Context->GameViewport)
			{
				Context->GameViewport->GetRenderData().PreviewLight = PreviewLight;
				Context->GameViewport->GetRenderData().PreviewShadowRange = {};

				// PIE 게임 화면은 에디터 설정 창의 값으로 그린다. 게임 뷰포트는 에디터를 모르므로 여기서 넘겨 준다.
				if (EditorSettings)
				{
					const FEditorSettings& S = EditorSettings->GetSettings();
					FSceneRenderOptions& ShowFlags = Context->GameViewport->EngineShowFlags;
					ShowFlags.bEnableFXAA = S.bEnableFXAA;
					ShowFlags.bCacheShadows = S.bCacheShadows;
					ShowFlags.PcfMode = S.PcfMode;
					ShowFlags.LightingModel = S.LightingModel;
					ShowFlags.LightCullingMode = S.LightCullingMode;
					ShowFlags.DirectionalShadowMapSize = S.GetDirectionalShadowMapSize();
					ShowFlags.PointShadowMapSize = S.GetPointShadowMapSize();
					ShowFlags.SpotShadowMapSize = S.GetSpotShadowMapSize();
					ShowFlags.bGammaCorrection = S.bGammaCorrection;
					ShowFlags.DisplayGamma = S.DisplayGamma;
					ShowFlags.bShowVignette = S.bShowVignette;
					ShowFlags.bShowCameraOverlay = S.bShowCameraOverlay;
					ShowFlags.bShowSkeletalMesh = S.bShowSkeletalMesh;
					ShowFlags.bShowParticles = S.bShowParticles;
				}
			}
		}
	}

	// 지금 편집 중인 뷰포트를 전역에 알려 둔다. 선택 외곽선과 기즈모는 그 뷰포트의 Draw만 그린다.
	GCurrentLevelEditingViewportClient = ViewportsPanel
		? ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex())
		: nullptr;

	// View마다 UpdateSingleViewportClient를 불러 그린다.
	for (int32 ViewIndex = 0; ViewIndex < AllViewportClients.Num(); ++ViewIndex)
	{
		UpdateSingleViewportClient(AllViewportClients[ViewIndex], ViewIndex);
	}

	if (!PendingThumbnailScene.empty()) SaveProjectThumbnail();

	// 별도 창(FWindow) 모드인 경우 PIE 창 타깃에 렌더링하고 스왑체인 백버퍼로 복사한다.
	bPIEWindowRendered = false;
	if (bIsPIE && IsPIEInWindow() && UpdatePIEWindowTargets())
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (Context && Context->GameViewport && Context->World())
		{
			FGameViewportClient* GameClient = Context->GameViewport.get();
			const uint32 Width = GameClient->GetWidth();
			const uint32 Height = GameClient->GetHeight();
			if (Width > 0 && Height > 0)
			{
				GameClient->Draw(Renderer, UIRenderer.get());

				CopyPIEWindowToBackbuffer();
			}
		}
	}

	// 열려 있는 에셋 에디터의 미리보기를 그린다.
	EditorManager->RenderPreview();
	if (ContentDrawerPanel)
	{
		ContentDrawerPanel->RenderPendingThumbnails(*this, Renderer);
	}

	// 네 View의 결과 텍스처와 ImGui를 메인 창의 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetBackbuffer(), nullptr,
		MainWindow ? MainWindow->GetWidth() : 0,
		MainWindow ? MainWindow->GetHeight() : 0);

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorManager->DrawEditorUI();
		// NewFrame과 패널 배치 이후의 입력/좌표를 사용해야 클릭과 기즈모 피킹이 일치한다.
		UpdateGizmoAndPicking();

		const ImGuiIO& IO = ImGui::GetIO();
		if (!PlayWorld && EditorManager && EditorManager->GetFocusedTab() == LevelEditorTab && !IO.WantTextInput)
		{
			if (IO.KeyCtrl && !IO.KeyAlt)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
				{
					if (IO.KeyShift)
						Redo();
					else
						Undo();
				}
				else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
				{
					Redo();
				}
			}
		}

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass();

	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	EditorSettings->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();

	EditorManager->LateTick();
}


void UEditorEngine::PreLoadMap(FWorldContext& Context)
{
	if (Context.WorldType == EWorldType::PIE)
	{
		ResetSceneSelection();
		// 월드가 바뀌면 SIE를 끝내고 게임 화면으로 돌아간다.
		if (bIsSimulatingInEditor)
			ToggleBetweenPIEandSIE();
	}
}

void UEditorEngine::PostLoadMap(FWorldContext& Context)
{
	if (Context.WorldType != EWorldType::PIE) return;
	// GameViewport 객체는 그대로라 ViewportsPanel 연결은 다시 하지 않아도 된다.
	PlayWorld = Context.World();
	OnActiveWorldChanged();
}

// View 하나를 그린다. PIE를 시작한 View는 게임 뷰포트로, 나머지는 에디터 뷰포트로 그린다.
// UE의 UEditorEngine::UpdateSingleViewportClient와 같은 자리다. UE처럼 어느 쪽이든 뷰포트 클라이언트의 Draw가 그린다.
void UEditorEngine::UpdateSingleViewportClient(FEditorViewportClient* ViewClient, int32 ViewIndex)
{
	const bool bIsPIE = (PlayWorld != nullptr);
	if (!ViewClient || !ViewClient->IsActive())
		return;

	const FRect& ViewRect = ViewClient->GetRect();
	if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
		return;

	bool bIsGameView = false;
	int32 TargetPlayerIndex = -1;

	// SIE에서는 이 View도 에디터 뷰포트로 그린다. UE도 Simulate 화면은 레벨 에디터 뷰포트 클라이언트가 그린다.
	// 별도 창을 만들지 못했거나 Simulate로 시작한 경우에도 View 안에서 돌므로 PlayMode 대신 실제 창 유무로 판단한다.
	if (bIsPIE && !bIsSimulatingInEditor && !IsPIEInWindow() &&
		ViewIndex == PIEStartEditorViewIndex)
	{
		bIsGameView = true;
		TargetPlayerIndex = 0;
	}

	if (bIsGameView && TargetPlayerIndex >= 0)
	{
		FGameViewportClient* GameClient = nullptr;
		for (int32 WorldIdx = 0; WorldIdx < WorldContextlist.Num(); ++WorldIdx)
		{
			if (WorldContextlist[WorldIdx].WorldType == EWorldType::PIE &&
				WorldContextlist[WorldIdx].PIEInstance == TargetPlayerIndex &&
				WorldContextlist[WorldIdx].GameViewport)
			{
				GameClient = WorldContextlist[WorldIdx].GameViewport.get();
				// 게임 뷰포트의 타깃 크기를 이 View의 영역에 맞춘 뒤 그린다.
				GameClient->Resize(
					static_cast<uint32>(std::max(ViewRect.Width, 1.0f)),
					static_cast<uint32>(std::max(ViewRect.Height, 1.0f)));
				GameClient->Draw(Renderer, UIRenderer.get());

				break;
			}
		}
	}
	else
	{
		ViewClient->Draw(Renderer);
	}
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldContextlist.Num(); ++i)
	{
		if (WorldContextlist[i].WorldType == EWorldType::Editor)
		{
			return WorldContextlist[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldContextlist)
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.PIEInstance == WorldPIEInstance)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}



void UEditorEngine::UpdateGizmoAndPicking()
{
	const int32 ViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	if (ViewIndex < 0 || ViewIndex >= 4 || !ViewportsPanel)
	{
		if (bTrackingGizmoDrag)
		{
			if (Gizmo) Gizmo->EndDrag();
			FinishGizmoDragUndo();
		}
		return;
	}
	if (!ViewportsPanel->WasRenderedThisFrame())
	{
		if (Gizmo) Gizmo->EndDrag();
		FinishGizmoDragUndo();
		return;
	}
	if (!ViewportsPanel->IsHovered() && !Gizmo->IsUsing())
	{
		if (bTrackingGizmoDrag && !Gizmo->IsUsing())
			FinishGizmoDragUndo();
		return;
	}
	if (IsPlayingView(ViewIndex))
	{
		if (bTrackingGizmoDrag && !Gizmo->IsUsing())
			FinishGizmoDragUndo();
		return;
	}

	FEditorViewportClient* ViewClient = ViewportsPanel->GetViewportClient(ViewIndex);
	if (!ViewClient || !ViewClient->IsActive())
	{
		if (bTrackingGizmoDrag && !Gizmo->IsUsing())
			FinishGizmoDragUndo();
		return;
	}

	USceneComponent* PreDragTarget = (!bTrackingGizmoDrag && Gizmo) ? Gizmo->GetTarget() : nullptr;
	const bool bWasUsingGizmo = Gizmo && Gizmo->IsUsing();
	TArray<FGizmoDragEntry> CandidateEntries;
	FVector PreDragPrimaryWorldLoc = FVector(0, 0, 0);
	FQuat PreDragPrimaryWorldQuat = FQuat::Identity();
	FVector PreDragPrimaryScale = FVector(1, 1, 1);
	if (PreDragTarget && PreDragTarget->GetOwner())
	{
		PreDragPrimaryWorldLoc = PreDragTarget->GetWorldLocation();
		PreDragPrimaryWorldQuat = PreDragTarget->GetWorldRotation().Quaternion();
		PreDragPrimaryScale = PreDragTarget->GetRelativeScale3D();

		FGizmoDragEntry PrimaryEntry;
		PrimaryEntry.ActorName = PreDragTarget->GetOwner()->GetFName();
		PrimaryEntry.CompName = PreDragTarget->GetFName();
		PrimaryEntry.StartTransform = PreDragTarget->GetTransform();
		PrimaryEntry.StartWorldLocation = PreDragPrimaryWorldLoc;
		PrimaryEntry.StartWorldQuat = PreDragPrimaryWorldQuat;
		PrimaryEntry.bIsPrimary = true;
		if (ULocalLightComponent* Light = Cast<ULocalLightComponent>(PreDragTarget))
		{
			PrimaryEntry.bHasLightRadius = true;
			PrimaryEntry.StartLightRadius = Light->GetAttenuationRadius();
		}
		CandidateEntries.Add(PrimaryEntry);

		if (OutlinerPanel && OutlinerPanel->GetSelectedActors().Num() > 1)
		{
			const TArray<AActor*>& Selected = OutlinerPanel->GetSelectedActors();
			auto HasSelectedAncestor = [&Selected](AActor* Actor) -> bool
			{
				for (AActor* Parent = EditorHierarchy::GetParentActor(Actor); Parent; Parent = EditorHierarchy::GetParentActor(Parent))
				{
					if (Selected.Find(Parent) != INDEX_NONE)
					{
						return true;
					}
				}
				return false;
			};

			for (AActor* SiblingActor : Selected)
			{
				if (!SiblingActor || SiblingActor == PreDragTarget->GetOwner() || HasSelectedAncestor(SiblingActor))
				{
					continue;
				}
				USceneComponent* SiblingRoot = SiblingActor->GetRootComponent();
				if (!SiblingRoot)
				{
					continue;
				}

				FGizmoDragEntry Entry;
				Entry.ActorName = SiblingActor->GetFName();
				Entry.CompName = SiblingRoot->GetFName();
				Entry.StartTransform = SiblingRoot->GetTransform();
				Entry.StartWorldLocation = SiblingRoot->GetWorldLocation();
				Entry.StartWorldQuat = SiblingRoot->GetWorldRotation().Quaternion();
				Entry.bIsPrimary = false;
				if (ULocalLightComponent* Light = Cast<ULocalLightComponent>(SiblingRoot))
				{
					Entry.bHasLightRadius = true;
					Entry.StartLightRadius = Light->GetAttenuationRadius();
				}
				CandidateEntries.Add(Entry);
			}
		}
	}

	if (Gizmo && EditorSettings)
	{
		const FEditorSettings& Settings = EditorSettings->GetSettings();
		Gizmo->SetLocationSnap(Settings.bLocationSnap, Settings.LocationSnapSize);
	}

	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	ViewClient->UpdateGizmoAndPicking(Gizmo.get(), LocalMousePosition, [this](AActor* SelectedActor, bool bCtrlToggle) {
		OutlinerPanel->SelectActor(SelectedActor, bCtrlToggle);
		});

	const bool bIsUsingNow = Gizmo && Gizmo->IsUsing();
	if (!bTrackingGizmoDrag && !bWasUsingGizmo && bIsUsingNow && !CandidateEntries.IsEmpty() && !IsPlaying())
	{
		bTrackingGizmoDrag = true;
		GizmoDragMode = Gizmo->GetMode();
		GizmoDragPrimaryStartWorldLoc = PreDragPrimaryWorldLoc;
		GizmoDragPrimaryStartWorldQuat = PreDragPrimaryWorldQuat;
		GizmoDragPrimaryStartScale = PreDragPrimaryScale;
		GizmoDragEntries = std::move(CandidateEntries);
	}

	if (bTrackingGizmoDrag && bIsUsingNow)
	{
		ApplyMultiActorGizmoDelta();
	}
	else if (bTrackingGizmoDrag && !bIsUsingNow)
	{
		FinishGizmoDragUndo();
	}
}

void UEditorEngine::ApplyMultiActorGizmoDelta()
{
	if (!bTrackingGizmoDrag || GizmoDragEntries.Num() <= 1 || !Gizmo || !EditorWorld)
	{
		return;
	}

	USceneComponent* Primary = Gizmo->GetTarget();
	if (!Primary)
	{
		return;
	}

	const FVector PrimaryWorldLoc = Primary->GetWorldLocation();
	const FQuat PrimaryWorldQuat = Primary->GetWorldRotation().Quaternion();
	const FVector PrimaryScale = Primary->GetRelativeScale3D();

	const FVector DeltaLoc = PrimaryWorldLoc - GizmoDragPrimaryStartWorldLoc;
	const FQuat DeltaQuat = PrimaryWorldQuat * GizmoDragPrimaryStartWorldQuat.Inverse();
	auto CalcAxisScale = [](float EntryStart, float PrimaryStart, float PrimaryNow) -> float
	{
		if (std::abs(PrimaryStart) > 1e-4f)
		{
			return EntryStart * (PrimaryNow / PrimaryStart);
		}
		return EntryStart + (PrimaryNow - PrimaryStart);
	};

	for (const FGizmoDragEntry& Entry : GizmoDragEntries)
	{
		if (Entry.bIsPrimary)
		{
			continue;
		}
		USceneComponent* Sibling = Cast<USceneComponent>(
			EditorUndoUtils::FindComponentByNames(EditorWorld, Entry.ActorName, Entry.CompName));
		if (!Sibling)
		{
			continue;
		}

		if (GizmoDragMode == EGizmoMode::Location)
		{
			Sibling->SetWorldLocation(Entry.StartWorldLocation + DeltaLoc, false);
		}
		else if (GizmoDragMode == EGizmoMode::Rotation)
		{
			const FVector Offset = Entry.StartWorldLocation - GizmoDragPrimaryStartWorldLoc;
			const FVector NewLoc = GizmoDragPrimaryStartWorldLoc + DeltaQuat.RotateVector(Offset);
			const FRotator NewRot = (DeltaQuat * Entry.StartWorldQuat).ToFRotator();
			Sibling->SetWorldLocationAndRotation(NewLoc, NewRot, false);
		}
		else if (GizmoDragMode == EGizmoMode::Scale)
		{
			FVector NewScale(
				CalcAxisScale(Entry.StartTransform.Scale.X, GizmoDragPrimaryStartScale.X, PrimaryScale.X),
				CalcAxisScale(Entry.StartTransform.Scale.Y, GizmoDragPrimaryStartScale.Y, PrimaryScale.Y),
				CalcAxisScale(Entry.StartTransform.Scale.Z, GizmoDragPrimaryStartScale.Z, PrimaryScale.Z));
			Sibling->SetRelativeScale3D(NewScale);
		}
	}
}

void UEditorEngine::FinishGizmoDragUndo()
{
	if (!bTrackingGizmoDrag)
	{
		return;
	}
	bTrackingGizmoDrag = false;
	TArray<FGizmoDragEntry> Entries = std::move(GizmoDragEntries);
	GizmoDragEntries.Reset();

	if (IsPlaying() || !UndoHistory.CanRecord() || !EditorWorld)
	{
		return;
	}

	TArray<TUniquePtr<IEditorUndoCommand>> Commands;
	TArray<FName> SelectedNames;
	for (const FGizmoDragEntry& Entry : Entries)
	{
		USceneComponent* Target = Cast<USceneComponent>(
			EditorUndoUtils::FindComponentByNames(EditorWorld, Entry.ActorName, Entry.CompName));
		if (!Target || !Target->GetOwner())
		{
			continue;
		}

		const FTransform& NewTransform = Target->GetTransform();
		const bool bTransformChanged =
			Entry.StartTransform.Location.X != NewTransform.Location.X ||
			Entry.StartTransform.Location.Y != NewTransform.Location.Y ||
			Entry.StartTransform.Location.Z != NewTransform.Location.Z ||
			Entry.StartTransform.Scale.X != NewTransform.Scale.X ||
			Entry.StartTransform.Scale.Y != NewTransform.Scale.Y ||
			Entry.StartTransform.Scale.Z != NewTransform.Scale.Z ||
			Entry.StartTransform.Rotation.X != NewTransform.Rotation.X ||
			Entry.StartTransform.Rotation.Y != NewTransform.Rotation.Y ||
			Entry.StartTransform.Rotation.Z != NewTransform.Rotation.Z ||
			Entry.StartTransform.Rotation.W != NewTransform.Rotation.W;

		float NewLightRadius = Entry.StartLightRadius;
		bool bRadiusChanged = false;
		if (Entry.bHasLightRadius)
		{
			if (ULocalLightComponent* Light = Cast<ULocalLightComponent>(Target))
			{
				NewLightRadius = Light->GetAttenuationRadius();
				bRadiusChanged = (NewLightRadius != Entry.StartLightRadius);
			}
		}

		if (bTransformChanged || bRadiusChanged)
		{
			SelectedNames.Add(Entry.ActorName);
			Commands.Add(EditorUndoUtils::MakeTransformCommand(
				Target,
				Entry.StartTransform,
				NewTransform,
				Entry.bHasLightRadius,
				Entry.StartLightRadius,
				NewLightRadius));
		}
	}

	if (Commands.Num() == 1)
	{
		UndoHistory.Push(std::move(Commands[0]));
	}
	else if (Commands.Num() > 1)
	{
		UndoHistory.Push(EditorUndoUtils::MakeCompositeCommand("Transform Actors", std::move(Commands), SelectedNames, SelectedNames));
	}
}

//void UEditorEngine::RenderSceneText(UWorld* TargetWorld, const FSceneView& SceneView)
//{
//	if (!TextRenderer) return;
//	for (TObjectIterator<UTextRenderComponent> Text; Text; ++Text)
//	{
//		if (!Text || !Text->IsRegistered() || !Text->GetFont() || !Text->IsShown(SceneView.bGameView) || !Text->GetOwner() ||
//			Text->GetOwner()->GetWorld() != TargetWorld)
//			continue;
//		TextRenderer->OnRender(Text->GetText(), Text->GetWorldMatrix(), Text->GetTextSize(),
//			*Text->GetFont(), SceneView.ViewProjectionMatrix);
//	}
//}
//
//bool UEditorEngine::RenderHeightFog(UWorld* TargetWorld, const FSceneView& SceneView,
//	FTexture2D* DepthTarget, FTexture2D* ColorTarget)
//{
//	if (!TargetWorld || !HeightFogRenderer || !DepthTarget || !ColorTarget ||
//		!SceneView.bIsPerspective)
//		return false;
//
//	for (TObjectIterator<UExponentialHeightFogComponent> Fog; Fog; ++Fog)
//	{
//		if (!Fog || !Fog->IsRegistered() || !Fog->IsShown(SceneView.bGameView) || !Fog->GetOwner() ||
//			Fog->GetOwner()->GetWorld() != TargetWorld)
//			continue;
//
//		FHeightFogSetting Setting;
//		Setting.FogDensity = Fog->GetFogDensity();
//		Setting.FogHeightFalloff = Fog->GetFogHeightFalloff();
//		Setting.FogHeight = Fog->GetFogHeight();
//		Setting.FogColor = Fog->GetFogColor();
//		Setting.StartDistance = Fog->GetStartDistance();
//		Setting.FogMaxOpacity = Fog->GetFogMaxOpacity();
//		Setting.FogCutoffDistance = Fog->GetFogCutoffDistance();
//		if (HeightFogRenderer->OnRender(SceneView, DepthTarget, ColorTarget, Setting))
//			return true;
//	}
//	return false;
//}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	UWorld::SetOnActorDestroyedCallback(nullptr);
	EndViewportNavCapture();
	bTrackingGizmoDrag = false;
	UndoHistory.Clear();
	ActorClipboard.Reset();
	EndPlayMap();
	// 시퀀서는 에디터 월드 액터를 가리키므로 월드보다 먼저 정리한다
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();

	// 레벨 패널을 마지막으로 사용한 뒤 탭을 파괴하고 빌린 참조를 비운다.
	FLog::RemoveSink(OutputLogPanel);
	if (ContentDrawerPanel)
	{
		ContentDrawerPanel->Shutdown();
	}
	if (EditorManager)
	{
		EditorManager->Shutdown();
	}

	LevelEditorTab = nullptr;
	OutputLogPanel = nullptr;
	DetailsPanel = nullptr;
	if (EditorSettings)
	{
		EditorSettings->SetViewportsPanel(nullptr);
		EditorSettings.reset();
	}
	ViewportsPanel = nullptr;
	ContentDrawerPanel = nullptr;
	OutlinerPanel = nullptr;
	AllViewportClients.Empty();

	ImGuiRenderer->Shutdown();
	GEditor = nullptr;
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::Undo()
{
	if (IsPlaying())
		return;
	if (bTrackingGizmoDrag)
	{
		if (Gizmo) Gizmo->EndDrag();
		FinishGizmoDragUndo();
	}
	UndoHistory.Undo(*this);
}

void UEditorEngine::Redo()
{
	if (IsPlaying())
		return;
	if (bTrackingGizmoDrag)
	{
		if (Gizmo) Gizmo->EndDrag();
		FinishGizmoDragUndo();
	}
	UndoHistory.Redo(*this);
}

void UEditorEngine::SelectActorForUndo(AActor* Actor, UActorComponent* Component)
{
	bTrackingGizmoDrag = false;
	GizmoDragEntries.Reset();
	if (!Actor)
	{
		if (Gizmo) Gizmo->SetTarget(nullptr);
		if (Outline) Outline->SetTarget(nullptr);
		if (DetailsPanel) DetailsPanel->SelectComponent(nullptr);
		if (OutlinerPanel) OutlinerPanel->SelectActor(nullptr);
		return;
	}

	if (OutlinerPanel)
	{
		OutlinerPanel->SelectActor(Actor);
	}
	if (Component && DetailsPanel)
	{
		DetailsPanel->SelectComponent(Component);
	}
}

void UEditorEngine::SelectActorsForUndo(const TArray<AActor*>& Actors)
{
	bTrackingGizmoDrag = false;
	GizmoDragEntries.Reset();
	if (OutlinerPanel)
	{
		OutlinerPanel->SelectActors(Actors);
	}
}

void UEditorEngine::DeleteActorForUndo(AActor* Actor)
{
	if (!Actor)
		return;

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponentForUndo(UActorComponent* Component)
{
	if (!Component)
		return;

	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Component->GetOwner()->DestroyComponent(Component);
}

void UEditorEngine::DeleteActors(const TArray<AActor*>& Actors)
{
	if (Actors.IsEmpty())
		return;
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;

	TArray<AActor*> ToDelete = Actors;
	if (ToDelete.Num() == 1)
	{
		DeleteActor(ToDelete[0]);
		return;
	}

	const bool bCanRecord = !IsPlaying() && UndoHistory.CanRecord();
	TArray<TUniquePtr<IEditorUndoCommand>> Cmds;
	TArray<FName> DeletedNames;
	for (AActor* Actor : ToDelete)
	{
		if (!Actor) continue;
		if (bCanRecord)
		{
			FActorUndoSnapshot Snapshot;
			if (EditorUndoUtils::CaptureActorSnapshot(Actor, Snapshot))
			{
				DeletedNames.Add(Actor->GetFName());
				Cmds.Add(EditorUndoUtils::MakeActorCreateDeleteCommand("Delete Actor", std::move(Snapshot), false));
			}
		}
	}

	for (AActor* Actor : ToDelete)
	{
		if (Actor)
		{
			DeleteActorForUndo(Actor);
		}
	}

	if (Cmds.Num() == 1)
	{
		UndoHistory.Push(std::move(Cmds[0]));
	}
	else if (Cmds.Num() > 1)
	{
		UndoHistory.Push(EditorUndoUtils::MakeCompositeCommand("Delete Actors", std::move(Cmds), DeletedNames, {}));
	}
}

void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
		return;
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;

	FActorUndoSnapshot Snapshot;
	const bool bRecordUndo = !IsPlaying() && UndoHistory.CanRecord()
		&& EditorUndoUtils::CaptureActorSnapshot(Actor, Snapshot);

	DeleteActorForUndo(Actor);

	if (bRecordUndo)
	{
		UndoHistory.Push(EditorUndoUtils::MakeActorCreateDeleteCommand("Delete Actor", std::move(Snapshot), false));
	}
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
		return;
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;

	AActor* Owner = Component->GetOwner();
	FName ActorName;
	FName CompName;
	FString ClassName;
	FName ParentCompName;
	TArray<FName> ChildCompNames;
	TArray<uint8> CompBytes;
	bool bRecordUndo = false;
	if (!IsPlaying() && UndoHistory.CanRecord() && Owner && Component != Owner->GetRootComponent())
	{
		ActorName = Owner->GetFName();
		CompName = Component->GetFName();
		ClassName = Component->GetClass() ? Component->GetClass()->Name : "";
		bRecordUndo = EditorUndoUtils::CaptureObjectBytes(Component, CompBytes);
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
		{
			USceneComponent* Parent = SceneComp->GetAttachParent();
			ParentCompName = (Parent && Parent->GetOwner() == Owner) ? Parent->GetFName() : FName();
			for (USceneComponent* Child : SceneComp->GetAttachChildren())
			{
				if (Child && Child->GetOwner() == Owner)
				{
					ChildCompNames.Add(Child->GetFName());
				}
			}
		}
	}

	DeleteComponentForUndo(Component);

	if (bRecordUndo)
	{
		UndoHistory.Push(EditorUndoUtils::MakeComponentAddRemoveCommand(
			"Delete Component",
			ActorName,
			CompName,
			std::move(ClassName),
			ParentCompName,
			std::move(ChildCompNames),
			std::move(CompBytes),
			false));
	}
}

void UEditorEngine::OnActiveWorldChanged()
{
	bTrackingGizmoDrag = false;
	UndoHistory.SetRecordingEnabled(!PlayWorld);
	ResetSceneSelection();
	UWorld* ActiveWorld = GetActiveWorld();

	// 피킹 대상 월드도 렌더링 대상 월드에 맞춘다.
	if (ViewportsPanel)
	{
		for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
		{
			FEditorViewportClient* Client =
				ViewportsPanel->GetViewportClient(ViewIndex);

			if (Client)
			{
				Client->SetWorld(ActiveWorld);
			}
		}
	}

	if (OutlinerPanel) OutlinerPanel->SetWorld(ActiveWorld);
	if (DetailsPanel) DetailsPanel->SetWorld(ActiveWorld);
	if (ViewportsPanel) ViewportsPanel->SetWorld(ActiveWorld);
}

// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::CopySelectedActor()
{
	if (PlayWorld || !OutlinerPanel) return;
	TArray<AActor*> SourceActors;
	for (AActor* Selected : OutlinerPanel->GetSelectedActors())
	{
		if (Selected && SourceActors.Find(Selected) == INDEX_NONE)
		{
			SourceActors.Add(Selected);
		}
	}
	if (SourceActors.IsEmpty()) return;

	ActorClipboard.Reset();
	TArray<AActor*> CopiedSourceActors;
	for (AActor* Actor : SourceActors)
	{
		FObjectDuplicationParameters Params(Actor, nullptr);
		Params.DestName = MakeUniqueObjectName(nullptr, Actor->GetClass(), Actor->GetFName());
		TUniquePtr<AActor> Copy(Cast<AActor>(StaticDuplicateObjectEx(Params)));
		if (!Copy) continue;

		// Preserve each actor's world transform cleanly without keeping dangling attach pointers to live world actors.
		if (USceneComponent* SrcRoot = Actor->GetRootComponent())
		{
			if (USceneComponent* CopyRoot = Copy->GetRootComponent())
			{
				if (CopyRoot->GetAttachParent())
				{
					CopyRoot->DetachFromParent(EAttachmentRule::KeepRelative);
				}
				CopyRoot->SetRelativeLocation(SrcRoot->GetWorldLocation());
				CopyRoot->SetRelativeRotation(SrcRoot->GetWorldRotation());
				CopyRoot->SetRelativeScale3D(SrcRoot->GetWorldScale3D());
			}
		}

		FClipboardActorEntry Entry;
		Entry.Actor = std::move(Copy);
		ActorClipboard.Add(std::move(Entry));
		CopiedSourceActors.Add(Actor);
	}

	for (int32 i = 0; i < ActorClipboard.Num(); ++i)
	{
		if (AActor* ParentActor = EditorHierarchy::GetParentActor(CopiedSourceActors[i]))
		{
			ActorClipboard[i].ParentIndex = CopiedSourceActors.Find(ParentActor);
		}
	}
}

void UEditorEngine::PasteActor()
{
	if (PlayWorld || ActorClipboard.IsEmpty() || !EditorWorld || !OutlinerPanel) return;

	TArray<AActor*> PastedActors;
	PastedActors.Reserve(ActorClipboard.Num());
	for (const FClipboardActorEntry& Entry : ActorClipboard)
	{
		PastedActors.Add(Entry.Actor ? EditorWorld->DuplicateActor(Entry.Actor.get()) : nullptr);
	}

	// Restore parent-child hierarchy for actors whose parent was also in the copied selection.
	for (int32 i = 0; i < PastedActors.Num(); ++i)
	{
		const int32 ParentIdx = ActorClipboard[i].ParentIndex;
		if (ParentIdx != INDEX_NONE && ParentIdx < PastedActors.Num() && PastedActors[i] && PastedActors[ParentIdx])
		{
			EditorHierarchy::Attach(PastedActors[i]->GetRootComponent(), PastedActors[ParentIdx]->GetRootComponent());
		}
	}

	TArray<AActor*> ValidPasted;
	for (AActor* Pasted : PastedActors)
	{
		if (Pasted) ValidPasted.Add(Pasted);
	}
	if (ValidPasted.IsEmpty()) return;

	OutlinerPanel->SelectActors(ValidPasted);

	if (UndoHistory.CanRecord())
	{
		TArray<TUniquePtr<IEditorUndoCommand>> Cmds;
		TArray<FName> PastedNames;
		for (AActor* Pasted : ValidPasted)
		{
			FActorUndoSnapshot Snapshot;
			if (EditorUndoUtils::CaptureActorSnapshot(Pasted, Snapshot))
			{
				PastedNames.Add(Pasted->GetFName());
				Cmds.Add(EditorUndoUtils::MakeActorCreateDeleteCommand("Paste Actor", std::move(Snapshot), true));
			}
		}
		if (Cmds.Num() == 1)
		{
			UndoHistory.Push(std::move(Cmds[0]));
		}
		else if (Cmds.Num() > 1)
		{
			UndoHistory.Push(EditorUndoUtils::MakeCompositeCommand("Paste Actors", std::move(Cmds), {}, PastedNames));
		}
	}
}

void UEditorEngine::ResetSceneSelection()
{
	ActorClipboard.Reset();
	Gizmo->SetTarget(nullptr, false);
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		KRS_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::NewScene(EditorWorld))
		return;

	bTrackingGizmoDrag = false;
	UndoHistory.Clear();
	ResetSceneSelection();
	LevelEditorTab->OnLevelLoaded.Broadcast(FEditorFileUtils::CurrentScenePath);
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		KRS_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld))
		return;

	bTrackingGizmoDrag = false;
	UndoHistory.Clear();
	ResetSceneSelection();

	LevelEditorTab->OnLevelLoaded.Broadcast(FEditorFileUtils::CurrentScenePath);
	QueueProjectThumbnail(true);
}

// 콘텐츠 드로어에서 더블클릭한 씬 파일을 바로 연다.
void UEditorEngine::OpenSceneFromPath(const FString& Path)
{
	if (IsPlaying())
	{
		KRS_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld, Path))
		return;

	bTrackingGizmoDrag = false;
	UndoHistory.Clear();
	ResetSceneSelection();

	LevelEditorTab->OnLevelLoaded.Broadcast(FEditorFileUtils::CurrentScenePath);
	QueueProjectThumbnail(true);
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		KRS_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	if (FEditorFileUtils::SaveScene(EditorWorld)) QueueProjectThumbnail();
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		KRS_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	if (FEditorFileUtils::SaveSceneAs(EditorWorld)) QueueProjectThumbnail();
}

void UEditorEngine::QueueProjectThumbnail(bool bOnlyIfMissing)
{
	if (!FPaths::HasProject()) return;
	std::error_code Ec;
	if (bOnlyIfMissing && fs::exists(FPaths::ProjectDir() / FPaths::ProjectThumbnailRelativePath, Ec)) return;
	const FGameSettings Settings = FGameSettings::Load(FPaths::ToUtf8(FPaths::ProjectDir() / L"Config/DefaultGame.ini"));
	const fs::path Startup = FPaths::ProjectDir() / FPaths::FromUtf8(Settings.StartupMap);
	if (fs::equivalent(FPaths::FromUtf8(FEditorFileUtils::CurrentScenePath), Startup, Ec))
		PendingThumbnailScene = FEditorFileUtils::CurrentScenePath;
}

void UEditorEngine::SaveProjectThumbnail()
{
	const FString Scene = PendingThumbnailScene;
	PendingThumbnailScene.clear();
	if (IsPlaying() || Scene != FEditorFileUtils::CurrentScenePath || !ViewportsPanel) return;
	FEditorViewportClient* View = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex());
	if (!View || !View->IsActive())
	{
		PendingThumbnailScene = Scene; // 첫 UI 배치 전이면 활성 뷰포트가 생긴 뒤 다시 시도한다.
		return;
	}
	const FGameSettings Settings = FGameSettings::Load(FPaths::ToUtf8(FPaths::ProjectDir() / L"Config/DefaultGame.ini"));
	FProjectThumbnailViewport Capture(Settings.RenderOptions);
	Capture.SetWorld(EditorWorld);
	Capture.SetViewportType(View->GetViewportType());
	Capture.SetViewLocation(View->GetViewLocation());
	Capture.SetViewRotation(View->GetViewRotation());
	Capture.SetViewFOV(View->GetViewFOV());
	Capture.SetClipPlanes(View->GetNearClip(), View->GetFarClip());
	Capture.Resize(444, 396); // 카드의 148:132 비율, 3배 해상도
	Capture.SetOrthoWidth(View->GetOrthoWidth());
	if (!Capture.GetViewTargets().IsValidFor(444, 396))
	{
		KRS_LOG(Warning, "Cannot create project thumbnail render targets.");
		return;
	}
	Capture.Draw(Renderer);
	const fs::path Path = FPaths::ProjectDir() / FPaths::ProjectThumbnailRelativePath;
	if (WriteThumbnailPNG(Capture.GetColorTarget(), Path))
		KRS_LOG(Info, "Project thumbnail saved: {}", FPaths::ToUtf8(Path));
	else
		KRS_LOG(Warning, "Failed to save project thumbnail: {}", FPaths::ToUtf8(Path));
}

// PIE 디버그 표시 (Settings > Debug Draw (PIE))
void UEditorEngine::DrawPIEDebug(UWorld* World, const FMatrix& ViewProjection)
{
	const FEditorSettings& Settings = EditorSettings->GetSettings();
	GShowSkeletalRootDebug = Settings.bShowRootDebug;  // 런타임 "root" 라벨 표시용

	// 충돌 도형·스프링 암 같은 배치 라인은 에디터 뷰와 SIE에서만 그린다. 게임 화면에는 올리지 않는다.
	FLineBatcher* LineBatcher = World->LineBatcher.get();

	// root는 바닥 높이라 가려지기 쉬우므로 깊이 테스트 없이 따로 그린다 (항상 위에 보임)
	if (Settings.bShowRootDebug)
	{
		LineBatcher->BeginFrame();
		DrawSkeletalRootDebug(World, LineBatcher);
		LineBatcher->SetDepthTest(false);
		LineBatcher->OnRender(ViewProjection);
		LineBatcher->SetDepthTest(true);
	}
}
