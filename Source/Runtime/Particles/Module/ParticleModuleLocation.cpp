#include "EnginePCH.h"
#include "ParticleModuleLocation.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleLocation::UParticleModuleLocation()
{
	bSpawnModule = true;

	StartLocation.Mode = EDistributionMode::Uniform;
	StartLocation.MinValue = FVector(-20.0f, -20.0f, 0.0f);
	StartLocation.MaxValue = FVector(20.0f, 20.0f, 0.0f);

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleLocation::BakeDistributions()
{
	StartLocation.Bake();
}

void UParticleModuleLocation::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const FVector LocalOffset = StartLocation.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);
	Particle.Location += Owner.EmitterToSimulationVector(LocalOffset);
}
