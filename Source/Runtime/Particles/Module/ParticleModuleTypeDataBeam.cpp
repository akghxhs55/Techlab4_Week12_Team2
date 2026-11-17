#include "EnginePCH.h"
#include "ParticleModuleTypeDataBeam.h"
#include "Particles/ParticleBeamEmitterInstance.h"

FParticleEmitterInstance* UParticleModuleTypeDataBeam::CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	FParticleBeamEmitterInstance* Instance = new FParticleBeamEmitterInstance();
	Instance->Init(InEmitter, InComponent);
	return Instance;
}
