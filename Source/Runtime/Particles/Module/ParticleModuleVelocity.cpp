#include "EnginePCH.h"
#include "ParticleModuleVelocity.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleVelocity::UParticleModuleVelocity()
{
	bSpawnModule = true;

	StartVelocity.Mode = EDistributionMode::Uniform;
	StartVelocity.MinValue = FVector(-20.0f, -20.0f, 80.0f);
	StartVelocity.MaxValue = FVector(20.0f, 20.0f, 120.0f);

	StartVelocityRadial.Mode = EDistributionMode::Constant;
	StartVelocityRadial.Constant = 0.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleVelocity::BakeDistributions()
{
	StartVelocity.Bake();
	StartVelocityRadial.Bake();
}

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	FVector Vel = Owner.EmitterToSimulationVector(StartVelocity.GetVectorValue(Owner.EmitterTime, Owner.RandomStream));

	const FVector EmitterOrigin = Owner.GetEmitterOrigin();
	const FVector FromOrigin = (Particle.Location - EmitterOrigin).GetSafeNormal();
	Vel += FromOrigin * StartVelocityRadial.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);

	// Reset이 매 프레임 BaseVelocity → Velocity로 복원하므로 둘 다
	Particle.Velocity += Vel;
	Particle.BaseVelocity += Vel;
}
