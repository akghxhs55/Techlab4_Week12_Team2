#include "EnginePCH.h"
#include "ParticleSystemCurveEditorPanel.h"

#include "Curves/CurveKeyUtils.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSpawn.h"
#include "Particles/Module/ParticleModuleTypeDataBase.h"

namespace
{
	float NiceStep(float Range, float Pixels, float MinGapPx = 60.0f)
	{
		const float Raw = Range * MinGapPx / Pixels;
		const float Pow10 = powf(10.0f, floorf(log10f(Raw)));
		for (float Mul : { 1.0f, 2.0f, 5.0f })
		{
			if (Pow10 * Mul >= Raw)
			{
				return Pow10 * Mul;
			}
		}
		return Pow10 * 10.0f;
	}

	float DistSq(const ImVec2& A, const ImVec2& B)
	{
		const float X = A.x - B.x, Y = A.y - B.y; return X * X + Y * Y;
	}

	// 눈 아이콘 토글. 클릭되면 true. 아이콘을 못 불러오면 체크박스로 대신한다.
	bool DrawVisibilityToggle(bool bVisible)
	{
		const ImTextureID Icon = ViewportToolbarWidgets::GetIcon(bVisible ? "visible" : "hidden");
		if (Icon == ImTextureID_Invalid)
		{
			bool bChecked = bVisible;
			return ImGui::Checkbox("##Vis", &bChecked);
		}

		// ImageButton은 프레임(배경·테두리)을 그리므로, 보이지 않는 버튼 위에 아이콘만 직접 그린다.
		const float Size = ViewportToolbarWidgets::IconSize();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Size, Min.y + Size);
		const bool bClicked = ImGui::InvisibleButton("##Vis", ImVec2(Size, Size));

		float Alpha = bVisible ? 1.0f : 0.45f;
		if (ImGui::IsItemHovered())
		{
			Alpha = FMath::Min(Alpha + 0.3f, 1.0f);
		}
		ImGui::GetWindowDrawList()->AddImage(Icon, Min, Max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
			ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, Alpha)));

		ImGui::SetItemTooltip(bVisible ? "Hide curve" : "Show curve");
		return bClicked;
	}
}

FParticleSystemCurveEditorPanel::FParticleSystemCurveEditorPanel(FParticleSystemEditor& InEditor)
	: FParticleSystemEditorPanel(InEditor, "Curve Editor")
{
}

void FParticleSystemCurveEditorPanel::DrawContent()
{
	CollectTracks();

	for (int32 i = SelectedKeys.Num() - 1; i >= 0; --i)
	{
		if (!ResolveKey(SelectedKeys[i]))
		{
			SelectedKeys.RemoveAt(i);
		}
	}

	if (Tracks.IsEmpty())
	{
		ImGui::TextDisabled("No curves available");
		return;
	}

	DrawToolbar();

	constexpr float ListWidth = 180.0f;
	ImGui::BeginChild("##CurveTracks", ImVec2(ListWidth, 0), ImGuiChildFlags_Borders);
	DrawTrackList();
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##CurveCanvasChild", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	DrawCanvas();
	ImGui::EndChild();
}

void FParticleSystemCurveEditorPanel::CollectTracks()
{
	Tracks.Empty();
	UParticleEmitter* Emitter = Editor.GetSelectedEmitter();
	UParticleLODLevel* LODLevel = Emitter ? Editor.GetCurrentLODLevel(Emitter) : nullptr;
	if (!LODLevel)
	{
		return;
	}

	CollectModuleTracks(LODLevel->GetRequiredModule());
	CollectModuleTracks(LODLevel->GetSpawnModule());
	CollectModuleTracks(LODLevel->GetTypeDataModule());
	for (auto* Module : LODLevel->GetModules())
	{
		CollectModuleTracks(Module);
	}
}

