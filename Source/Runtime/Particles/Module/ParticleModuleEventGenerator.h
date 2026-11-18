#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Containers/Array.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleEventTypes.h"
#include "ParticleModuleEventGenerator.generated.h"

// 내보낼 이벤트 하나의 설정
USTRUCT()
struct FParticleEventGeneratorInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	EParticleEventType Type = EParticleEventType::Collision;

	// Receiver가 이 이름으로 골라 받는다
	UPROPERTY(EditAnywhere)
	FString CustomName;

	// N번째 사건마다 하나만 보낸다. 1 이하면 매번
	UPROPERTY(EditAnywhere, meta=(ClampMin=1))
	int32 Frequency = 1;

	// Collision 전용: 그 파티클의 첫 충돌만 / 마지막 충돌(MaxCollisions 도달)만
	UPROPERTY(EditAnywhere)
	bool bFirstTimeOnly = false;
	UPROPERTY(EditAnywhere)
	bool bLastTimeOnly = false;

	// 이벤트에 그대로 실어 보내는 값
	UPROPERTY(EditAnywhere)
	int32 CustomData = 0;
};

// 어떤 사건을 어떤 이름으로 내보낼지 정한다. 파티클을 직접 건드리지 않는다.
// 에미터 인스턴스가 Spawn/Kill/Collision/Burst를 처리할 때 이 설정을 보고 컴포넌트에 보고한다
UCLASS()
class UParticleModuleEventGenerator : public UParticleModule
{
	GENERATED_BODY()
public:
	EModuleType GetModuleType() const override { return EPMT_Event; }
	void BakeDistributions() override;

	bool HasEvent(EParticleEventType Type) const;
	const TArray<FParticleEventGeneratorInfo>& GetEvents() const { return Events; }
	// Events[Index]의 이름. Bake에서 FName으로 바꿔 둔다
	FName GetEventName(int32 Index) const { return EventNames.IsValidIndex(Index) ? EventNames[Index] : FName(); }

	void AddEvent(const FParticleEventGeneratorInfo& Info) { Events.Add(Info); BakeDistributions(); }

private:
	UPROPERTY(EditAnywhere)
	TArray<FParticleEventGeneratorInfo> Events;

	TArray<FName> EventNames;
};
