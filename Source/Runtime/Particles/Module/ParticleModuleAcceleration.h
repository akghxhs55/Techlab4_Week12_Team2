#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleAcceleration.generated.h"

// 파티클마다 가속도를 하나 뽑아 두고 매 프레임 속도에 더한다 (중력, 바람)
UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleAcceleration();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	void BakeDistributions() override;

	// 뽑은 가속도(시뮬레이션 공간)를 파티클마다 저장
	uint32 RequiredBytes() const override { return sizeof(FVector); }

	FVectorDistribution& GetAcceleration() { return Acceleration; }

private:
	UPROPERTY(EditAnywhere)
	FVectorDistribution Acceleration;

	// true면 Acceleration을 월드 방향으로 본다 (중력처럼 컴포넌트가 회전해도 항상 아래).
	// false면 에미터 기준 방향이라 컴포넌트를 따라 회전한다
	UPROPERTY(EditAnywhere)
	bool bAlwaysInWorldSpace = true;
};
