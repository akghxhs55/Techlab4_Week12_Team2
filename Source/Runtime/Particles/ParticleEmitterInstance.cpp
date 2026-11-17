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
#include "Engine/ParticleSystemRender.h"
#include "Engine/Scene.h"

#include <malloc.h>
#include <cstring>

bool FParticleEmitterInstance::bLogBursts = false;

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
	CurrentLODIndex = 0;
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
			const int32 Count = SpawnModule->GetBurstCount(i, RandomStream);
			Total += Count;
			BurstFired[i] = 1;
			if (bLogBursts)
			{
				KRS_LOG(Info, "Particle burst {} fired {} (LOD {}, loop {}, time {:.2f})", i, Count, CurrentLODIndex, LoopCount, EmitterTime);
			}
		}
	}
	return Total;
}

void FParticleEmitterInstance::SetCurrentLODIndex(int32 InLODIndex)
{
	if (SpriteTemplate == nullptr)
		return;

	const int32 NumLODLevels = SpriteTemplate->GetNumLODLevels();
	if (NumLODLevels == 0)
		return;

	const int32 NewIndex = (std::clamp)(InLODIndex, 0, NumLODLevels - 1);
	UParticleLODLevel* NewLODLevel = SpriteTemplate->GetLODLevel(NewIndex);
	if (NewLODLevel == nullptr || NewLODLevel == CurrentLODLevel)
		return;

	CurrentLODIndex = NewIndex;
	CurrentLODLevel = NewLODLevel;

	// Burst 발사 기록은 항목 번호로 남는다. 항목 수가 다른 레벨로 오면 기록이 통째로 초기화되어
	// 이미 터진 Burst가 다시 터지므로, 이번 루프에서 시간이 지난 항목은 터진 것으로 둔다
	const UParticleModuleSpawn* SpawnModule = CurrentLODLevel->GetSpawnModule();
	if (SpawnModule && SpawnModule->GetBurstList().Num() != BurstFired.Num())
	{
		const TArray<FParticleBurst>& Bursts = SpawnModule->GetBurstList();
		BurstFired.Init(0, Bursts.Num());
		for (int32 i = 0; i < Bursts.Num(); ++i)
		{
			BurstFired[i] = (EmitterTime >= Bursts[i].Time) ? 1 : 0;
		}
	}
}

void FParticleEmitterInstance::Spawn(float DeltaTime)
{
	// 꺼진 LOD 레벨은 생성만 멈춘다. bSpawningEnabled(Deactivate)와는 따로 둬야
	// 꺼진 레벨에서 돌아왔을 때 Deactivate한 이펙트가 다시 살아나지 않는다
	if (!bSpawningEnabled || !CurrentLODLevel->IsEnabled() || EmitterTime < 0.0f || HasCompletedLoops())
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

	// 현재 LOD의 상한. 메모리 블록은 이전 LOD에서 더 커져 있을 수 있으므로 생성 수만 이 값으로 자른다
	const UParticleModuleRequired* Required = CurrentLODLevel->GetRequiredModule();
	const int32 LODLimit = (Required && Required->GetMaxActiveParticles() > 0) ? Required->GetMaxActiveParticles() : 65535;

	const int32 Needed = (std::min)(ActiveParticles + Number + BurstNumber, LODLimit);
	if (Needed > MaxActiveParticles)
	{
		int32 NewMax = (std::min)(static_cast<int32>(Needed * 1.5f), LODLimit);
		if (NewMax > 65535)
		{
			NewMax = 65535;
		}
		Resize(NewMax);
	}

	// 상한보다 많이 살아 있으면(상한이 낮은 LOD로 바뀐 직후) 줄어들 때까지 생성하지 않는다
	const int32 SpawnLimit = (std::min)(MaxActiveParticles, LODLimit);

	// 칸이 모자라면 Rate 먼저, 남은 칸에 Burst
	const int32 FreeSlots = (std::max)(SpawnLimit - ActiveParticles, 0);
	Number = Number < FreeSlots ? Number : FreeSlots;
	if (Number > 0)
	{
		SpawnParticles(Number, StartTime, Increment);
	}

	const int32 FreeSlotsAfter = (std::max)(SpawnLimit - ActiveParticles, 0);
	BurstNumber = BurstNumber < FreeSlotsAfter ? BurstNumber : FreeSlotsAfter;
	if (BurstNumber > 0)
	{
		SpawnParticles(BurstNumber, 0.0f, 0.0f);
	}
}

