#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"

#include "BaseParticle.h"
#include "ParticleLODLevel.h"
#include "ParticleModuleLifetime.h"
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

	float MaxLifetime = 0.0f;
	float Rate = 0.0f;
	if (CurrentLODLevel)
	{
		if (CurrentLODLevel->SpawnModule)
		{
			Rate = CurrentLODLevel->SpawnModule->Rate;
		}
		for (auto* Module : CurrentLODLevel->SpawnModules)
		{
			if (auto* Lifetime = Cast<UParticleModuleLifetime>(Module))
			{
				MaxLifetime = FMath::Max(MaxLifetime, Lifetime->GetMaxLifetime());
			}
		}
	}

	const int32 Estimated = static_cast<int32>(std::ceil(Rate * MaxLifetime));
	Resize(std::clamp(Estimated, 1, 65535));
}

void FParticleEmitterInstance::Tick(float DeltaTime, bool bSuppressSpawning)
{
	const FVector Current = Component.GetWorldLocation();
	OldLocation = EmitterTime == 0.0f ? Current : Location;
	Location = Current;

	KillParticles();

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticle(i);
		Particle->Color = Particle->BaseColor;
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

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticle(i);
		Particle->Location += Particle->Velocity * DeltaTime;
	}

	if (!bSuppressSpawning && CurrentLODLevel && CurrentLODLevel->SpawnModule)
	{
		const float Rate = CurrentLODLevel->SpawnModule->Rate;
		const float OldLeftover = SpawnFraction;
		float NewLeftover = OldLeftover + DeltaTime * Rate;
		const int32 Number = static_cast<int32>(NewLeftover);
		const float Increment = Rate > 0.0f ? 1.0f / Rate : 0.0f;
		const float StartTime = DeltaTime + OldLeftover * Increment - Increment;
		SpawnFraction = NewLeftover - static_cast<float>(Number);

		if (Number > 0)
		{
			SpawnParticles(Number, StartTime, Increment, Location, FVector::ZeroVector);
		}
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

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	if (ActiveParticles + Count > MaxActiveParticles)
	{
		const int32 Needed = ActiveParticles + Count;
		const int32 NewMax = std::min(std::max(Needed, MaxActiveParticles * 2), 65535);
		Resize(NewMax);
		Count = std::min(Count, MaxActiveParticles - ActiveParticles);
	}

	float SpawnTime = StartTime;
	float Interp = 1.0f;
	const float InterpIncrement = (Count > 0 && Increment > 0.0f) ? 1.0f / static_cast<float>(Count) : 0.0f;

	for (int32 i = 0; i < Count; ++i)
	{
		FBaseParticle* Particle = GetParticle(ActiveParticles);

		FMemory::Memzero(Particle, ParticleStride);
		Particle->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		Particle->Color = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		Particle->Location = InitialLocation;
		Particle->BaseVelocity = InitialVelocity;
		Particle->Velocity = InitialVelocity;

		if (CurrentLODLevel)
		{
			const UParticleModule::FSpawnContext Context{
				.Owner = this,
				.Offset = 0,
				.SpawnTime = SpawnTime,
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

		Particle->Location += (OldLocation - Location) * Interp;
		Particle->Location += Particle->Velocity * SpawnTime;
		Particle->RelativeTime += SpawnTime * Particle->OneOverMaxLifeTime;

		++ActiveParticles;
		SpawnTime -= Increment;
		Interp -= InterpIncrement;
	}
}

void FParticleEmitterInstance::KillParticles()
{
	for (int32 i = ActiveParticles - 1; i >= 0; --i)
	{
		if (GetParticle(i)->RelativeTime >= 1.0f)
		{
			std::swap(ParticleIndices[i], ParticleIndices[ActiveParticles - 1]);
			--ActiveParticles;
		}
	}
}
