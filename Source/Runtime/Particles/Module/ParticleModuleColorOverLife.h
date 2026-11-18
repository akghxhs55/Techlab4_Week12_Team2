#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleColorOverLife.generated.h"

// 파티클 수명(RelativeTime 0~1)에 따라 색과 알파를 덮어쓴다
UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColorOverLife();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	void BakeDistributions() override;

private:
	FLinearColor Evaluate(FParticleEmitterInstance& Owner, float RelativeTime) const;

	UPROPERTY(EditAnywhere)
	FVectorDistribution ColorOverLife;   // RGB
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	FFloatDistribution  AlphaOverLife;   // A

	UPROPERTY(EditAnywhere)
	bool bClampAlpha = true;
};
