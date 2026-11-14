#pragma once

#include "BaseParticle.h"
#include "ParticleModule.h"
#include "ParticleModuleLifetime.generated.h"

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleLifetime() { bSpawnModule = true; }

	float MinLifetime = 1.0f;
	float MaxLifetime = 1.0f;

	float GetMaxLifetime() const { return std::max(MinLifetime, MaxLifetime); }

	virtual void Spawn(const FSpawnContext& Context) override
	{
		const float T = FMath::Lerp(MinLifetime, MaxLifetime, rand() / static_cast<float>(RAND_MAX));
		Context.ParticleBase->OneOverMaxLifeTime = T > 0.0f ? 1.0f / T : 0.0f;
	}
};
