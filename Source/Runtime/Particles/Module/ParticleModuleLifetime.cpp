#include "EnginePCH.h"
#include "ParticleModuleLifetime.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleLifetime::UParticleModuleLifetime()
{
	bSpawnModule = true;
	Lifetime.Mode = EDistributionMode::Constant;
	Lifetime.Constant = 1.0f;

	// Build() 전에도 기본값이 동작하도록 한 번 굽는다
	BakeDistributions();
}

void UParticleModuleLifetime::BakeDistributions()
{
	Lifetime.Bake();
}

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const float MaxLifetime = Lifetime.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);

	// 0 이하면 OneOverMaxLifetime = 0 → RelativeTime이 늘지 않아 죽지 않는다
	Particle.OneOverMaxLifetime = MaxLifetime > 0.0f ? 1.0f / MaxLifetime : 0.0f;

	// 프레임 중간에 태어난 만큼 수명도 미리 진행 (UE 동일)
	Particle.RelativeTime = SpawnTime * Particle.OneOverMaxLifetime;
}
