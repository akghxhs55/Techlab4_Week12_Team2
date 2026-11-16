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

UParticleSystem::~UParticleSystem()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		delete Emitter;
	}
	Emitters.Reset();
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

void UParticleSystem::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 Version = Ar.IsSaving() ? 1 : 0;
	Record << SA_VALUE("Version", Version);
	if (Version != 1)
	{
		Ar.SetError();
		return;
	}

	int32 Count = Ar.IsSaving() ? Emitters.Num() : 0;
	auto EmitterArray = Record.EnterArray("Emitters", Count);

	if (Ar.IsLoading())
	{
		assert(Emitters.IsEmpty());
		Emitters.SetNum(Count);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		if (Ar.IsLoading())
		{
			Emitters[i] = NewObject<UParticleEmitter>(this);
		}

		Emitters[i]->Serialize(EmitterArray.EnterElement().EnterRecord());

		if (Ar.IsError())
		{
			break;
		}
	}
}
