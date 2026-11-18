#pragma once

#include "Core/Types.h"
#include "Particles/Distributions.h"
#include "ParticleModuleEventReceiverBase.h"
#include "ParticleModuleEventReceiverSpawn.generated.h"

// 이벤트를 받으면 이 에미터의 파티클을 이벤트 위치에 생성한다 (빗방울 → 물보라, 로켓 Death → 폭발).
// 받는 에미터는 보통 Spawn Rate를 0으로 두고 이벤트로만 생성한다
UCLASS()
class UParticleModuleEventReceiverSpawn : public UParticleModuleEventReceiverBase
{
	GENERATED_BODY()
public:
	UParticleModuleEventReceiverSpawn();

	void BakeDistributions() override;
	void ProcessEvent(FParticleEmitterInstance& Owner, const FParticleEventData& Event) override;

private:
	// 이벤트 하나당 생성 수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution SpawnCount;

	// true면 이벤트 위치 대신 에미터 위치에 생성
	UPROPERTY(EditAnywhere)
	bool bUseEmitterLocation = false;

	// true면 이벤트 속도 × InheritVelocityScale을 생성 속도에 더한다
	UPROPERTY(EditAnywhere)
	bool bInheritVelocity = false;

	UPROPERTY(EditAnywhere)
	float InheritVelocityScale = 1.0f;
};
