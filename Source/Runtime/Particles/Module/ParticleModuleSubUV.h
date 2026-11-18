#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "ParticleModuleSubUV.generated.h"

class UParticleModuleRequired;

struct FFullSubUVPayload
{
	float ImageIndex;        // 정수부 = 칸 번호, 소수부 = 다음 칸과 섞는 비율
	float RandomImageTime;   // 마지막으로 칸을 뽑은 RelativeTime
};

UCLASS()
class UParticleModuleSubUV : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSubUV();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	uint32 RequiredBytes() const override { return sizeof(FFullSubUVPayload); }
	void BakeDistributions() override;

	FFloatDistribution& GetSubImageIndex() { return SubImageIndex; }

private:
	float DetermineImageIndex(FParticleEmitterInstance& Owner, const UParticleModuleRequired& Required,
		const FBaseParticle& Particle, FFullSubUVPayload& Payload) const;

	// Linear 모드: RelativeTime(0~1) → 칸 번호
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution SubImageIndex;
};