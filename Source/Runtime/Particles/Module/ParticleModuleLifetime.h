#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleLifetime.generated.h"

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleLifetime();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void BakeDistributions() override;

	FFloatDistribution& GetLifetime() { return Lifetime; }

private:
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution Lifetime;   // 0 이하면 죽지 않는다
};
