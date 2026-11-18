#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleColor.generated.h"

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColor();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void BakeDistributions() override;

	FVectorDistribution& GetStartColor() { return StartColor; }
	FFloatDistribution& GetStartAlpha() { return StartAlpha; }

private:
	UPROPERTY(EditAnywhere)
	FVectorDistribution StartColor;   // RGB
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	FFloatDistribution  StartAlpha;   // A

	UPROPERTY(EditAnywhere)
	bool bClampAlpha = true;
};
