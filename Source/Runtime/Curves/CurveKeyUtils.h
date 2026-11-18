#pragma once

#include "CurveFloat.h"

namespace CurveKeyUtils
{
	float EvaluateSegment(const FFloatCurveKey& Left, const FFloatCurveKey& Right, float Time);
	float Evaluate(const TArray<FFloatCurveKey>& Keys, float Time);
	void SortKeys(TArray<FFloatCurveKey>& Keys);
	void EnsureKeyIds(TArray<FFloatCurveKey>& Keys);
	int32 NewKeyId(const TArray<FFloatCurveKey>& Keys);
	int32 FindKeyIndex(const TArray<FFloatCurveKey>& Keys, int32 KeyId);
	void SetAutoTangent(TArray<FFloatCurveKey>& Keys, int32 Index);
	void SetFlatTangent(FFloatCurveKey& Key);
}
