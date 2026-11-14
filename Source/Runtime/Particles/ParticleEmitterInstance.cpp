#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"

#include "BaseParticle.h"
#include "ParticleLODLevel.h"
#include "ParticleModuleSpawn.h"
#include "ParticleSystemComponent.h"
#include "Core/HAL/UnrealMemory.h"

FParticleEmitterInstance::FParticleEmitterInstance(UParticleSystemComponent& InComponent)
	: Component(InComponent)
{}

void FParticleEmitterInstance::InitParameters(UParticleEmitter* InEmitter)
{
	SpriteTemplate = InEmitter;
}

void FParticleEmitterInstance::Init()
{
	ParticleSize = sizeof(FBaseParticle);
	PayloadOffset = ParticleSize;
	ParticleStride = ParticleSize;
}

void FParticleEmitterInstance::Tick(float DeltaTime, bool bSuppressSpawning)
{
	for (int32 i = ActiveParticles - 1; i >= 0; --i)
	{
		if (GetParticle(i)->RelativeTime >= 1.0f)
		{
			std::swap(ParticleIndices[i], ParticleIndices[ActiveParticles - 1]);
			--ActiveParticles;
		}
	}

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticle(i);
		Particle->Velocity = Particle->BaseVelocity;
		Particle->Size = Particle->BaseSize;
		Particle->RotationRate = Particle->BaseRotationRate;
		Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifeTime;
	}

	if (CurrentLODLevel)
	{
		const UParticleModule::FUpdateContext Context{
			.Owner = this,
			.Offset = 0,
			.DeltaTime = DeltaTime,
		};
		for (auto* Module : CurrentLODLevel->UpdateModules)
		{
			Module->Update(Context);
		}
	}

	if (!bSuppressSpawning && CurrentLODLevel && CurrentLODLevel->SpawnModule)
	{
		SpawnFraction += CurrentLODLevel->SpawnModule->Rate * DeltaTime;
		const int32 Count = static_cast<int32>(SpawnFraction);
		SpawnFraction -= static_cast<float>(Count);
		if (Count > 0)
		{
			SpawnParticles(Count);
		}
	}

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticle(i);
		Particle->Location += Particle->Velocity * DeltaTime;
	}
	EmitterTime += DeltaTime;
}

bool FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles)
{
	if (NewMaxActiveParticles > 65535)
	{
		return false;
	}

	ParticleData.SetNum(NewMaxActiveParticles * ParticleStride);
	ParticleIndices.SetNum(NewMaxActiveParticles);
	for (int32 i = MaxActiveParticles; i < NewMaxActiveParticles; ++i)
	{
		ParticleIndices[i] = static_cast<uint16>(i);
	}

	MaxActiveParticles = NewMaxActiveParticles;

	return true;
}

FBaseParticle* FParticleEmitterInstance::GetParticle(int32 Index)
{
	return reinterpret_cast<FBaseParticle*>(ParticleData.GetData() + ParticleStride * ParticleIndices[Index]);
}

void FParticleEmitterInstance::SpawnParticles(int32 Count)
{
	if (ActiveParticles + Count > MaxActiveParticles)
	{
		const int32 Needed = ActiveParticles + Count;
		const int32 NewMax = std::min(std::max(Needed, MaxActiveParticles * 2), 65535);
		Resize(NewMax);
		Count = std::min(Count, MaxActiveParticles - ActiveParticles);
	}

	const FVector SpawnLocation = Component.GetWorldLocation();

	for (int32 i = 0; i < Count; ++i)
	{
		FBaseParticle* Particle = GetParticle(ActiveParticles);
		FMemory::Memzero(Particle, ParticleSize);

		Particle->Location = SpawnLocation;

		if (CurrentLODLevel)
		{
			const UParticleModule::FSpawnContext Context{
				.Owner = this,
				.Offset = 0,
				.SpawnTime = 0.0f,
				.ParticleBase = Particle,
			};
			for (auto* Module : CurrentLODLevel->SpawnModules)
			{
				Module->Spawn(Context);
			}

			if (Particle->OneOverMaxLifeTime <= 0.0f)
			{
				Particle->OneOverMaxLifeTime = 1.0f;
			}
		}

		++ActiveParticles;
	}
}
