#pragma once

#include "Core/Types.h"
#include "Containers/Containers.h"
#include "Math/EngineMath.h"
#include "Curves/CurveFloat.h"
#include "Distributions.generated.h"

UENUM()
enum class EDistributionMode : uint8
{
	Constant,
	Uniform,
	Curve,
};

// 칸 안의 숫자들의 해석 방식에 대한 연산을 정의하는 열거형
enum class ERawDistributionOperation : uint8
{
	// 숫자가 없거나 초기화되지 않은 상태
	Uninitialized,
	// 값을 그대로 반환
	None,
	// Min~Max 사이의 값을 랜덤하게 반환
	Random,
	// Min이나 Max 중 하나를 랜덤하게 반환
	Extreme,
};

struct FDistributionLookupTable
{
	float TimeScale;
	float TimeBias;
	TArray<float> Values;
	ERawDistributionOperation Op;
	uint8 EntryCount;
	uint8 EntryStride;
	uint8 SubEntryStride;
	uint8 LockFlag;

	FDistributionLookupTable()
		: TimeScale(0.0f)
		, TimeBias(0.0f)
		, Op(ERawDistributionOperation::Uninitialized)
		, EntryCount(0)
		, EntryStride(0)
		, SubEntryStride(0)
		, LockFlag(0)
	{
	}

	void Empty()
	{
		Values.Empty();
		Op = ERawDistributionOperation::Uninitialized;
		EntryCount = 0;
		EntryStride = 0;
		SubEntryStride = 0;
		TimeScale = 0.0f;
		TimeBias = 0.0f;
		LockFlag = 0;
	}
	inline bool IsEmpty() const
	{
		return Values.Num() == 0 || EntryCount == 0;
	}
	inline int32 GetValuesPerEntry() const
	{
		return static_cast<int32>(EntryStride - SubEntryStride);
	}
	inline int32 GetValueCount() const
	{
		return static_cast<int32>(Values.Num());
	}
	inline void GetEntry(float Time, const float*& Entry1, const float*& Entry2, float& LerpAlpha) const
	{
		Time -= TimeBias;
		Time *= TimeScale;
		Time = Time >= 0.f ? Time : 0.f;

		LerpAlpha = Time - FMath::TruncToFloat(Time);

		const uint32 Index = FMath::TruncToInt(Time);
		const uint32 Index1 = FMath::Min<uint32>(Index + 0, EntryCount - 1) * EntryStride;
		const uint32 Index2 = FMath::Min<uint32>(Index + 1, EntryCount - 1) * EntryStride;
		Entry1 = &Values[Index1];
		Entry2 = &Values[Index2];
	}
	void GetRange(float* OutMinValues, float* OutMaxValues)
	{
		if (EntryCount > 0)
		{
			const int32 ValuesPerEntry = GetValuesPerEntry();
			const float* Entry = Values.GetData();

			// Initialize to the first entry in the table.
			for (int32 ValueIndex = 0; ValueIndex < ValuesPerEntry; ++ValueIndex)
			{
				OutMinValues[ValueIndex] = Entry[ValueIndex];
				OutMaxValues[ValueIndex] = Entry[ValueIndex + SubEntryStride];
			}

			// Iterate over each entry updating the minimum and maximum values.
			for (int32 EntryIndex = 1; EntryIndex < EntryCount; ++EntryIndex)
			{
				Entry += EntryStride;
				for (int32 ValueIndex = 0; ValueIndex < ValuesPerEntry; ++ValueIndex)
				{
					OutMinValues[ValueIndex] = FMath::Min(OutMinValues[ValueIndex], Entry[ValueIndex]);
					OutMaxValues[ValueIndex] = FMath::Max(OutMaxValues[ValueIndex], Entry[ValueIndex + SubEntryStride]);
				}
			}
		}
	}
};

struct FRandomStream;

USTRUCT()
struct FFloatDistribution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EDistributionMode Mode = EDistributionMode::Constant;
	UPROPERTY(EditAnywhere)
	float MinValue = 0.0f;
	UPROPERTY(EditAnywhere)
	float MaxValue = 0.0f;
	UPROPERTY(EditAnywhere)
	float Constant = 0.0f;
	UPROPERTY()  TArray<FFloatCurveKey> CurveKeys;
	FDistributionLookupTable LookupTable;

	void Bake();
	float GetFloatValue(float InTime, FRandomStream& InRandom) const;
};

USTRUCT()
struct FVectorDistribution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) EDistributionMode Mode = EDistributionMode::Constant;
	UPROPERTY(EditAnywhere) FVector MinValue;
	UPROPERTY(EditAnywhere) FVector MaxValue;
	UPROPERTY(EditAnywhere) FVector Constant;
	UPROPERTY() TArray<FFloatCurveKey> CurveKeysX;
	UPROPERTY() TArray<FFloatCurveKey> CurveKeysY;
	UPROPERTY() TArray<FFloatCurveKey> CurveKeysZ;
	FDistributionLookupTable LookupTable;
	UPROPERTY(EditAnywhere) bool bLockAxes = false;
	void Bake();
	FVector GetVectorValue(float InTime, FRandomStream& InRandom) const;
};
