#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/EditorUI/ImGuiRenderer.h"

#include "Core/Window.h"
#include "Input/InputSystem.h"
#include "Core/Misc/Paths.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <d3dcompiler.h>
#include <cstring>
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
// imgui_impl_dx11.cpp에 정의만 있고 헤더에는 아직 선언이 없다.
void ImGui_ImplDX11_SetSwapChainDescs(const DXGI_SWAP_CHAIN_DESC* desc_templates, int desc_templates_count);

namespace
{
	FString GImGuiSettingsPath;

	void MigrateEditorSettings(const std::filesystem::path& ConfigDir)
	{
		for (const wchar_t* Filename : { L"editor.ini", L"imgui.ini" })
		{
			std::error_code Error;
			const auto Target = ConfigDir / Filename;
			if (std::filesystem::exists(Target, Error)) continue;
			for (const auto& OldDir : { FPaths::ProjectSavedDir() / "Editor", FPaths::ProjectDir() })
			{
				const auto Source = OldDir / Filename;
				if (!std::filesystem::is_regular_file(Source, Error)) continue;
				std::filesystem::rename(Source, Target, Error);
				if (Error)
					HTR_LOG(Warning, "Cannot move editor settings to Config: {} ({})", FPaths::ToUtf8(Source), Error.message());
				break;
			}
		}
	}
	// FWindow가 ImGui를 직접 알지 않도록 메시지 처리기를 훅으로 등록한다.
	bool ImGuiWndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0;
	}



}

FImGuiRenderer::~FImGuiRenderer()
{

}

bool FImGuiRenderer::Init(HWND WindowHandle, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	std::error_code SettingsError;
	const auto SettingsDir = FPaths::EditorSettingsFile().parent_path();
	std::filesystem::create_directories(SettingsDir, SettingsError);
	MigrateEditorSettings(SettingsDir);
	GImGuiSettingsPath = FPaths::ToUtf8(SettingsDir / "imgui.ini");
	io.IniFilename = GImGuiSettingsPath.c_str();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui_ImplWin32_Init(WindowHandle);
	ImGui_ImplDX11_Init(Device, DeviceContext);

	// 분리된 ImGui 창의 스왑체인을 메인 창(FSwapchain)과 같은 플립 모델로 맞춘다.
	// 백엔드 기본값(DISCARD, 버퍼 1개)을 플립 모델 메인 창과 섞어 Present하면 Present가 끝나지 않고 에디터가 멈췄다.
	DXGI_SWAP_CHAIN_DESC ViewportSwapChainDesc{};
	ViewportSwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ViewportSwapChainDesc.SampleDesc.Count = 1;
	ViewportSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	ViewportSwapChainDesc.BufferCount = 2;
	ViewportSwapChainDesc.Windowed = TRUE;
	ViewportSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	ImGui_ImplDX11_SetSwapChainDescs(&ViewportSwapChainDesc, 1);
	// Reuse ImGui's vertex shader and draw commands; only array-depth sampling differs.
	ComPtr<ID3DBlob> Bytecode;
	ComPtr<ID3DBlob> Errors;
	HRESULT PreviewResult = D3DCompileFromFile(FPaths::EngineFile(L"EngineResources/Shaders/ShadowPreview.hlsl").c_str(), nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainPS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
		Bytecode.GetAddressOf(), Errors.GetAddressOf());
	if (FAILED(PreviewResult))
	{
		if (Errors) OutputDebugStringA(static_cast<const char*>(Errors->GetBufferPointer()));
	}
	else
	{
		PreviewResult = Device->CreatePixelShader(Bytecode->GetBufferPointer(), Bytecode->GetBufferSize(),
			nullptr, ShadowPreviewPS.GetAddressOf());
		D3D11_BUFFER_DESC BufferDesc{};
		BufferDesc.ByteWidth = 16;
		BufferDesc.Usage = D3D11_USAGE_DEFAULT;
		BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (SUCCEEDED(PreviewResult)) PreviewResult = Device->CreateBuffer(&BufferDesc, nullptr, ShadowPreviewCB.GetAddressOf());
		D3D11_SAMPLER_DESC SamplerDesc{};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = SamplerDesc.AddressV = SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (SUCCEEDED(PreviewResult)) PreviewResult = Device->CreateSamplerState(&SamplerDesc, ShadowPreviewSampler.GetAddressOf());
	}
	if (FAILED(PreviewResult))
	{
		ShadowPreviewPS.Reset();
		ShadowPreviewCB.Reset();
		ShadowPreviewSampler.Reset();
	}
	FWindow::SetWndProcHook(&ImGuiWndProcHook);

	EditorStyle::Apply();
	UnscaledStyle = ImGui::GetStyle();
	MainWindowHandle = WindowHandle;

	io.ConfigDpiScaleFonts = true;
	io.ConfigDpiScaleViewports = true;
	UpdateDpiScale();

	return true;
}

