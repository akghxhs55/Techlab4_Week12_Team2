#pragma once

#include "BaseParticle.h"
#include "ParticleModule.h"
#include "ParticleModuleVelocity.generated.h"

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleVelocity() { bSpawnModule = true; }

	virtual void Spawn(const FSpawnContext& Context) override
	{
		FBaseParticle* Particle = Context.ParticleBase;
		Particle->Velocity += StartVelocity;
		Particle->BaseVelocity += StartVelocity;
	}

	FVector StartVelocity = FVector{ 0.0f, 0.0f, 1.0f };
};
