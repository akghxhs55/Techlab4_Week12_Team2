#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleEventTypes.h"
#include "ParticleModuleEventReceiverBase.generated.h"

struct FParticleEmitterInstance;

// 같은 컴포넌트 안의 에미터가 낸 이벤트를 받아 행동하는 모듈의 공통 부분.
// 종류와 이름으로 받을 이벤트를 고르고, 무엇을 할지는 하위 클래스가 ProcessEvent에서 정한다
UCLASS()
class UParticleModuleEventReceiverBase : public UParticleModule
{
	GENERATED_BODY()
public:
	EModuleType GetModuleType() const override { return EPMT_Event; }
	void BakeDistributions() override;

	// 종류(Any면 전부)와 이름(비어 있으면 전부)이 맞는지
	bool WillProcessEvent(const FParticleEventData& Event) const;

	// 배달 단계에서 이 모듈을 가진 에미터 인스턴스(Owner)로 부른다
	virtual void ProcessEvent(FParticleEmitterInstance& Owner, const FParticleEventData& Event) {}

	EParticleEventType GetEventGeneratorType() const { return EventGeneratorType; }
	const FString& GetEventName() const { return EventName; }

protected:
	UPROPERTY(EditAnywhere)
	EParticleEventType EventGeneratorType = EParticleEventType::Any;

	UPROPERTY(EditAnywhere)
	FString EventName;

	FName CachedEventName;
};
