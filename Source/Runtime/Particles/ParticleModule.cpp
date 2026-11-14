#include "EnginePCH.h"
#include "ParticleModule.h"

void UParticleModule::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& ParticleBase)
{
}

void UParticleModule::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	BakeDistributions();
}

void UParticleModule::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
}
