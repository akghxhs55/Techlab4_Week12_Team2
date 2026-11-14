#pragma once

#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	void UpdateModuleLists();

	int32 Level = -1;
	bool bEnabled = false;

	UParticleModuleRequired* RequiredModule = nullptr;
	UParticleModuleSpawn* SpawnModule = nullptr;
	TArray<UParticleModule*> Modules;

	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;
};