void FParticleSystemCurveEditorPanel::CollectModuleTracks(UParticleModule* Module)
{
	if (!Module)
	{
		return;
	}

	for (const UStruct* Struct = Module->GetClass(); Struct; Struct = Struct->GetSuperStruct())
	{
		for (FProperty* Property : Struct->GetProperties())
		{
			const FStructProperty* StructProp = CastField<FStructProperty>(Property);
			if (!StructProp)
			{
				continue;
			}

			void* ValuePtr = StructProp->ContainerPtrToValuePtr(Module);
			// 주소는 삭제 후 새 모듈에 재사용될 수 있어 선택·숨김 상태가 엉뚱한 커브로 옮겨 붙는다. 재사용되지 않는 UUID를 쓴다
			const FString BaseId = std::to_string(Module->GetUUID()) + "/" + Property->GetName();

			auto AddTrack = [&](TArray<FFloatCurveKey>& Keys, const char* Suffix, ImU32 Color)
			{
				CurveKeyUtils::EnsureKeyIds(Keys);
				FCurveTrack& Track = Tracks.Emplace_GetRef();
				Track.Module = Module;
				Track.Property = StructProp;
				Track.Keys = &Keys;
				Track.Id = BaseId + Suffix;
				Track.Label = Property->GetName() + Suffix;
				Track.Color = Color;
			};

			if (StructProp->Struct == FFloatDistribution::StaticStruct())
			{
				auto& Dist = *static_cast<FFloatDistribution*>(ValuePtr);
				if (Dist.Mode == EDistributionMode::Curve)
				{
					AddTrack(Dist.CurveKeys, "", IM_COL32(230, 200, 80, 255));
				}
			}
			else if (StructProp->Struct == FVectorDistribution::StaticStruct())
			{
				auto& Dist = *static_cast<FVectorDistribution*>(ValuePtr);
				if (Dist.Mode == EDistributionMode::Curve)
				{
					AddTrack(Dist.CurveKeysX, ".X", IM_COL32(230, 80, 80, 255));
					AddTrack(Dist.CurveKeysY, ".Y", IM_COL32(80, 200, 80, 255));
					AddTrack(Dist.CurveKeysZ, ".Z", IM_COL32(90, 140, 240, 255));
				}
			}
		}
	}
}

FParticleSystemCurveEditorPanel::FCurveTrack* FParticleSystemCurveEditorPanel::FindTrack(const FString& Id)
{
	for (auto& Track : Tracks)
	{
		if (Track.Id == Id)
		{
			return &Track;
		}
	}
	return nullptr;
}

void FParticleSystemCurveEditorPanel::DrawToolbar()
{
	if (ImGui::Button("Fit"))
	{
		bFitPending = true;
	}
	if (SelectedKeys.Num() != 1)
	{
		return;
	}

	FCurveTrack* Track = nullptr;
	FFloatCurveKey* Key = ResolveKey(SelectedKeys[0], &Track);
	if (!Key)
	{
		return;
	}

	bool bChanged = false;
	ImGui::SameLine(); 
	ImGui::SetNextItemWidth(90); 
	bChanged |= ImGui::DragFloat("Time", &Key->Time, 0.01f, 0.0f, FLT_MAX);

	ImGui::SameLine();
	ImGui::SetNextItemWidth(90);
	bChanged |= ImGui::DragFloat("Value", &Key->Value, 0.01f);

	int32 Mode = static_cast<int32>(Key->InterpMode);
	ImGui::SameLine(); 
	ImGui::SetNextItemWidth(90);
	if (ImGui::Combo("Interp", &Mode, "Constant\0Linear\0Cubic\0"))
	{
		Key->InterpMode = static_cast<ECurveInterpMode>(Mode);
		bChanged = true;
	}
	if (Key->InterpMode == ECurveInterpMode::Cubic)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		bChanged |= ImGui::DragFloat("Arrive", &Key->ArriveTangent, 0.01f);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		bChanged |= ImGui::DragFloat("Leave", &Key->LeaveTangent, 0.01f);
	}

	if (bChanged) 
	{
		Commit(*Track);
	}
}

