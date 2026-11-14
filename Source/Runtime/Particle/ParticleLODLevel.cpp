#include "EnginePCH.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"

void UParticleLODLevel::UpdateModuleLists()
{
	SpawnModules.Empty();
	UpdateModules.Empty();

	for (UParticleModule* Module : Modules)
	{
		if (!Module || !Module->IsEnabled())
			continue;

		if (Module->IsSpawnModule())  SpawnModules.Add(Module);
		if (Module->IsUpdateModule()) UpdateModules.Add(Module); 
	}
}

void UParticleLODLevel::AddModule(UParticleModule* InModule)
{
	if (!InModule || Modules.Contains(InModule))
		return;
	Modules.Add(InModule);
	UpdateModuleLists();
}

void UParticleLODLevel::RemoveModule(UParticleModule* InModule)
{
	if (!InModule)
		return;
	Modules.Remove(InModule);
	UpdateModuleLists();
}