#include "EnginePCH.h"
#include "ParticleModuleSize.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleSize::UParticleModuleSize()
{
	bSpawnModule = true;

	StartSize.Mode = EDistributionMode::Constant;
	StartSize.Constant = FVector(10.0f, 10.0f, 10.0f);
	// Uniform일 때 가로/세로가 따로 뽑혀 찌그러지지 않도록
	StartSize.bLockAxes = true;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleSize::BakeDistributions()
{
	StartSize.Bake();
}

void UParticleModuleSize::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const FVector Size = StartSize.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);

	// Reset이 매 프레임 BaseSize → Size로 복원하므로 둘 다
	Particle.Size += Size;
	Particle.BaseSize += Size;
}
