#include "EnginePCH.h"
#include "ParticleModuleTypeDataRibbon.h"
#include "Particles/ParticleRibbonEmitterInstance.h"

FParticleEmitterInstance* UParticleModuleTypeDataRibbon::CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	FParticleRibbonEmitterInstance* Instance = new FParticleRibbonEmitterInstance();
	Instance->Init(InEmitter, InComponent);
	return Instance;
}