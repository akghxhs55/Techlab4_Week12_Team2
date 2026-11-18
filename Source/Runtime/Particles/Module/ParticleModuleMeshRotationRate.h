#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleMeshRotationRate.generated.h"

// 메시 파티클의 회전 속도. 메시 에미터가 아니면 아무것도 하지 않는다
UCLASS()
class UParticleModuleMeshRotationRate : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleMeshRotationRate();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void BakeDistributions() override;

	FVectorDistribution& GetStartRotationRate() { return StartRotationRate; }

private:
	// 도/초. X = Roll, Y = Pitch, Z = Yaw
	UPROPERTY(EditAnywhere)
	FVectorDistribution StartRotationRate;
};