FVector FParticleEmitterInstance::GetComponentScale() const
{
	return Component ? Component->GetWorldScale3D() : FVector(1.0f);
}

float FParticleEmitterInstance::GetParticleRadius(const FBaseParticle& Particle) const
{
	// 카메라 평면 안에서 회전하는 쿼드의 반대각선
	return 0.5f * std::sqrt(Particle.Size.X * Particle.Size.X + Particle.Size.Y * Particle.Size.Y);
}

void FParticleEmitterInstance::FillReplayData(FDynamicEmitterReplayDataBase& Out) const
{
	Out.ActiveParticleCount = ActiveParticles;
	Out.MaxActiveParticles = MaxActiveParticles;
	Out.ParticleStride = ParticleStride;
	if (ActiveParticles > 0)
	{
		// 인덱스가 슬롯 위치를 가리키므로 블록은 통째로 복사한다
		const int32 DataSize = MaxActiveParticles * ParticleStride;
		Out.ParticleData.SetNum(DataSize, false);
		std::memcpy(Out.ParticleData.GetData(), ParticleData, DataSize);
		Out.ParticleIndices.SetNum(ActiveParticles, false);
		std::memcpy(Out.ParticleIndices.GetData(), ParticleIndices, sizeof(uint16) * ActiveParticles);
	}

	Out.bUseLocalSpace = IsLocalSpace() && Component;
	Out.LocalToWorld = Out.bUseLocalSpace ? Component->GetWorldMatrix() : FMatrix::Identity;
	Out.Scale = GetComponentScale();

	const UParticleModuleRequired* Required = CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr;
	Out.Material = Required ? Required->GetMaterial() : nullptr;
	Out.SortMode = Required ? Required->GetSortMode() : EParticleSortMode::None;
}

void FParticleEmitterInstance::UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const
{
	if (!InOutData || InOutData->GetType() != EDynamicEmitterType::Sprite)
		InOutData = MakeUnique<FDynamicSpriteEmitterData>();

	FDynamicSpriteEmitterReplayData& Source = static_cast<FDynamicSpriteEmitterData&>(*InOutData).Source;
	FillReplayData(Source);

	const UParticleModuleRequired* Required = CurrentLODLevel ? CurrentLODLevel->GetRequiredModule() : nullptr;
	const bool bSubUV = Required && Required->UsesSubUV() && SubUVDataOffset > 0;
	Source.SubUVDataOffset = bSubUV ? SubUVDataOffset : 0;
	Source.SubImagesHorizontal = bSubUV ? Required->GetSubImagesHorizontal() : 1;
	Source.SubImagesVertical = bSubUV ? Required->GetSubImagesVertical() : 1;
}

void FParticleEmitterInstance::UpdateBoundingBox(float DeltaTime)
{
	const bool bLocalSpace = IsLocalSpace() && Component;
	const FMatrix SimToWorld = bLocalSpace ? Component->GetWorldMatrix() : FMatrix::Identity;
	// Local은 월드 행렬이, World는 렌더 시 크기에 곱하는 스케일이 같은 값이다
	const FVector Scale = GetComponentScale();
	const float MaxScale = std::max(std::abs(Scale.X), std::max(std::abs(Scale.Y), std::abs(Scale.Z)));

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

		const FVector Center = bLocalSpace ? SimToWorld.TransformPosition(Particle->Location) : Particle->Location;
		const FVector Extent(GetParticleRadius(*Particle) * MaxScale);
		NewBox.Expand(FBox(Center - Extent, Center + Extent));
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