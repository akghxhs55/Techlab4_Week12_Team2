#pragma once

#include "ParticleModule.h"
#include "ParticleModuleSpawn.generated.h"

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()
public:
	float Rate = 20.0f;
};
