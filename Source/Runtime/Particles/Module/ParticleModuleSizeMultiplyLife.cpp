#include "EnginePCH.h"
#include "ParticleModuleSizeMultiplyLife.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"

UParticleModuleSizeMultiplyLife::UParticleModuleSizeMultiplyLife()
{
	bSpawnModule = true;
	bUpdateModule = true;

	LifeMultiplier.Mode = EDistributionMode::Constant;
	LifeMultiplier.Constant = FVector(1.0f, 1.0f, 1.0f);
	LifeMultiplier.bLockAxes = true;

	BakeDistributions();
}

void UParticleModuleSizeMultiplyLife::BakeDistributions()
{
	LifeMultiplier.Bake();
}

void UParticleModuleSizeMultiplyLife::Apply(FParticleEmitterInstance& Owner, FBaseParticle& Particle) const
{
	const FVector Scale = LifeMultiplier.GetVectorValue(Particle.RelativeTime, Owner.RandomStream);
	if (bMultiplyX) Particle.Size.X *= Scale.X;
	if (bMultiplyY) Particle.Size.Y *= Scale.Y;
	if (bMultiplyZ) Particle.Size.Z *= Scale.Z;
}

void UParticleModuleSizeMultiplyLife::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	Apply(Owner, Particle);
}

void UParticleModuleSizeMultiplyLife::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle& Particle, uint8*)
		{
			Apply(Owner, Particle);
		});
}
