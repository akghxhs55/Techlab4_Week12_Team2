#include "EnginePCH.h"
#include "DistributionCustomization.h"

#include "Editor/Details/PropertyEditor.h"
#include "Particles/Distributions.h"
#include <imgui.h>

namespace
{
	void DrawCurveKeyCount(const char* Label, int32 NumKeys)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s: %d keys (edit in Curve Editor)", Label, NumKeys);
	}

	void KeepMinBelowMax(float& Min, float& Max, bool bMinEdited, bool bMaxEdited)
	{
		if (Min <= Max) return;
		if (bMinEdited)      Max = Min;
		else if (bMaxEdited) Min = Max;
	}

	bool DrawFloatDistribution(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const FPropertyMeta& OuterMeta, ImFont* CustomFont)
	{
		FFloatDistribution& Distribution = *static_cast<FFloatDistribution*>(StructPtr);

		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);

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

		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);

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
