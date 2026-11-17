#pragma once
#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include <algorithm>
#include <cmath>
#include "ParticleModuleBeamNoise.generated.h"

// 빔을 따라 놓은 노이즈 점을 축에 수직인 방향으로 흔든다. 경로를 만드는 건 렌더 쪽이다
UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleBeamNoise();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	// Frequency가 바뀌면 크기가 바뀌므로 에미터를 다시 빌드해야 한다
	uint32 RequiredBytes() const override;
	EModuleType GetModuleType() const override { return EPMT_Beam; }

	int32 GetFrequency() const { return std::clamp(Frequency, 1, 250); }
	bool IsSmooth() const { return bSmooth; }
	int32 GetNoiseTessellation() const { return std::clamp(NoiseTessellation, 1, 16); }
	float GetNoiseRange() const { return std::abs(NoiseRange); }

	static FBeamNoiseHeader& GetHeader(uint8* ParticleBase, int32 Offset);
	static FBeamNoisePoint* GetPoints(uint8* ParticleBase, int32 Offset);
	static const FBeamNoisePoint* GetPoints(const uint8* ParticleBase, int32 Offset);

private:
	void PickTargets(FParticleEmitterInstance& Owner, FBeamNoisePoint* Points) const;

	UPROPERTY(EditAnywhere)
	int32 Frequency = 8;           // 꺾이는 지점 수. 노이즈 점은 양 끝 포함 Frequency + 1개

	UPROPERTY(EditAnywhere)
	float NoiseRange = 20.0f;      // 빔 옆으로 최대 ± 이만큼

	UPROPERTY(EditAnywhere)
	float NoiseLockTime = 0.08f;   // 새 목표를 뽑는 간격(초). 0이면 매 프레임

	UPROPERTY(EditAnywhere)
	float NoiseSpeed = 0.0f;       // 목표로 가는 속도(단위/초). 0이면 바로 이동

	UPROPERTY(EditAnywhere)
	bool bSmooth = true;           // 노이즈 점 사이를 곡선으로 잇는다

	UPROPERTY(EditAnywhere)
	int32 NoiseTessellation = 4;   // bSmooth일 때 노이즈 점 사이를 몇 칸으로 나눌지
};
