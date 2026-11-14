#include "EnginePCH.h"
#include "ParticleModuleSpawn.h"
#include "Math/RandomStream.h"

UParticleModuleSpawn::UParticleModuleSpawn()
{
	Rate.Mode = EDistributionMode::Constant;
	Rate.Constant = 20.0f;
	RateScale.Mode = EDistributionMode::Constant;
	RateScale.Constant = 1.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleSpawn::BakeDistributions()
{
	Rate.Bake();
	RateScale.Bake();
}

float UParticleModuleSpawn::GetSpawnRate(float EmitterTime, FRandomStream& Random) const
{
	const float Value = Rate.GetFloatValue(EmitterTime, Random) * RateScale.GetFloatValue(EmitterTime, Random);
	return Value > 0.0f ? Value : 0.0f;
}

int32 UParticleModuleSpawn::GetBurstCount(int32 BurstIndex, FRandomStream& Random) const
{
	if (!BurstList.IsValidIndex(BurstIndex))
		return 0;

	const FParticleBurst& Burst = BurstList[BurstIndex];
	if (Burst.CountLow > -1)
	{
		return Random.RandRange(Burst.CountLow, Burst.Count);
	}
	return Burst.Count;
}
