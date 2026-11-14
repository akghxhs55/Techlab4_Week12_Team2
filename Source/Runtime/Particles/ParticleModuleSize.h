#pragma once

#include "BaseParticle.h"
#include "ParticleModule.h"
#include "ParticleModuleSize.generated.h"

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSize() { bSpawnModule = true; }

	virtual void Spawn(const FSpawnContext& Context) override
	{
		FBaseParticle* Particle = Context.ParticleBase;
		Particle->Size += StartSize;
		Particle->BaseSize += StartSize;
	}

	FVector StartSize = FVector{ 1.0f, 1.0f, 1.0f };
};
