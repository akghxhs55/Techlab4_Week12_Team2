#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"
#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "ParticleTypes.h"
#include "Module/ParticleModuleRequired.h"
#include "Module/ParticleModuleSpawn.h"
#include "Module/ParticleModuleSubUV.h"

#include "Components/ParticleSystemComponent.h"

#include "Engine/ParticleSystemSceneProxy.h"
#include "Engine/Scene.h"

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
	// 재호출 시 Stride가 바뀔 수 있으므로 새로 잡는다
	FreeParticleMemory();

	SpriteTemplate = InTemplate;
	Component = InComponent;
	CurrentLODLevel = InTemplate->GetLODLevel(0);
	ParticleSize = InTemplate->GetParticleSize();
	assert(ParticleSize >= sizeof(FBaseParticle));

	// 에미터는 모듈 Payload만 계산하므로 인스턴스 Payload는 그 뒤에 직접 붙인다
	const int32 InstancePayloadSize = GetInstancePayloadSize();
	InstancePayloadOffset = InstancePayloadSize > 0 ? ParticleSize : 0;
	ParticleSize += InstancePayloadSize;

	ParticleStride = (ParticleSize + 15) & ~15; // 16바이트 정렬
	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	SecondsSinceCreation = 0.0f;
	LoopCount = 0;
	bSpawningEnabled = true;
	BurstFired.Empty();

	// 음수인 동안(Delay)은 생성하지 않는다
	const UParticleModuleRequired* Required = CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr;
	EmitterTime = Required ? -Required->GetDelayForLoop(0) : 0.0f;

	BoundingBox = FBox(FVector(FLT_MAX), FVector(-FLT_MAX));
	RandomStream.GenerateNewSeed();
	SubUVDataOffset = 0;
	if (CurrentLODLevel)
	{
		for (UParticleModule* Module : CurrentLODLevel->GetModules())
		{
			if (Cast<UParticleModuleSubUV>(Module))
			{
				SubUVDataOffset = SpriteTemplate->GetModuleOffset(Module);
				break;
			}
		}
	}
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
	Particle->OldLocation = Particle->Location;
	Particle->Location += Particle->Velocity * SpawnTime;
	Particle->Flags |= (ParticleCounter++ & STATE_CounterMask);
	Particle->Flags |= STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment)
{
	const FVector Origin = GetEmitterOrigin();

	for (int32 i = 0; i < Count; i++)
	{
		uint16 SlotIndex = ParticleIndices[ActiveParticles];
		FBaseParticle* Particle = GetParticle(SlotIndex);
		float SpawnTime = StartTime - Increment * i;

		PreSpawn(Particle, Origin);
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

bool FParticleEmitterInstance::HasCompletedLoops() const
{
	const UParticleModuleRequired* Required = CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr;
	if (!Required)
		return false;
	const int32 Loops = Required->GetEmitterLoops();
	return Loops > 0 && LoopCount >= Loops;
}

bool FParticleEmitterInstance::IsLocalSpace() const
{
	const UParticleModuleRequired* Required = CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr;
	return Required && Required->IsUseLocalSpace();
}

FVector FParticleEmitterInstance::GetEmitterOrigin() const
{
	if (IsLocalSpace() || Component == nullptr)
	{
		return FVector::ZeroVector;
	}
	return Component->GetWorldLocation();
}

FVector FParticleEmitterInstance::SimulationToWorld(const FVector& SimulationPosition) const
{
	if (IsLocalSpace() && Component)
	{
		return Component->GetWorldMatrix().TransformPosition(SimulationPosition);
	}
	return SimulationPosition;
}

FVector FParticleEmitterInstance::EmitterToSimulationVector(const FVector& EmitterVector) const
{
	if (IsLocalSpace() || Component == nullptr)
	{
		return EmitterVector;
	}
	return Component->GetWorldMatrix().TransformVector(EmitterVector);
}

void FParticleEmitterInstance::UpdateEmitterTime(float DeltaTime)
{
	SecondsSinceCreation += DeltaTime;
	EmitterTime += DeltaTime;

	const UParticleModuleRequired* Required = CurrentLODLevel->GetRequiredModule();
	if (!Required)
		return;

	const float Duration = Required->GetEmitterDuration();
	if (Duration <= 0.0f)
		return;   // 루프 없음: EmitterTime이 계속 증가

	// 큰 DeltaTime에 루프를 여러 번 넘을 수 있으므로 while
	while (EmitterTime >= Duration && !HasCompletedLoops())
	{
		++LoopCount;
		EmitterTime -= Duration + Required->GetDelayForLoop(LoopCount);
		BurstFired.Init(0, BurstFired.Num());
	}
}

int32 FParticleEmitterInstance::ComputeBurstCount()
{
	const UParticleModuleSpawn* SpawnModule = CurrentLODLevel->GetSpawnModule();
	if (!SpawnModule)
		return 0;

	const TArray<FParticleBurst>& Bursts = SpawnModule->GetBurstList();
	if (BurstFired.Num() != Bursts.Num())
	{
		BurstFired.Init(0, Bursts.Num());
	}

	int32 Total = 0;
	for (int32 i = 0; i < Bursts.Num(); ++i)
	{
		if (!BurstFired[i] && EmitterTime >= Bursts[i].Time)
		{
			Total += SpawnModule->GetBurstCount(i, RandomStream);
			BurstFired[i] = 1;
		}
	}
	return Total;
}

void FParticleEmitterInstance::Spawn(float DeltaTime)
{
	if (!bSpawningEnabled || EmitterTime < 0.0f || HasCompletedLoops())
		return;

	UParticleModuleSpawn* SpawnModule = CurrentLODLevel->GetSpawnModule();
	if (!SpawnModule || !SpawnModule->IsEnabled())
		return;

	const float Rate = SpawnModule->GetSpawnRate(EmitterTime, RandomStream);
	const float OldLeftover = SpawnFraction;
	const float NewLeftover = OldLeftover + DeltaTime * Rate;
	int32 Number = FMath::FloorToInt(NewLeftover);
	const float Increment = Rate > 0.0f ? 1.0f / Rate : 0.0f;
	const float StartTime = DeltaTime + OldLeftover * Increment - Increment;
	SpawnFraction = NewLeftover - Number;

	int32 BurstNumber = ComputeBurstCount();

	const int32 Needed = ActiveParticles + Number + BurstNumber;
	if (Needed > MaxActiveParticles)
	{
		int32 NewMax = static_cast<int32>(Needed * 1.5f);
		if (NewMax > 65535)
		{
			NewMax = 65535;
		}
		Resize(NewMax);
	}

	// 칸이 모자라면 Rate 먼저, 남은 칸에 Burst
	const int32 FreeSlots = MaxActiveParticles - ActiveParticles;
	Number = Number < FreeSlots ? Number : FreeSlots;
	if (Number > 0)
	{
		SpawnParticles(Number, StartTime, Increment);
	}

	const int32 FreeSlotsAfter = MaxActiveParticles - ActiveParticles;
	BurstNumber = BurstNumber < FreeSlotsAfter ? BurstNumber : FreeSlotsAfter;
	if (BurstNumber > 0)
	{
		SpawnParticles(BurstNumber, 0.0f, 0.0f);
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

		// 이번 프레임에 생성된 파티클은 PostSpawn에서 이미 이동했다
		const bool bJustSpawned = (Particle->Flags & STATE_Particle_JustSpawned) != 0;
		Particle->Flags &= ~STATE_Particle_JustSpawned;
		if (!bJustSpawned)
		{
			Particle->Location += Particle->Velocity * DeltaTime;
			Particle->Rotation += Particle->RotationRate * DeltaTime;
		}

		NewBox.Expand(FBox(Particle->Location, Particle->Location));
	}
	BoundingBox = NewBox;
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (SpriteTemplate == nullptr || CurrentLODLevel == nullptr)
		return;

	UpdateEmitterTime(DeltaTime);

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