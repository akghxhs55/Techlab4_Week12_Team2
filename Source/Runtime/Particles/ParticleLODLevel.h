#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	// Modules를 보고 SpawnModules, UpdateModules를 다시 만든다.
	void UpdateModuleLists();

	int32 GetLevel() const { return Level; }
	bool  IsEnabled() const { return bEnabled; }
	void  SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	UParticleModuleRequired* GetRequiredModule() const { return RequiredModule; }
	void SetRequiredModule(UParticleModuleRequired* InModule) { RequiredModule = InModule; }

	const TArray<UParticleModule*>& GetModules() const { return Modules; }
	void AddModule(UParticleModule* InModule);
	void RemoveModule(UParticleModule* InModule);

	// UpdateModuleLists()만 채운다. 바깥에서는 읽기만 한다.
	const TArray<UParticleModule*>& GetSpawnModules() const { return SpawnModules; }
	const TArray<UParticleModule*>& GetUpdateModules() const { return UpdateModules; }

private:
	int32 Level = 0;
	bool  bEnabled = true;
	UParticleModuleRequired* RequiredModule = nullptr;
	TArray<UParticleModule*> Modules;
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;
};