#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleSpawn.generated.h"

struct FRandomStream;

// 정해진 시간에 한 번에 터뜨리는 생성
USTRUCT()
struct FParticleBurst
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	int32 Count = 0;      // 생성 개수
	UPROPERTY(EditAnywhere)
	int32 CountLow = -1;  // -1이면 Count 고정, 아니면 [CountLow, Count] 랜덤
	UPROPERTY(EditAnywhere)
	float Time = 0.0f;    // 루프 안 시간
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSpawn();

	EModuleType GetModuleType() const override { return EPMT_Spawn; }
	void BakeDistributions() override;

	float GetSpawnRate(float EmitterTime, FRandomStream& Random) const;
	int32 GetBurstCount(int32 BurstIndex, FRandomStream& Random) const;

	FFloatDistribution& GetRate() { return Rate; }
	FFloatDistribution& GetRateScale() { return RateScale; }

	const TArray<FParticleBurst>& GetBurstList() const { return BurstList; }
	void AddBurst(const FParticleBurst& InBurst) { BurstList.Add(InBurst); }
	void ClearBursts() { BurstList.Empty(); }

private:
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution Rate;        // 초당 생성 수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution RateScale;   // Rate에 곱하는 배율
	UPROPERTY(EditAnywhere)
	TArray<FParticleBurst> BurstList;
};
