#include "EnginePCH.h"
#include "DistributionCustomization.h"

#include "Editor/Details/PropertyEditor.h"
#include "Particles/Distributions.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	void DrawCurveKeyCount(const char* Label, int32 NumKeys)
	{
		if (ImGui::GetCurrentTable() != nullptr)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().IndentSpacing * 0.5f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Label);
			ImGui::TableSetColumnIndex(1);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%d keys (edit in Curve Editor)", NumKeys);
			return;
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s: %d keys (edit in Curve Editor)", Label, NumKeys);
	}

	void KeepMinBelowMax(float& Min, float& Max, bool bMinEdited, bool bMaxEdited)
	{
		if (Min <= Max) return;
		if (bMinEdited)      Max = Min;
		else if (bMaxEdited) Min = Max;
	}

	// Curve 모드로 막 바꿨는데 키가 없으면 현재 상수값으로 평평한 키 두 개를 넣는다.
	void SeedCurve(TArray<FFloatCurveKey>& Keys, float Value)
	{
		if (!Keys.IsEmpty())
		{
			return;
		}

		FFloatCurveKey First;
		First.KeyId = 0;
		First.Time = 0.0f;
		First.Value = Value;
		Keys.Add(First);

		FFloatCurveKey Last;
		Last.KeyId = 1;
		Last.Time = 1.0f;
		Last.Value = Value;
		Keys.Add(Last);
	}

	bool DrawFloatDistribution(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const FPropertyMeta& OuterMeta, ImFont* CustomFont)
	{
		FFloatDistribution& Distribution = *static_cast<FFloatDistribution*>(StructPtr);

		const EDistributionMode OldMode = Distribution.Mode;
		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);
		if (OldMode != Distribution.Mode && Distribution.Mode == EDistributionMode::Curve)
		{
			SeedCurve(Distribution.CurveKeys, Distribution.Constant);
		}

		switch (Distribution.Mode)
		{
		case EDistributionMode::Constant:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Constant", CustomFont, &OuterMeta);
			break;
		case EDistributionMode::Uniform:
		{
			const bool bMinEdited = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MinValue", CustomFont, &OuterMeta);
			const bool bMaxEdited = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MaxValue", CustomFont, &OuterMeta);
			KeepMinBelowMax(Distribution.MinValue, Distribution.MaxValue, bMinEdited, bMaxEdited);
			bChanged |= bMinEdited || bMaxEdited;
			break;
		}
		case EDistributionMode::Curve:
			DrawCurveKeyCount("Curve", Distribution.CurveKeys.Num());
			break;
		}

		return bChanged;
	}

	bool DrawVectorDistribution(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const FPropertyMeta& OuterMeta, ImFont* CustomFont)
	{
		FVectorDistribution& Distribution = *static_cast<FVectorDistribution*>(StructPtr);

		const EDistributionMode OldMode = Distribution.Mode;
		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);
		if (OldMode != Distribution.Mode && Distribution.Mode == EDistributionMode::Curve)
		{
			SeedCurve(Distribution.CurveKeysX, Distribution.Constant.X);
			SeedCurve(Distribution.CurveKeysY, Distribution.Constant.Y);
			SeedCurve(Distribution.CurveKeysZ, Distribution.Constant.Z);
		}

		switch (Distribution.Mode)
		{
		case EDistributionMode::Constant:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Constant", CustomFont, &OuterMeta);
			break;
		case EDistributionMode::Uniform:
		{
			const bool bMinEdited = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MinValue", CustomFont, &OuterMeta);
			const bool bMaxEdited = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MaxValue", CustomFont, &OuterMeta);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				KeepMinBelowMax(Distribution.MinValue.V[Axis], Distribution.MaxValue.V[Axis], bMinEdited, bMaxEdited);
			}
			bChanged |= bMinEdited || bMaxEdited;
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "bLockAxes", CustomFont);
			break;
		}
		case EDistributionMode::Curve:
			DrawCurveKeyCount("X", Distribution.CurveKeysX.Num());
			DrawCurveKeyCount("Y", Distribution.CurveKeysY.Num());
			DrawCurveKeyCount("Z", Distribution.CurveKeysZ.Num());
			break;
		}

		return bChanged;
	}
}

void RegisterDistributionCustomizations()
{
	FPropertyEditor::RegisterStructDrawer(FFloatDistribution::StaticStruct(), &DrawFloatDistribution);
	FPropertyEditor::RegisterStructDrawer(FVectorDistribution::StaticStruct(), &DrawVectorDistribution);
}
