#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"
#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "ParticleTypes.h"

#include <malloc.h>
#include <cstring>

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	// ParticleData는 _aligned_free로 해제해야 함
	if (ParticleData)
	{
		_aligned_free(ParticleData);
		ParticleData = nullptr;
	}
	if (ParticleIndices)
	{
		delete[] ParticleIndices;
		ParticleIndices = nullptr;
	}
}

FBaseParticle* FParticleEmitterInstance::GetParticle(int32 SlotIndex) const
{
	return reinterpret_cast<FBaseParticle*>(ParticleData + SlotIndex * ParticleStride);
}

void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
	CurrentLODLevel = InTemplate->GetLODLevel(0);
	ParticleSize = InTemplate->GetParticleSize();
	assert(ParticleSize >= sizeof(FBaseParticle));
	
	ParticleStride = (ParticleSize + 15) & ~15; // 16바이트 정렬
	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	EmitterTime = 0.0f;
	SecondsSinceCreation = 0.0f;
	BoundingBox = FBox(FVector(FLT_MAX), FVector(-FLT_MAX));
	RandomStream.GenerateNewSeed();
	Resize(64);
}

bool FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles)
{
	if (NewMaxActiveParticles <= MaxActiveParticles) 
		return false;

	uint8* temp = reinterpret_cast<uint8*>(_aligned_realloc(ParticleData, NewMaxActiveParticles * ParticleStride, 16));
	if (temp == nullptr)
		return false;
	ParticleData = temp;

	for(int32 i = MaxActiveParticles; i < NewMaxActiveParticles; i++)
	{
		ParticleIndices[i] = i;
	}
	MaxActiveParticles = NewMaxActiveParticles;
	assert(NewMaxActiveParticles <= 65535); // uint16 범위
	return true;
}

void FParticleEmitterInstance::KillParticles()
{
	for (int32 i = ActiveParticles - 1; i >= 0; i--)
	{
		uint16 SlotIndex = ParticleIndices[i];
		FBaseParticle* Particle = GetParticle(SlotIndex);
		if (Particle->RelativeTime > 1.0f)
		{
			uint16 temp = ParticleIndices[i];
			ParticleIndices[i] = ParticleIndices[ActiveParticles - 1];
			ParticleIndices[ActiveParticles - 1] = temp;
			ActiveParticles--;
		}
	}
}

void FParticleEmitterInstance::ResetParticleParameters(float DeltaTime)
{
	for (int32 i = 0; i < ActiveParticles; i++)
	{
		uint16 SlotIndex = ParticleIndices[i];
		FBaseParticle* Particle = GetParticle(SlotIndex);
		Particle->OldLocation = Particle->Location;
		Particle->Velocity = Particle->BaseVelocity;
		Particle->Size = Particle->BaseSize;
		Particle->Color = Particle->BaseColor;
		Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifetime;
	}
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation)
{
	memset(Particle, 0, ParticleStride);
	Particle->OldLocation = InitialLocation;
	Particle->Location = InitialLocation;
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	Particle->OldLocation = Particle->Location;
	Particle->Location += Particle->Velocity * SpawnTime;
	Particle->Flags |= (ParticleCounter++ & STATE_CounterMask);
	Particle->Flags |= STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment)
{
	for (int32 i = 0; i < Count; i++)
	{
		uint16 SlotIndex = ParticleIndices[ActiveParticles];
		FBaseParticle* Particle = GetParticle(SlotIndex);
		float SpawnTime = StartTime - Increment * i;

		PreSpawn(Particle, FVector::ZeroVector);
		for (UParticleModule* ParticleModule : CurrentLODLevel->GetSpawnModules())
		{
			ParticleModule->Spawn(*this, SpriteTemplate->GetModuleOffset(ParticleModule), SpawnTime, *Particle);
		}
		PostSpawn(Particle, SpawnTime);
		ActiveParticles++;
	}
}

void FParticleEmitterInstance::Spawn(float DeltaTime)
{
	float OldLeftover = SpawnFraction;
	float Rate = 20.0f; // 초당 생성 수
	float NewLeftover = OldLeftover + DeltaTime * Rate;
	int32 Number = FMath::FloorToInt(NewLeftover);
	float Increment = 1.0f / Rate;
	float StartTime = DeltaTime + OldLeftover * Increment - Increment;
	SpawnFraction = NewLeftover - Number;
	if (ActiveParticles + Number > MaxActiveParticles)
	{
		if (!Resize((ActiveParticles + Number) * 1.5f))
		{
			Number = MaxActiveParticles - ActiveParticles;
		}
	}
	if (Number > 0)
	{
		SpawnParticles(Number, StartTime, Increment);
	}
}

void FParticleEmitterInstance::UpdateBoundingBox(float DeltaTime)
{
	FBox NewBox(FVector(FLT_MAX), FVector(-FLT_MAX));
	for (int32 i = 0; i < ActiveParticles; i++)
	{
		uint16 SlotIndex = ParticleIndices[i];
		FBaseParticle* Particle = GetParticle(SlotIndex);
		Particle->OldLocation = Particle->Location;
		Particle->Location += Particle->Velocity * DeltaTime;
		NewBox.Expand(FBox(Particle->Location, Particle->Location));
		Particle->Flags &= ~STATE_Particle_JustSpawned;
	}
	BoundingBox = NewBox;
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (SpriteTemplate == nullptr || CurrentLODLevel == nullptr)
		return;

	SecondsSinceCreation += DeltaTime;
	EmitterTime = SecondsSinceCreation;

	KillParticles();
	ResetParticleParameters(DeltaTime);
	for(UParticleModule* ParticleModule : CurrentLODLevel->GetUpdateModules())
	{
		ParticleModule->Update(*this, SpriteTemplate->GetModuleOffset(ParticleModule), DeltaTime);
	}
	Spawn(DeltaTime);
	UpdateBoundingBox(DeltaTime);
}