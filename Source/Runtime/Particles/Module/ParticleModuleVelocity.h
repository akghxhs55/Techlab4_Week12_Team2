#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleVelocity.generated.h"

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleVelocity();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void BakeDistributions() override;

	FVectorDistribution& GetStartVelocity() { return StartVelocity; }
	FFloatDistribution& GetStartVelocityRadial() { return StartVelocityRadial; }

private:
	UPROPERTY(EditAnywhere)
	FVectorDistribution StartVelocity;
	UPROPERTY(EditAnywhere)
	FFloatDistribution  StartVelocityRadial;  // 에미터 원점 → 파티클 방향으로 더하는 속도
};
