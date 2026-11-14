#include "EnginePCH.h"
#include "ParticleLODLevel.h"

#include "ParticleModule.h"

void UParticleLODLevel::UpdateModuleLists()
{
	SpawnModules.Reset();
	UpdateModules.Reset();

	for (auto* Module : Modules)
	{
		if (!Module)
		{
			continue;
		}

		if (Module->bSpawnModule)
		{
			SpawnModules.Add(Module);
		}
		if (Module->bUpdateModule)
		{
			UpdateModules.Add(Module);
		}
	}
}