void FImGuiRenderer::UpdateDpiScale()
{
    const float DpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(MainWindowHandle);
    if (DpiScale == AppliedDpiScale) return;
    // 원본 스타일에서 다시 계산해 모니터 이동 시 반올림 오차가 누적되지 않게 한다.
    ImGui::GetStyle() = UnscaledStyle;
    ImGui::GetStyle().ScaleAllSizes(DpiScale);
    ImGui::GetStyle().FontScaleDpi = DpiScale;
    AppliedDpiScale = DpiScale;
}

void FImGuiRenderer::Begin()
{
    UpdateDpiScale();
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();
}

void FImGuiRenderer::End()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
	// Detached Details windows consume their draw commands above as well.
	PreviewTextures.clear();
}

bool FImGuiRenderer::DrawShadowMap(ID3D11ShaderResourceView* AtlasSRV, unsigned int AtlasIndex,
	ImVec2 Size, ImVec2 UV0, ImVec2 UV1, float Contrast)
{
	if (!AtlasSRV || !ShadowPreviewPS || !ShadowPreviewCB || !ShadowPreviewSampler) return false;
	PreviewTextures.emplace_back(AtlasSRV);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddCallback(BindShadowPreview, this);
	
    // Copy the constants into ImGui-owned storage until actual GPU draws.
	unsigned int Constants[4] = { AtlasIndex, 0, 0, 0 };
	static_assert(sizeof(Contrast) == sizeof(Constants[1]));
	
    std::memcpy(&Constants[1], &Contrast, sizeof(Contrast));
	
    DrawList->AddCallback(SetShadowPreviewSlice, Constants, sizeof(Constants));
	ImGui::Image((ImTextureID)AtlasSRV, Size, UV0, UV1);
	
    DrawList->AddCallback(RestoreShadowPreview, this);
	DrawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState);
	return true;
}

void FImGuiRenderer::BindShadowPreview(const ImDrawList*, const ImDrawCmd* Command)
{
	auto* Self = static_cast<FImGuiRenderer*>(Command->UserCallbackData);
	auto* State = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
	ID3D11DeviceContext* Context = State->DeviceContext;
	Context->PSGetConstantBuffers(0, 1, Self->PreviousPSConstants.ReleaseAndGetAddressOf());
	Context->PSSetShader(Self->ShadowPreviewPS.Get(), nullptr, 0);
	ID3D11Buffer* Buffer = Self->ShadowPreviewCB.Get();
	Context->PSSetConstantBuffers(0, 1, &Buffer);
	ID3D11SamplerState* Sampler = Self->ShadowPreviewSampler.Get();
	Context->PSSetSamplers(0, 1, &Sampler);
}

void FImGuiRenderer::SetShadowPreviewSlice(const ImDrawList*, const ImDrawCmd* Command)
{
	auto* State = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
	ComPtr<ID3D11Buffer> Buffer;
	State->DeviceContext->PSGetConstantBuffers(0, 1, Buffer.GetAddressOf());
	State->DeviceContext->UpdateSubresource(Buffer.Get(), 0, nullptr, Command->UserCallbackData, 0, 0);
}

void FImGuiRenderer::RestoreShadowPreview(const ImDrawList*, const ImDrawCmd* Command)
{
	auto* Self = static_cast<FImGuiRenderer*>(Command->UserCallbackData);
	auto* State = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
	ID3D11Buffer* Buffer = Self->PreviousPSConstants.Get();
	State->DeviceContext->PSSetConstantBuffers(0, 1, &Buffer);
	Self->PreviousPSConstants.Reset();
}

void FImGuiRenderer::Shutdown()
{
	PreviewTextures.clear();
	PreviousPSConstants.Reset();
	ShadowPreviewSampler.Reset();
	ShadowPreviewCB.Reset();
	ShadowPreviewPS.Reset();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
}
