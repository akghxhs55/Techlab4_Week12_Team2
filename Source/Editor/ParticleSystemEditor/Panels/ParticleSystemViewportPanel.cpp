#include "EnginePCH.h"
#include "ParticleSystemViewportPanel.h"

#include "Components/ParticleSystemComponent.h"
#include "Engine/ParticleSystemSceneProxy.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleStats.h"

#include <format>

namespace
{
	const char* GetEmitterTypeName(EDynamicEmitterType Type)
	{
		switch (Type)
		{
		case EDynamicEmitterType::Sprite: return "Sprite";
		case EDynamicEmitterType::Mesh:   return "Mesh";
		case EDynamicEmitterType::Beam:   return "Beam";
		case EDynamicEmitterType::Ribbon: return "Ribbon";
		}
		return "-";
	}

	constexpr ImU32 StatTitleColor = IM_COL32(255, 220, 90, 255);
	constexpr ImU32 StatValueColor = IM_COL32(235, 235, 235, 255);
	constexpr ImU32 StatBackgroundColor = IM_COL32(0, 0, 0, 160);
}

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
	if (Editor.IsStatsOverlayEnabled())
	{
		DrawStatsOverlay(Origin);
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

void FParticleSystemViewportPanel::DrawStatsOverlay(const ImVec2& ViewMin) const
{
	const UParticleSystemComponent* Component = Editor.GetPreviewComponent();
	if (!Component)
	{
		return;
	}

	const FPrimitiveSceneProxy* BaseProxy = Component->GetSceneProxy();
	const FParticleSystemSceneProxy* Proxy = (BaseProxy && BaseProxy->IsParticleSystem())
		? static_cast<const FParticleSystemSceneProxy*>(BaseProxy) : nullptr;
	const TArray<FParticleEmitterInstance*>& Instances = Component->GetEmitterInstances();

	TArray<TArray<FString>> Rows;
	Rows.Add({ "#", "Type", "Active", "Max", "Use", "Update ms", "Spawn ms", "Mem KB" });

	int64 TotalActive = 0, TotalMax = 0;
	uint64 TotalMemory = 0;
	double TotalTickMs = 0.0;

	for (int32 Index = 0; Index < Instances.Num(); ++Index)
	{
		const FParticleEmitterInstance* Instance = Instances[Index];
		if (!Instance)
		{
			continue;
		}

		const FDynamicEmitterDataBase* RenderData =
			(Proxy && Proxy->GetDynamicEmitterData().IsValidIndex(Index)) ? Proxy->GetDynamicEmitterData()[Index].get() : nullptr;
		const FParticleEmitterStats& Stats = Instance->Stats;
		const uint64 Memory = Instance->GetParticleMemoryBytes();
		const double Usage = Instance->MaxActiveParticles > 0
			? 100.0 * Instance->ActiveParticles / Instance->MaxActiveParticles : 0.0;

		Rows.Add({
			std::format("{}", Index),
			RenderData ? GetEmitterTypeName(RenderData->GetType()) : "-",
			std::format("{}", Instance->ActiveParticles),
			std::format("{}", Instance->MaxActiveParticles),
			std::format("{:.0f}%", Usage),
			std::format("{:.3f}", Stats.UpdateMs),
			std::format("{:.3f}", Stats.SpawnMs),
			std::format("{:.1f}", static_cast<double>(Memory) / 1024.0),
		});

		TotalActive += Instance->ActiveParticles;
		TotalMax += Instance->MaxActiveParticles;
		TotalMemory += Memory;
		TotalTickMs += Stats.UpdateMs + Stats.SpawnMs;
	}

	TArray<FString> Summary;
	Summary.Add(std::format("Particles {} / {}   Emitters {}   Memory {:.1f} KB",
		TotalActive, TotalMax, Instances.Num(), static_cast<double>(TotalMemory) / 1024.0));
	Summary.Add(std::format("CPU  Tick {:.3f} ms   Event {:.3f} ms   Send {:.3f} ms   Build {:.3f} ms",
		TotalTickMs, Component->GetEventDispatchMs(), Component->GetSendRenderDataMs(),
		Proxy ? Proxy->GetBuildRenderDataMs() : 0.0));
	Summary.Add(TotalActive > 0
		? std::format("     Tick per particle {:.3f} us", TotalTickMs * 1000.0 / static_cast<double>(TotalActive))
		: FString("     Tick per particle -"));

	const int32 NumColumns = Rows[0].Num();
	TArray<float> ColumnWidths;
	ColumnWidths.Init(0.0f, NumColumns);
	for (const TArray<FString>& Row : Rows)
	{
		for (int32 Column = 0; Column < NumColumns; ++Column)
		{
			ColumnWidths[Column] = (std::max)(ColumnWidths[Column], ImGui::CalcTextSize(Row[Column].c_str()).x);
		}
	}

	constexpr float Margin = 8.0f;
	constexpr float Padding = 6.0f;
	const float ColumnGap = ImGui::GetFontSize();
	const float LineHeight = ImGui::GetTextLineHeight();

	float TableWidth = 0.0f;
	for (float Width : ColumnWidths)
	{
		TableWidth += Width + ColumnGap;
	}
	TableWidth -= ColumnGap;

	float MaxWidth = TableWidth;
	for (const FString& Line : Summary)
	{
		MaxWidth = (std::max)(MaxWidth, ImGui::CalcTextSize(Line.c_str()).x);
	}

	const ImVec2 Origin{ ViewMin.x + Margin + Padding, ViewMin.y + Margin + Padding };
	const float TotalLines = static_cast<float>(Summary.Num() + Rows.Num()) + 0.5f;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled({ Origin.x - Padding, Origin.y - Padding },
		{ Origin.x + MaxWidth + Padding, Origin.y + LineHeight * TotalLines + Padding },
		StatBackgroundColor, 4.0f);

	float Y = Origin.y;
	for (const FString& Line : Summary)
	{
		DrawList->AddText({ Origin.x, Y }, StatValueColor, Line.c_str());
		Y += LineHeight;
	}
	Y += LineHeight * 0.5f;

	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const ImU32 Color = RowIndex == 0 ? StatTitleColor : StatValueColor;
		float X = Origin.x;
		for (int32 Column = 0; Column < NumColumns; ++Column)
		{
			const FString& Text = Rows[RowIndex][Column];
			const float TextX = Column < 2 ? X : X + ColumnWidths[Column] - ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ TextX, Y }, Color, Text.c_str());
			X += ColumnWidths[Column] + ColumnGap;
		}
		Y += LineHeight;
	}
}
