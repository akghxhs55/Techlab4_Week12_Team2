#include "EnginePCH.h"
#include "ParticleModuleColorOverLife.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	bSpawnModule = true;
	bUpdateModule = true;

	ColorOverLife.Mode = EDistributionMode::Constant;
	ColorOverLife.Constant = FVector(1.0f, 1.0f, 1.0f);

	AlphaOverLife.Mode = EDistributionMode::Constant;
	AlphaOverLife.Constant = 1.0f;

	BakeDistributions();
}

void UParticleModuleColorOverLife::BakeDistributions()
{
	ColorOverLife.Bake();
	AlphaOverLife.Bake();
}

FLinearColor UParticleModuleColorOverLife::Evaluate(FParticleEmitterInstance& Owner, float RelativeTime) const
{
	const FVector Rgb = ColorOverLife.GetVectorValue(RelativeTime, Owner.RandomStream);
	float Alpha = AlphaOverLife.GetFloatValue(RelativeTime, Owner.RandomStream);
	if (bClampAlpha)
	{
		Alpha = Alpha < 0.0f ? 0.0f : (Alpha > 1.0f ? 1.0f : Alpha);
	}
	return FLinearColor(Rgb.X, Rgb.Y, Rgb.Z, Alpha);
}

void UParticleModuleColorOverLife::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	Particle.Color = Evaluate(Owner, Particle.RelativeTime);
	Particle.BaseColor = Particle.Color;
}

void UParticleModuleColorOverLife::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle& Particle, uint8*)
		{
			Particle.Color = Evaluate(Owner, Particle.RelativeTime);
		});
}
