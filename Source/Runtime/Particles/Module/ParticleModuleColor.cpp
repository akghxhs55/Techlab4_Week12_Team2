#include "EnginePCH.h"
#include "ParticleModuleColor.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleColor::UParticleModuleColor()
{
	bSpawnModule = true;

	StartColor.Mode = EDistributionMode::Constant;
	StartColor.Constant = FVector(1.0f, 1.0f, 1.0f);

	StartAlpha.Mode = EDistributionMode::Constant;
	StartAlpha.Constant = 1.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleColor::BakeDistributions()
{
	StartColor.Bake();
	StartAlpha.Bake();
}

void UParticleModuleColor::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const FVector Rgb = StartColor.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);
	float Alpha = StartAlpha.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);
	if (bClampAlpha)
	{
		Alpha = Alpha < 0.0f ? 0.0f : (Alpha > 1.0f ? 1.0f : Alpha);
	}

	Particle.Color = FLinearColor(Rgb.X, Rgb.Y, Rgb.Z, Alpha);
	Particle.BaseColor = Particle.Color;
}
