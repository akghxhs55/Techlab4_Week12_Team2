#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleLifetime.generated.h"

// 파티클 수명 설정 (UE UParticleModuleLifetime)
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
	FFloatDistribution Lifetime;   // 수명. 0 이하면 죽지 않는다
};
