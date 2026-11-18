#pragma once

#include "Core/Types.h"
#include "ParticleModuleEventReceiverBase.h"
#include "ParticleModuleEventReceiverKillAll.generated.h"

// 이벤트를 받으면 이 에미터의 파티클을 모두 지운다 (보호막이 맞으면 빛 효과를 즉시 끄기)
UCLASS()
class UParticleModuleEventReceiverKillAll : public UParticleModuleEventReceiverBase
{
	GENERATED_BODY()
public:
	void ProcessEvent(FParticleEmitterInstance& Owner, const FParticleEventData& Event) override;

private:
	// true면 지운 뒤 이 에미터의 생성도 멈춘다 (컴포넌트를 다시 활성화하면 재개)
	UPROPERTY(EditAnywhere)
	bool bStopSpawning = false;
};
