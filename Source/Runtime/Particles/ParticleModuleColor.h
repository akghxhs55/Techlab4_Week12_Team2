#pragma once

#include "BaseParticle.h"
#include "ParticleModule.h"
#include "ParticleModuleColor.generated.h"

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColor() { bSpawnModule = true; }

	virtual void Spawn(const FSpawnContext& Context) override
	{
		FBaseParticle* Particle = Context.ParticleBase;
		Particle->Color = FVector4{ StartColor, StartAlpha };
		Particle->BaseColor = Particle->Color;
	}

	FVector StartColor = FVector{ 1.0f, 1.0f, 1.0f };
	float StartAlpha = 1.0f;
};
