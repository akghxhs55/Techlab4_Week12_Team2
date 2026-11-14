#include "EnginePCH.h"
#include "ParticleModuleLifetime.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleLifetime::UParticleModuleLifetime()
{
	bSpawnModule = true;
	Lifetime.Mode = EDistributionMode::Constant;
	Lifetime.Constant = 1.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleLifetime::BakeDistributions()
{
	Lifetime.Bake();
}

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const float MaxLifetime = Lifetime.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);

	Particle.OneOverMaxLifetime = MaxLifetime > 0.0f ? 1.0f / MaxLifetime : 0.0f;

	// 프레임 중간에 태어난 만큼 수명도 미리 진행
	Particle.RelativeTime = SpawnTime * Particle.OneOverMaxLifetime;
}
