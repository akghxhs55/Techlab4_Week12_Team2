#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include <bit> 
#include <random>


// 난수 생성기. 시드가 같으면 같은 난수를 생성한다.
struct FRandomStream
{
    explicit FRandomStream(int32 InSeed = 0) : InitialSeed(InSeed), Seed(uint32(InSeed)) {}
	void Initialize(int32 InSeed) { InitialSeed = InSeed; Seed = uint32(InSeed); }
	void GenerateNewSeed() { Initialize(static_cast<int32>(std::random_device{}())); }
    void  Reset() { Seed = uint32(InitialSeed); }
    float GetFraction() const
    {
        MutateSeed();
        return std::bit_cast<float>(0x3F800000U | (Seed >> 9)) - 1.0f;
    }
    float FRandRange(float Min, float Max) const { return Min + (Max - Min) * GetFraction(); }

	int32 RandRange(int32 Min, int32 Max) const { return Min + static_cast<int32>(GetFraction() * (Max - Min + 1)); }

	FVector VRand() const
	{
		const float Z = FRandRange(-1.0f, 1.0f);
		const float T = FRandRange(0.0f, 2.0f * PI);
		const float R = std::sqrt(1.0f - Z * Z);
		return FVector(R * std::cos(T), R * std::sin(T), Z);
	}

	FVector FRandRange(const FVector& Min, const FVector& Max) const
	{
		return FVector(FRandRange(Min.X, Max.X), FRandRange(Min.Y, Max.Y), FRandRange(Min.Z, Max.Z));
	}
private:
    void MutateSeed() const { Seed = (Seed * 196314165U) + 907633515U; }
    int32  InitialSeed;
    mutable uint32 Seed;
};