void FParticleSystemCurveEditorPanel::DrawTrackList()
{
	// 한 행의 높이를 정해 두고 아이콘·색 사각형·이름을 모두 그 행의 세로 중앙에 맞춘다.
	const float IconSize = ViewportToolbarWidgets::IconSize();
	const float SwatchSize = EditorStyle::Scale(10.0f);
	const float Spacing = ImGui::GetStyle().ItemInnerSpacing.x;
	const float RowHeight = FMath::Max(IconSize, ImGui::GetTextLineHeight());
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
	for (const auto& Track : Tracks)
	{
		ImGui::PushID(Track.Id.c_str());

		const ImVec2 RowMin = ImGui::GetCursorScreenPos();
		const float CenterY = RowMin.y + RowHeight * 0.5f;

		// 눈 아이콘
		ImGui::SetCursorScreenPos(ImVec2(RowMin.x, CenterY - IconSize * 0.5f));
		const bool bVisible = IsVisible(Track);
		if (DrawVisibilityToggle(bVisible))
		{
			if (bVisible)
			{
				HiddenTracks.insert(Track.Id);
			}
			else
			{
				HiddenTracks.erase(Track.Id);
			}
		}

		// 색 사각형 (상호작용이 없으므로 직접 그린다)
		const float SwatchX = RowMin.x + IconSize + Spacing;
		DrawList->AddRectFilled(ImVec2(SwatchX, CenterY - SwatchSize * 0.5f),
			ImVec2(SwatchX + SwatchSize, CenterY + SwatchSize * 0.5f), Track.Color);

		// 이름
		ImGui::SetCursorScreenPos(ImVec2(SwatchX + SwatchSize + Spacing, RowMin.y));
		if (ImGui::Selectable(Track.Label.c_str(), ActiveTrackId == Track.Id, ImGuiSelectableFlags_None, ImVec2(0.0f, RowHeight)))
		{
			ActiveTrackId = Track.Id;
		}
		if (Track.Module == Editor.GetSelectedModule())
		{
			ImGui::SetItemTooltip("Selected module");
		}

		ImGui::PopID();
	}
	ImGui::PopStyleVar();
}

