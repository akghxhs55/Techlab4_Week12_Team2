#include "EnginePCH.h"
#include "ParticleModuleEventReceiverKillAll.h"
#include "Particles/ParticleEmitterInstance.h"

void UParticleModuleEventReceiverKillAll::ProcessEvent(FParticleEmitterInstance& Owner, const FParticleEventData& Event)
{
	Owner.KillAllParticles();
	if (bStopSpawning)
	{
		Owner.bSpawningEnabled = false;
	}
}
