#pragma once

#include "GameFramework/Actor.h"
#include "Emitter.generated.h"

class UParticleSystemComponent;
class UBillboardComponent;

UCLASS()
class AEmitter : public AActor
{
	GENERATED_BODY()
public:
	AEmitter();

	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	UBillboardComponent* SpriteComponent = nullptr;
};