void FParticleSystemCurveEditorPanel::DrawCanvas()
{
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	ImVec2 Size = ImGui::GetContentRegionAvail();
	Size.x = FMath::Max(Size.x, 50.0f);
	Size.y = FMath::Max(Size.y, 50.0f);

	ImGui::InvisibleButton("##CurveCanvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bHovered = ImGui::IsItemHovered();

	View.Origin = Pos;
	View.Size = Size;
	if (bFitPending)
	{
		FitView(); 
		bFitPending = false;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Max(Pos.x + Size.x, Pos.y + Size.y);
	DrawList->PushClipRect(Pos, Max, true);
	DrawList->AddRectFilled(Pos, Max, IM_COL32(28, 28, 30, 255));

	HandleViewInput(bHovered);
	DrawGrid(DrawList);
	for (const FCurveTrack& Track : Tracks)
	{
		if (IsVisible(Track))
		{
			DrawCurve(DrawList, Track);
		}
	}

	HandleKeyInput(bHovered);
	DrawKeys(DrawList);

	DrawList->PopClipRect();
	DrawContextMenu();
}

void FParticleSystemCurveEditorPanel::DrawGrid(ImDrawList* DrawList) const
{
	constexpr ImU32 LineCol = IM_COL32(60, 60, 64, 255);
	constexpr ImU32 AxisCol = IM_COL32(110, 110, 120, 255);
	constexpr ImU32 TextCol = IM_COL32(150, 150, 150, 255);
	const float Top = View.Origin.y;
	const float Bottom = View.Origin.y + View.Size.y;
	const float Left = View.Origin.x;
	const float Right = View.Origin.x + View.Size.x;

	const float TStep = NiceStep(View.TimeMax - View.TimeMin, View.Size.x);
	for (float T = ceilf(View.TimeMin / TStep) * TStep; T <= View.TimeMax; T += TStep)
	{
		const float X = View.TimeToX(T);
		DrawList->AddLine(ImVec2(X, Top), ImVec2(X, Bottom), fabsf(T) < TStep * 0.5f ? AxisCol : LineCol);
		char Buf[32]; snprintf(Buf, sizeof(Buf), "%.3g", T);
		DrawList->AddText(ImVec2(X + 2, Bottom - 16), TextCol, Buf);
	}

	const float VStep = NiceStep(View.ValueMax - View.ValueMin, View.Size.y, 40.0f);
	for (float V = ceilf(View.ValueMin / VStep) * VStep; V <= View.ValueMax; V += VStep)
	{
		const float Y = View.ValueToY(V);
		DrawList->AddLine(ImVec2(Left, Y), ImVec2(Right, Y), fabsf(V) < VStep * 0.5f ? AxisCol : LineCol);
		char Buf[32]; snprintf(Buf, sizeof(Buf), "%.3g", V);
		DrawList->AddText(ImVec2(Left + 2, Y - 14), TextCol, Buf);
	}
}

void FParticleSystemCurveEditorPanel::DrawCurve(ImDrawList* DrawList, const FCurveTrack& Track) const
{
	if (Track.Keys->IsEmpty()) 
	{
		return;
	}

	TArray<ImVec2> Points;
	for (float X = View.Origin.x; X <= View.Origin.x + View.Size.x; X += 2.0f)
	{
		const float Value = CurveKeyUtils::Evaluate(*Track.Keys, View.XToTime(X));
		Points.Add(ImVec2(X, View.ValueToY(Value)));
	}
	const float Thickness = (Track.Id == ActiveTrackId) ? 2.5f : 1.5f;
	DrawList->AddPolyline(Points.GetData(), Points.Num(), Track.Color, ImDrawFlags_None, Thickness);
}

void FParticleSystemCurveEditorPanel::DrawKeys(ImDrawList* DrawList) const
{
	for (const FCurveTrack& Track : Tracks)
	{
		if (!IsVisible(Track))
		{
			continue;
		}
		const TArray<FFloatCurveKey>& Keys = *Track.Keys;
		for (int32 i = 0; i < Keys.Num(); ++i)
		{
			const FFloatCurveKey& Key = Keys[i];
			const ImVec2 P = View.ToScreen(Key.Time, Key.Value);
			const bool bSelected = IsSelected(Track.Id, Key.KeyId);

			if (bSelected)
			{
				constexpr ImU32 HandleCol = IM_COL32(200, 200, 200, 255);
				if (i + 1 < Keys.Num() && Key.InterpMode == ECurveInterpMode::Cubic)
				{
					const ImVec2 H = TangentHandlePos(Key, Key.LeaveTangent, +1.0f);
					DrawList->AddLine(P, H, HandleCol);
					DrawList->AddCircleFilled(H, 4.0f, HandleCol);
				}
				if (i > 0 && Keys[i - 1].InterpMode == ECurveInterpMode::Cubic)
				{
					const ImVec2 H = TangentHandlePos(Key, Key.ArriveTangent, -1.0f);
					DrawList->AddLine(P, H, HandleCol);
					DrawList->AddCircleFilled(H, 4.0f, HandleCol);
				}
			}

			constexpr float R = 5.0f;
			DrawList->AddQuadFilled(ImVec2(P.x, P.y - R), ImVec2(P.x + R, P.y), ImVec2(P.x, P.y + R), ImVec2(P.x - R, P.y),
				bSelected ? IM_COL32(255, 255, 255, 255) : Track.Color);
		}
	}
}

void FParticleSystemCurveEditorPanel::DrawContextMenu()
{
	if (!ImGui::BeginPopup("##CurveKeyMenu"))
	{
		return;
	}

	auto ForEachSelected = [&](auto&& Fn)
	{
		TArray<FCurveTrack*> Touched;
		for (const FKeyRef& Ref : SelectedKeys)
		{
			FCurveTrack* Track = FindTrack(Ref.TrackId);
			if (!Track)
			{
				continue;
			}
			const int32 Index = CurveKeyUtils::FindKeyIndex(*Track->Keys, Ref.KeyId);
			if (Index < 0)
			{
				continue;
			}
			Fn(*Track->Keys, Index);
			if (std::ranges::find(Touched, Track) == Touched.end())
			{
				Touched.Add(Track);
			}
		}
		for (FCurveTrack* Track : Touched)
		{
			Commit(*Track);
		}
	};

	const bool bHasSelection = !SelectedKeys.IsEmpty();
	if (ImGui::MenuItem("Constant", nullptr, false, bHasSelection))
	{
		ForEachSelected([](auto& K, int32 i) { K[i].InterpMode = ECurveInterpMode::Constant; });
	}
	if (ImGui::MenuItem("Linear", nullptr, false, bHasSelection))
	{
		ForEachSelected([](auto& K, int32 i) { K[i].InterpMode = ECurveInterpMode::Linear; });
	}
	if (ImGui::MenuItem("Cubic", nullptr, false, bHasSelection))
	{
		ForEachSelected([](auto& K, int32 i) { K[i].InterpMode = ECurveInterpMode::Cubic; CurveKeyUtils::SetAutoTangent(K, i); });
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Auto Tangent", nullptr, false, bHasSelection))
	{
		ForEachSelected([](auto& K, int32 i) { CurveKeyUtils::SetAutoTangent(K, i); });
	}
	if (ImGui::MenuItem("Flat Tangent", nullptr, false, bHasSelection))
	{
		ForEachSelected([](auto& K, int32 i) { CurveKeyUtils::SetFlatTangent(K[i]); });
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Delete", "Del", false, bHasSelection))
	{
		DeleteSelectedKeys();
	}
	if (ImGui::MenuItem("Fit View", "F"))
	{
		FitView();
	}

	ImGui::EndPopup();
}

void FParticleSystemCurveEditorPanel::HandleViewInput(bool bHovered)
{
	ImGuiIO& IO = ImGui::GetIO();

	if (bHovered && IO.MouseWheel != 0.0f)
	{
		const float Scale = powf(0.85f, IO.MouseWheel);
		const float MouseT = View.XToTime(IO.MousePos.x);
		const float MouseV = View.YToValue(IO.MousePos.y);
		if (!IO.KeyShift)
		{
			View.TimeMin = MouseT + (View.TimeMin - MouseT) * Scale;
			View.TimeMax = MouseT + (View.TimeMax - MouseT) * Scale;
		}
		if (!IO.KeyCtrl)
		{
			View.ValueMin = MouseV + (View.ValueMin - MouseV) * Scale;
			View.ValueMax = MouseV + (View.ValueMax - MouseV) * Scale;
		}
	}

	for (ImGuiMouseButton Button : { ImGuiMouseButton_Right, ImGuiMouseButton_Middle })
	{
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(Button))
		{
			const float Dt = IO.MouseDelta.x / View.PixelsPerTime();
			const float Dv = IO.MouseDelta.y / View.PixelsPerValue();
			View.TimeMin -= Dt; View.TimeMax -= Dt;
			View.ValueMin += Dv; View.ValueMax += Dv;
		}
	}

	if (bHovered && ImGui::IsKeyPressed(ImGuiKey_F))
	{
		FitView();
	}
}

void FParticleSystemCurveEditorPanel::HandleKeyInput(bool bHovered)
{
	ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 Mouse = IO.MousePos;

	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const FHit Hit = HitTest(Mouse);
		if (Hit.Mode == EDragMode::None)
		{
			if (!IO.KeyShift)
			{
				SelectedKeys.Empty();
			}
		}
		else
		{
			const FString& TrackId = Tracks[Hit.TrackIndex].Id;
			ActiveTrackId = TrackId;
			if (Hit.Mode == EDragMode::Keys && !IsSelected(TrackId, Hit.KeyId))
			{
				if (!IO.KeyShift)
				{
					SelectedKeys.Empty();
				}
				SelectedKeys.Add({ TrackId, Hit.KeyId });
			}
			DragMode = Hit.Mode;
			DragKey = { TrackId, Hit.KeyId };
			DragGrabOffset = ImVec2(0.0f, 0.0f);
			if (Hit.Mode == EDragMode::LeaveTangent || Hit.Mode == EDragMode::ArriveTangent)
			{
				if (const FFloatCurveKey* Key = ResolveKey(DragKey))
				{
					const bool bLeave = Hit.Mode == EDragMode::LeaveTangent;
					const ImVec2 HandlePos = TangentHandlePos(*Key, bLeave ? Key->LeaveTangent : Key->ArriveTangent, bLeave ? 1.0f : -1.0f);
					DragGrabOffset = ImVec2(HandlePos.x - Mouse.x, HandlePos.y - Mouse.y);
				}
			}
			BeginKeyDrag(Mouse);
		}
	}

	if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && HitTest(Mouse).Mode == EDragMode::None)
	{
		AddKeyAt(Mouse);
	}

	if (DragMode != EDragMode::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
	{
		TArray<FCurveTrack*> Touched;

		if (DragMode == EDragMode::Keys)
		{
			const float Dt = (Mouse.x - DragStartMouse.x) / View.PixelsPerTime();
			const float Dv = -(Mouse.y - DragStartMouse.y) / View.PixelsPerValue();
			for (const FDragOrigin& Origin : DragOrigins)
			{
				FCurveTrack* Track = nullptr;
				FFloatCurveKey* Key = ResolveKey({ Origin.TrackId, Origin.KeyId }, &Track);
				if (!Key)
				{
					continue;
				}
				Key->Time = FMath::Max(0.0f, Origin.Time + Dt);
				Key->Value = Origin.Value + Dv;
				if (IO.KeyCtrl) // 그리드 스냅
				{
					const float TStep = NiceStep(View.TimeMax - View.TimeMin, View.Size.x) * 0.5f;
					const float VStep = NiceStep(View.ValueMax - View.ValueMin, View.Size.y, 40.0f) * 0.5f;
					Key->Time = roundf(Key->Time / TStep) * TStep;
					Key->Value = roundf(Key->Value / VStep) * VStep;
				}
				if (std::ranges::find(Touched, Track) == Touched.end())
				{
					Touched.Add(Track);
				}
			}
		}
		else
		{
			FCurveTrack* Track = nullptr;
			if (FFloatCurveKey* Key = ResolveKey(DragKey, &Track))
			{
				// 잡은 지점과 핸들 중심의 차이를 보정해 클릭 순간 튀지 않게 한다.
				const ImVec2 HandlePos(Mouse.x + DragGrabOffset.x, Mouse.y + DragGrabOffset.y);
				const ImVec2 KeyPos = View.ToScreen(Key->Time, Key->Value);
				float Dx = HandlePos.x - KeyPos.x;
				// Leave는 키 오른쪽, Arrive는 키 왼쪽에만 둔다 (0으로 나누기 방지 겸)
				Dx = (DragMode == EDragMode::LeaveTangent) ? FMath::Max(Dx, 1.0f) : FMath::Min(Dx, -1.0f);
				const float Dy = HandlePos.y - KeyPos.y;
				const float Slope = (-Dy / View.PixelsPerValue()) / (Dx / View.PixelsPerTime());

				const bool bBroken = IO.KeyAlt;
				if (DragMode == EDragMode::LeaveTangent || !bBroken)
				{
					Key->LeaveTangent = Slope;
				}
				if (DragMode == EDragMode::ArriveTangent || !bBroken)
				{
					Key->ArriveTangent = Slope;
				}
				Touched.Add(Track);
			}
		}

		for (FCurveTrack* Track : Touched)
		{
			Commit(*Track);
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		DeleteSelectedKeys();
	}

	if (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
		IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < IO.MouseDragThreshold * IO.MouseDragThreshold)
	{
		const FHit Hit = HitTest(Mouse);
		if (Hit.Mode == EDragMode::Keys && !IsSelected(Tracks[Hit.TrackIndex].Id, Hit.KeyId))
		{
			SelectedKeys.Empty();
			SelectedKeys.Add({ Tracks[Hit.TrackIndex].Id, Hit.KeyId });
		}
		ImGui::OpenPopup("##CurveKeyMenu");
	}
}

FParticleSystemCurveEditorPanel::FHit FParticleSystemCurveEditorPanel::HitTest(const ImVec2& Mouse) const
{
	constexpr float RadiusSq = 7.0f * 7.0f;

	for (const FKeyRef& Ref : SelectedKeys)
	{
		for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
		{
			const FCurveTrack& Track = Tracks[TrackIndex];
			if (Track.Id != Ref.TrackId)
			{
				continue;
			}
			const TArray<FFloatCurveKey>& Keys = *Track.Keys;
			const int32 i = CurveKeyUtils::FindKeyIndex(Keys, Ref.KeyId);
			if (i < 0)
			{
				continue;
			}
			if (i + 1 < Keys.Num() && Keys[i].InterpMode == ECurveInterpMode::Cubic &&
				DistSq(Mouse, TangentHandlePos(Keys[i], Keys[i].LeaveTangent, +1.0f)) < RadiusSq)
			{
				return { EDragMode::LeaveTangent, TrackIndex, Ref.KeyId };
			}
			if (i > 0 && Keys[i - 1].InterpMode == ECurveInterpMode::Cubic &&
				DistSq(Mouse, TangentHandlePos(Keys[i], Keys[i].ArriveTangent, -1.0f)) < RadiusSq)
			{
				return { EDragMode::ArriveTangent, TrackIndex, Ref.KeyId };
			}
		}
	}

	for (int32 TrackIndex = Tracks.Num() - 1; TrackIndex >= 0; --TrackIndex)
	{
		const FCurveTrack& Track = Tracks[TrackIndex];
		if (!IsVisible(Track))
		{
			continue;
		}
		for (const FFloatCurveKey& Key : *Track.Keys)
		{
			if (DistSq(Mouse, View.ToScreen(Key.Time, Key.Value)) < RadiusSq)
			{
				return { EDragMode::Keys, TrackIndex, Key.KeyId };
			}
		}
	}

	return {};
}

ImVec2 FParticleSystemCurveEditorPanel::TangentHandlePos(const FFloatCurveKey& Key, float Tangent, float Dir) const
{
	constexpr float HandleLength = 50.0f;
	const ImVec2 KeyPos = View.ToScreen(Key.Time, Key.Value);
	const float Dx = View.PixelsPerTime();
	const float Dy = -Tangent * View.PixelsPerValue();
	const float Len = sqrtf(Dx * Dx + Dy * Dy);
	return { KeyPos.x + Dir * Dx / Len * HandleLength, KeyPos.y + Dir * Dy / Len * HandleLength };
}

void FParticleSystemCurveEditorPanel::BeginKeyDrag(const ImVec2& Mouse)
{
	DragStartMouse = Mouse;
	DragOrigins.Empty();
	for (const FKeyRef& Ref : SelectedKeys)
	{
		if (const FFloatCurveKey* Key = ResolveKey(Ref))
		{
			DragOrigins.Add({ Ref.TrackId, Ref.KeyId, Key->Time, Key->Value });
		}
	}
}

void FParticleSystemCurveEditorPanel::AddKeyAt(const ImVec2& Mouse)
{
	FCurveTrack* Track = FindTrack(ActiveTrackId);
	if (!Track && !Tracks.IsEmpty())
	{
		Track = &Tracks[0];
	}
	if (!Track)
	{
		return;
	}

	TArray<FFloatCurveKey>& Keys = *Track->Keys;
	const float Time = (std::max)(0.0f, View.XToTime(Mouse.x));

	FFloatCurveKey NewKey;
	NewKey.KeyId = CurveKeyUtils::NewKeyId(Keys);
	NewKey.Time = Time;
	const float CurveValue = Keys.IsEmpty() ? View.YToValue(Mouse.y) : CurveKeyUtils::Evaluate(Keys, Time);
	NewKey.Value = fabsf(View.ValueToY(CurveValue) - Mouse.y) < 8.0f ? CurveValue : View.YToValue(Mouse.y);
	Keys.Add(NewKey);
	CurveKeyUtils::SortKeys(Keys);

	const int32 Index = CurveKeyUtils::FindKeyIndex(Keys, NewKey.KeyId);
	if (Index > 0)
	{
		Keys[Index].InterpMode = Keys[Index - 1].InterpMode;
	}
	CurveKeyUtils::SetAutoTangent(Keys, Index);

	SelectedKeys.Empty();
	SelectedKeys.Add({ Track->Id, NewKey.KeyId });
	Commit(*Track);
}

void FParticleSystemCurveEditorPanel::DeleteSelectedKeys()
{
	TArray<FCurveTrack*> Touched;
	for (const FKeyRef& Ref : SelectedKeys)
	{
		FCurveTrack* Track = FindTrack(Ref.TrackId);
		if (!Track)
		{
			continue;
		}
		const int32 Index = CurveKeyUtils::FindKeyIndex(*Track->Keys, Ref.KeyId);
		if (Index < 0)
		{
			continue;
		}
		Track->Keys->RemoveAt(Index);
		if (std::ranges::find(Touched, Track) == Touched.end())
		{
			Touched.Add(Track);
		}
	}
	SelectedKeys.Empty();
	for (FCurveTrack* Track : Touched) Commit(*Track);
}

void FParticleSystemCurveEditorPanel::FitView()
{
	float TMin = FLT_MAX, TMax = -FLT_MAX, VMin = FLT_MAX, VMax = -FLT_MAX;
	for (const FCurveTrack& Track : Tracks)
	{
		if (!IsVisible(Track) || Track.Keys->IsEmpty())
		{
			continue;
		}
		const TArray<FFloatCurveKey>& Keys = *Track.Keys;
		TMin = (std::min)(TMin, Keys[0].Time);
		TMax = (std::max)(TMax, Keys.Last().Time);
		for (int32 i = 0; i <= 64; ++i)
		{
			const float T = Keys[0].Time + (Keys.Last().Time - Keys[0].Time) * i / 64.0f;
			const float V = CurveKeyUtils::Evaluate(Keys, T);
			VMin = (std::min)(VMin, V);
			VMax = (std::max)(VMax, V);
		}
	}
	if (TMin > TMax) { TMin = 0; TMax = 1; }
	if (VMin > VMax) { VMin = 0; VMax = 1; }
	if (TMax - TMin < 1e-3f) { TMin -= 0.5f; TMax += 0.5f; }
	if (VMax - VMin < 1e-3f) { VMin -= 0.5f; VMax += 0.5f; }

	const float PadT = (TMax - TMin) * 0.05f, PadV = (VMax - VMin) * 0.1f;
	View.TimeMin = TMin - PadT;  View.TimeMax = TMax + PadT;
	View.ValueMin = VMin - PadV; View.ValueMax = VMax + PadV;
}

bool FParticleSystemCurveEditorPanel::IsSelected(const FString& TrackId, int32 KeyId) const
{
	for (const FKeyRef& Ref : SelectedKeys)
	{
		if (Ref.TrackId == TrackId && Ref.KeyId == KeyId)
		{
			return true;
		}
	}

	return false;
}

FFloatCurveKey* FParticleSystemCurveEditorPanel::ResolveKey(const FKeyRef& Ref, FCurveTrack** OutTrack)
{
	FCurveTrack* Track = FindTrack(Ref.TrackId);
	if (!Track)
	{
		return nullptr;
	}
	const int32 Index = CurveKeyUtils::FindKeyIndex(*Track->Keys, Ref.KeyId);
	if (Index < 0)
	{
		return nullptr;
	}
	if (OutTrack)
	{
		*OutTrack = Track;
	}
	return &(*Track->Keys)[Index];
}

void FParticleSystemCurveEditorPanel::Commit(const FCurveTrack& Track) const
{
	CurveKeyUtils::SortKeys(*Track.Keys);
	Track.Module->PostEditChangeProperty(*Track.Property);
	Editor.NotifyPropertyEdited();
}
