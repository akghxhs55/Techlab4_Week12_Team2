#pragma once

#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "ParticleSystemActor.generated.h"

UCLASS()
class AParticleSystemActor : public AActor
{
	GENERATED_BODY()
public:
	AParticleSystemActor();

	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;   // 에디터에서 클릭 선택용
};
