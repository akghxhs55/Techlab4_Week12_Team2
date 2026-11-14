#include "EnginePCH.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"

void UParticleSystem::BuildEmitters()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
			Emitter->Build();
	}
}

void UParticleSystem::AddEmitter(UParticleEmitter* InEmitter)
{
	if (InEmitter && !Emitters.Contains(InEmitter))
		Emitters.Add(InEmitter);
}

void UParticleSystem::RemoveEmitter(UParticleEmitter* InEmitter)
{
	if (InEmitter)
		Emitters.Remove(InEmitter);
}