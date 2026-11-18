#include "EnginePCH.h"
#include "Distributions.h"
#include "Math/RandomStream.h"
#include "Curves/CurveKeyUtils.h"
#include <algorithm>
#include <functional>
#include <cassert>

// 칸 하나에 들어갈 수 있는 최대 숫자 개수
static constexpr int32 MaxValuesPerEntry = 6;

static void BuildLookupTable(FDistributionLookupTable& Table, ERawDistributionOperation Op, int32 ValuesPerEntry, int32 EntryCount, float MinTime, float MaxTime, const std::function<void(float Time, float* Out)>& Sample)
{
	Table.Op = Op;
	Table.EntryCount = EntryCount;
	Table.EntryStride = (Op == ERawDistributionOperation::None ? 1 : 2) * ValuesPerEntry;
	Table.SubEntryStride = (Op == ERawDistributionOperation::None ? 0 : ValuesPerEntry);
	float TimeGap = EntryCount > 1 ? (MaxTime - MinTime) / (EntryCount - 1) : 0.0f;
	float TimeScale = TimeGap > 0.0f ? 1.0f / TimeGap : 0.0f;
	Table.TimeScale = TimeScale;
	Table.TimeBias = MinTime;
	Table.Values.Empty();
	Table.Values.Reserve(Table.EntryCount * Table.EntryStride);

	for(int32 i = 0; i < EntryCount; ++i)
	{
		float Time = MinTime + i * TimeGap;
		assert(Table.EntryStride <= MaxValuesPerEntry && "칸 하나의 숫자 개수가 Values 배열보다 큽니다");
		float Values[MaxValuesPerEntry];
		Sample(Time, Values);
		for(int32 j = 0; j < Table.EntryStride; ++j)
		{
			Table.Values.Add(Values[j]);
		}
	}
}

// 커브를 몇 칸으로 구울지. 많을수록 원본 커브에 가깝다.
static constexpr int32 CurveBakeSampleCount = 32;

void FFloatDistribution::Bake()
{
	// 커브는 키가 시간순이어야 구간 찾기가 맞으므로 먼저 정렬한다.
	CurveKeyUtils::SortKeys(CurveKeys);

	// 키가 0개/1개인 커브는 상수와 같다.
	EDistributionMode BakeMode = Mode;
	float ConstantValue = Constant;
	if (Mode == EDistributionMode::Curve && CurveKeys.Num() <= 1)
	{
		BakeMode = EDistributionMode::Constant;
		ConstantValue = CurveKeys.IsEmpty() ? 0.0f : CurveKeys[0].Value;
	}

	switch (BakeMode)
	{
	case EDistributionMode::Constant:
		// 칸 1개: [값]
		BuildLookupTable(LookupTable, ERawDistributionOperation::None, 1, 1, 0.0f, 0.0f,
			[ConstantValue](float, float* Out) { Out[0] = ConstantValue; });
		break;

	case EDistributionMode::Uniform:
		// 칸 1개: [Min, Max]. 랜덤은 GetFloatValue에서 뽑는다.
		BuildLookupTable(LookupTable, ERawDistributionOperation::Random, 1, 1, 0.0f, 0.0f,
			[this](float, float* Out) { Out[0] = MinValue; Out[1] = MaxValue; });
		break;

	case EDistributionMode::Curve:
		// 첫 키 ~ 마지막 키 구간을 일정 간격 32칸으로 샘플링
		BuildLookupTable(LookupTable, ERawDistributionOperation::None, 1, CurveBakeSampleCount,
			CurveKeys[0].Time, CurveKeys.Last().Time,
			[this](float Time, float* Out) { Out[0] = CurveKeyUtils::Evaluate(CurveKeys, Time); });
		break;
	}
}

float FFloatDistribution::GetFloatValue(float InTime, FRandomStream& InRandom) const
{
	// Bake를 안 불렀으면 테이블이 비어 있다. 크래시 대신 0을 돌려준다.
	if (LookupTable.IsEmpty()) return 0.0f;

	const float* Entry1 = nullptr;
	const float* Entry2 = nullptr;
	float Alpha = 0.0f;
	LookupTable.GetEntry(InTime, Entry1, Entry2, Alpha);   // 시간으로 앞뒤 칸, 보간 비율

	const float Value = FMath::Lerp(Entry1[0], Entry2[0], Alpha);
	if (LookupTable.Op == ERawDistributionOperation::Random)
	{
		// 칸 안의 Min)과 Max를 각각 보간한 뒤 그 사이에서 랜덤
		const int32 MaxOffset = LookupTable.SubEntryStride;
		const float MaxValueAtTime = FMath::Lerp(Entry1[MaxOffset], Entry2[MaxOffset], Alpha);
		return Value + (MaxValueAtTime - Value) * InRandom.GetFraction();
	}
	return Value;
}

