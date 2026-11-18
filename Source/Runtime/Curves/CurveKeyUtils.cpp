#include "EnginePCH.h"
#include "CurveKeyUtils.h"

float CurveKeyUtils::EvaluateSegment(const FFloatCurveKey& Left, const FFloatCurveKey& Right, float Time)
{
	const float Length = Right.Time - Left.Time;
	if (Length <= 0.0001f) return Left.Value;
	const float T = std::clamp((Time - Left.Time) / Length, 0.0f, 1.0f);
	if (Left.InterpMode == ECurveInterpMode::Constant) return Left.Value;
	if (Left.InterpMode == ECurveInterpMode::Linear)   return Left.Value + (Right.Value - Left.Value) * T;
	const float T2 = T * T, T3 = T2 * T;   // Cubic (Hermite)
	return (2 * T3 - 3 * T2 + 1) * Left.Value + (T3 - 2 * T2 + T) * Left.LeaveTangent * Length +
		(-2 * T3 + 3 * T2) * Right.Value + (T3 - T2) * Right.ArriveTangent * Length;
}

float CurveKeyUtils::Evaluate(const TArray<FFloatCurveKey>& Keys, float Time)
{
	if (Keys.IsEmpty()) return 0.0f;
	if (Time <= Keys[0].Time)     return Keys[0].Value;      // 첫 키보다 앞
	if (Time >= Keys.Last().Time) return Keys.Last().Value;  // 마지막 키보다 뒤
	// InTime보다 시간이 큰 첫 키 = 오른쪽 키, 그 바로 앞 = 왼쪽 키
	const auto Right = std::upper_bound(Keys.begin(), Keys.end(), Time,
		[](float Time, const FFloatCurveKey& Key) { return Time < Key.Time; });
	return EvaluateSegment(*(Right - 1), *Right, Time);
}

void CurveKeyUtils::SortKeys(TArray<FFloatCurveKey>& Keys)
{
	std::ranges::stable_sort(Keys, [](const FFloatCurveKey& A, const FFloatCurveKey& B) {
		return A.Time < B.Time;
	});
}

void CurveKeyUtils::EnsureKeyIds(TArray<FFloatCurveKey>& Keys)
{
	int32 MaxId = -1;
	for (const FFloatCurveKey& Key : Keys)
	{
		MaxId = FMath::Max(MaxId, Key.KeyId);
	}

	std::unordered_set<int32> Seen;
	for (auto& Key : Keys)
	{
		if (Key.KeyId < 0 || !Seen.insert(Key.KeyId).second)
		{
			Key.KeyId = ++MaxId;
			Seen.insert(Key.KeyId);
		}
	}
}

int32 CurveKeyUtils::NewKeyId(const TArray<FFloatCurveKey>& Keys)
{
	int32 MaxId = -1;
	for (const auto& Key : Keys)
	{
		MaxId = FMath::Max(MaxId, Key.KeyId);
	}
	return MaxId + 1;
}

int32 CurveKeyUtils::FindKeyIndex(const TArray<FFloatCurveKey>& Keys, int32 KeyId)
{
	for (int32 i = 0; i < Keys.Num(); i++)
	{
		if (Keys[i].KeyId == KeyId)
		{
			return i;
		}
	}
	return -1;
}

void CurveKeyUtils::SetAutoTangent(TArray<FFloatCurveKey>& Keys, int32 Index)
{
	float Slope = 0.0f;
	if (Index > 0 && Index + 1 < Keys.Num())
	{
		const FFloatCurveKey& Prev = Keys[Index - 1];
		const FFloatCurveKey& Next = Keys[Index + 1];
		const float Dt = Next.Time - Prev.Time;
		Slope = Dt > 1e-4f ? (Next.Value - Prev.Value) / Dt : 0.0f;
	}
	Keys[Index].ArriveTangent = Keys[Index].LeaveTangent = Slope;
}

void CurveKeyUtils::SetFlatTangent(FFloatCurveKey& Key)
{
	Key.ArriveTangent = Key.LeaveTangent = 0.0f;
}
