#include "EnginePCH.h"
#include "ParticleSystemViewportPanel.h"

FParticleSystemViewportPanel::FParticleSystemViewportPanel(FParticleSystemEditor& InEditor)
	: FParticleSystemEditorPanel(InEditor, "Viewport")
{
}

void FParticleSystemViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	ImGui::InvisibleButton("##ParticleSystemViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (FTexture2D* Target = Client->GetColorTarget(); Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin,
			ImVec2(Origin.x + Size.x, Origin.y + Size.y));
	}

	const bool bHovered = ImGui::IsItemHovered();
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		Editor.FocusPreview();
	}

	const ImGuiIO& IO = ImGui::GetIO();
	FViewportCameraInput Input
	{
		.bCaptured = bFocused && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right),
		.bAllowWheel = bHovered,
		.bForward = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX = IO.MouseDelta.x,
		.MouseY = IO.MouseDelta.y,
		.Wheel = IO.MouseWheel * 120.0f,
	};

	uint32 Width = static_cast<uint32>(Size.x);
	uint32 Height = static_cast<uint32>(Size.y);
	Editor.SetViewportState(Width, Height, Input);
}
