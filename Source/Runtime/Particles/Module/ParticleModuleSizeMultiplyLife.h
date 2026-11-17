#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleSizeMultiplyLife.generated.h"

// 파티클 수명(RelativeTime 0~1)에 따른 배율을 크기에 곱한다. 크기는 매 프레임 BaseSize로 복원된 뒤 곱해진다
UCLASS()
class UParticleModuleSizeMultiplyLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSizeMultiplyLife();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	void BakeDistributions() override;

private:
	void Apply(FParticleEmitterInstance& Owner, FBaseParticle& Particle) const;

	UPROPERTY(EditAnywhere)
	FVectorDistribution LifeMultiplier;

	UPROPERTY(EditAnywhere)
	bool bMultiplyX = true;
	UPROPERTY(EditAnywhere)
	bool bMultiplyY = true;
	UPROPERTY(EditAnywhere)
	bool bMultiplyZ = true;
};
