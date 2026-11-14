#include "EnginePCH.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"

void UParticleLODLevel::UpdateModuleLists()
{
	SpawnModules.Empty();
	UpdateModules.Empty();

	for (UParticleModule* Module : Modules)
	{
		// 비활성 모듈도 목록에 둔다. bEnabled는 실행 루프에서 검사한다 (UE와 동일)
		if (!Module)
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