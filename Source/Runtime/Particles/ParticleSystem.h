#pragma once

#include "ParticleSystem.generated.h"

class UParticleEmitter;

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()
public:
	TArray<UParticleEmitter*> Emitters;
};
