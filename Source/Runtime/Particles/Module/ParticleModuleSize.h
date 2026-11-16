#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleSize.generated.h"

// 스프라이트는 Size.X = 가로, Size.Y = 세로
UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSize();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void BakeDistributions() override;

	FVectorDistribution& GetStartSize() { return StartSize; }

private:
	UPROPERTY(EditAnywhere) 
	FVectorDistribution StartSize;
};
