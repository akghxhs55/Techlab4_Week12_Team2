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

	bool DrawFloatDistribution(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, ImFont* CustomFont)
	{
		const FFloatDistribution& Distribution = *static_cast<const FFloatDistribution*>(StructPtr);

		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);

		switch (Distribution.Mode)
		{
		case EDistributionMode::Constant:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Constant", CustomFont);
			break;
		case EDistributionMode::Uniform:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MinValue", CustomFont);
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MaxValue", CustomFont);
			break;
		case EDistributionMode::Curve:
			DrawCurveKeyCount("Curve", Distribution.CurveKeys.Num());
			break;
		}

		return bChanged;
	}

	bool DrawVectorDistribution(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, ImFont* CustomFont)
	{
		const FVectorDistribution& Distribution = *static_cast<const FVectorDistribution*>(StructPtr);

		bool bChanged = FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Mode", CustomFont);

		switch (Distribution.Mode)
		{
		case EDistributionMode::Constant:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "Constant", CustomFont);
			break;
		case EDistributionMode::Uniform:
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MinValue", CustomFont);
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "MaxValue", CustomFont);
			bChanged |= FPropertyEditor::DrawStructMember(Owner, StructPtr, Struct, "bLockAxes", CustomFont);
			break;
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
