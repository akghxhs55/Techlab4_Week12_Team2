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
	FreeParticleMemory();
}

void FParticleEmitterInstance::FreeParticleMemory()
{
	// ParticleData는 _aligned_free로 해제해야 함
	if (ParticleData)
	{
		_aligned_free(ParticleData);
		ParticleData = nullptr;
	}
	if (ParticleIndices)
	{
		free(ParticleIndices);
		ParticleIndices = nullptr;
	}
	MaxActiveParticles = 0;
	ActiveParticles = 0;
}

FBaseParticle* FParticleEmitterInstance::GetParticle(int32 SlotIndex) const
{
	return reinterpret_cast<FBaseParticle*>(ParticleData + SlotIndex * ParticleStride);
}

void FParticleEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
	assert(InTemplate);
	// 재호출 시 Stride가 바뀔 수 있으므로 기존 블록을 버리고 새로 잡는다
	FreeParticleMemory();

	SpriteTemplate = InTemplate;
	Component = InComponent;
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
	assert(NewMaxActiveParticles <= 65535); // uint16 범위
	if (NewMaxActiveParticles <= MaxActiveParticles)
		return true;

	uint8* NewParticleData = reinterpret_cast<uint8*>(_aligned_realloc(ParticleData, NewMaxActiveParticles * ParticleStride, 16));
	if (NewParticleData == nullptr)
		return false;
	ParticleData = NewParticleData;

	uint16* NewParticleIndices = reinterpret_cast<uint16*>(realloc(ParticleIndices, sizeof(uint16) * (NewMaxActiveParticles + 1)));
	if (NewParticleIndices == nullptr)
		return false;
	ParticleIndices = NewParticleIndices;

	for(int32 i = MaxActiveParticles; i < NewMaxActiveParticles; i++)
	{
		ParticleIndices[i] = static_cast<uint16>(i);
	}
	MaxActiveParticles = NewMaxActiveParticles;
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
			uint16 DeadIndex = ParticleIndices[i];
			ParticleIndices[i] = ParticleIndices[ActiveParticles - 1];
			ParticleIndices[ActiveParticles - 1] = DeadIndex;
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
		Particle->Velocity = Particle->BaseVelocity;
		Particle->Size = Particle->BaseSize;
		Particle->RotationRate = Particle->BaseRotationRate;
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
	// TODO: Lifetime 모듈 구현 후 제거. 수명이 없으면 파티클이 죽지 않고 계속 쌓인다.
	if (Particle->OneOverMaxLifetime == 0.0f)
	{
		Particle->OneOverMaxLifetime = 0.5f; // 임시 수명 2초
	}

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
		for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
		{
			if (!Module->IsEnabled())
				continue;
			Module->Spawn(*this, SpriteTemplate->GetModuleOffset(Module), SpawnTime, *Particle);
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
		int32 NewMax = static_cast<int32>((ActiveParticles + Number) * 1.5f);
		if (NewMax > 65535)
		{
			NewMax = 65535;
		}
		if (!Resize(NewMax) || ActiveParticles + Number > MaxActiveParticles)
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
		Particle->Rotation += Particle->RotationRate * DeltaTime;
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
	for(UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (!Module->IsEnabled())
			continue;
		Module->Update(*this, SpriteTemplate->GetModuleOffset(Module), DeltaTime);
	}
	Spawn(DeltaTime);
	UpdateBoundingBox(DeltaTime);
}