void FVectorDistribution::Bake()
{
	for (TArray<FFloatCurveKey>* Keys : {&CurveKeysX, &CurveKeysY, &CurveKeysZ})
	{
		std::stable_sort(Keys->begin(), Keys->end(),
			[](const FFloatCurveKey& A, const FFloatCurveKey& B) { return A.Time < B.Time; });
	}

	switch (Mode)
	{
	case EDistributionMode::Constant:
		BuildLookupTable(LookupTable, ERawDistributionOperation::None, 3, 1, 0.0f, 0.0f,
			[this](float, float* Out) { Out[0] = Constant.X; Out[1] = Constant.Y; Out[2] = Constant.Z; });
		break;

	case EDistributionMode::Uniform:
		BuildLookupTable(LookupTable, ERawDistributionOperation::Random, 3, 1, 0.0f, 0.0f,
			[this](float, float* Out) {
				Out[0] = MinValue.X; Out[1] = MinValue.Y; Out[2] = MinValue.Z;
				Out[3] = MaxValue.X; Out[4] = MaxValue.Y; Out[5] = MaxValue.Z;
			});
		break;

	case EDistributionMode::Curve:
	{
		float MinTime = FLT_MAX, MaxTime = -FLT_MAX;
		for (const TArray<FFloatCurveKey>* Keys : { &CurveKeysX, &CurveKeysY, &CurveKeysZ })
		{
			if (Keys->IsEmpty()) continue;

			MinTime = FMath::Min(MinTime, (*Keys)[0].Time);
			MaxTime = FMath::Max(MaxTime, Keys->Last().Time);
		}

		if (MinTime > MaxTime)
		{
			MinTime = MaxTime = 0.0f;
		}

		const int32 EntryCount = (MaxTime > MinTime) ? CurveBakeSampleCount : 1;

		BuildLookupTable(LookupTable, ERawDistributionOperation::None, 3, EntryCount, MinTime, MaxTime,
			[this](float Time, float* Out) {
				Out[0] = CurveKeyUtils::Evaluate(CurveKeysX, Time);
				Out[1] = CurveKeyUtils::Evaluate(CurveKeysY, Time);
				Out[2] = CurveKeyUtils::Evaluate(CurveKeysZ, Time);
			});
		break;
	}
	}
}

FVector FVectorDistribution::GetVectorValue(float InTime, FRandomStream& InRandom) const
{
	if (LookupTable.IsEmpty()) return FVector::ZeroVector;
	const float* Entry1 = nullptr;
	const float* Entry2 = nullptr;
	float Alpha = 0.0f;
	LookupTable.GetEntry(InTime, Entry1, Entry2, Alpha);
	FVector Value(
		FMath::Lerp(Entry1[0], Entry2[0], Alpha),
		FMath::Lerp(Entry1[1], Entry2[1], Alpha),
		FMath::Lerp(Entry1[2], Entry2[2], Alpha));
	if (LookupTable.Op == ERawDistributionOperation::Random)
	{
		const int32 MaxOffset = LookupTable.SubEntryStride;
		FVector MaxValueAtTime(
			FMath::Lerp(Entry1[MaxOffset + 0], Entry2[MaxOffset + 0], Alpha),
			FMath::Lerp(Entry1[MaxOffset + 1], Entry2[MaxOffset + 1], Alpha),
			FMath::Lerp(Entry1[MaxOffset + 2], Entry2[MaxOffset + 2], Alpha));
		
		const float RandX = InRandom.GetFraction();
		const float RandY = bLockAxes ? RandX : InRandom.GetFraction();
		const float RandZ = bLockAxes ? RandX : InRandom.GetFraction();
		return Value + (MaxValueAtTime - Value) * FVector(RandX, RandY, RandZ);
	}
	return Value;
}