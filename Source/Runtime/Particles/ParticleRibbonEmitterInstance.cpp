#include "EnginePCH.h"
#include "ParticleRibbonEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleEmitter.h"
#include "ParticleModule.h"
#include "Module/ParticleModuleTypeDataRibbon.h"

#include "Components/ParticleSystemComponent.h"

#include "Engine/ParticleSystemRender.h"

#include <algorithm>

void FParticleRibbonEmitterInstance::Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	FParticleEmitterInstance::Init(InEmitter, InComponent);

	RibbonTypeData = CurrentLODLevel ? Cast<UParticleModuleTypeDataRibbon>(CurrentLODLevel->GetTypeDataModule()) : nullptr;
	bHasLastSource = false;
	bTrailHasPoints = false;
	TrailId = 0;
	TiledDistance = 0.0f;
	DistanceAccum = 0.0f;
}

void FParticleRibbonEmitterInstance::UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const
{
	if (!InOutData || InOutData->GetType() != EDynamicEmitterType::Ribbon)
		InOutData = MakeUnique<FDynamicRibbonEmitterData>();

	FDynamicRibbonEmitterReplayData& Source = static_cast<FDynamicRibbonEmitterData&>(*InOutData).Source;
	FillReplayData(Source);
	Source.RibbonPayloadOffset = InstancePayloadOffset;

	if (!RibbonTypeData || !Component)
	{
		Source.bHeadValid = false;
		return;
	}

	Source.Sheets = RibbonTypeData->GetSheets();
	const float TilingDistance = RibbonTypeData->GetTilingDistance();
	Source.bTiling = TilingDistance > 0.0f;
	Source.TessellationStepSize = RibbonTypeData->GetDistanceTessellationStepSize();
	Source.MaxTessellation = RibbonTypeData->GetMaxTessellationBetweenParticles();

	Source.HeadPosition = Component->GetWorldLocation();
	Source.HeadTrailId = TrailId;
	Source.bHeadValid = RibbonTypeData->IsRenderHead() && bHasLastSource && bTrailHasPoints;
	// 가장 새 점의 TiledU에 거기서 head까지 거리를 더해야 head 구간 텍스처가 이어진다
	Source.HeadTiledU = Source.bTiling
		? (TiledDistance + (Source.HeadPosition - LastSpawnPos).Length()) / TilingDistance
		: 0.0f;
}

FRibbonPayloadData& FParticleRibbonEmitterInstance::GetRibbonPayload(FBaseParticle& Particle) const
{
	return ParticleHelper::GetPayload<FRibbonPayloadData>(Particle, InstancePayloadOffset);
}

void FParticleRibbonEmitterInstance::BreakTrail()
{
	++TrailId;
	bTrailHasPoints = false;
	DistanceAccum = 0.0f;
}

void FParticleRibbonEmitterInstance::Spawn(float DeltaTime)
{
	if (!RibbonTypeData || RibbonTypeData->GetSpawnDistance() <= 0.0f)
	{
		FParticleEmitterInstance::Spawn(DeltaTime);
		return;
	}

	const FVector Cur = GetEmitterOrigin();
	const bool bCanSpawn = bSpawningEnabled && CurrentLODLevel->IsEnabled() && EmitterTime >= 0.0f && !HasCompletedLoops();
	if (!bCanSpawn)
	{
		bHasLastSource = false;
		return;
	}
	if (!bHasLastSource)
	{
		BreakTrail();
		LastSourcePos = Cur;
		bHasLastSource = true;
		return;
	}

	const FVector Delta = Cur - LastSourcePos;
	const float Step = Delta.Length();
	if (Step < 1e-4f)
		return;

	const float MaxSpawnDistance = RibbonTypeData->GetMaxSpawnDistance();
	if (MaxSpawnDistance > 0.0f && Step > MaxSpawnDistance)
	{
		BreakTrail();
		LastSourcePos = Cur;
		return;
	}

	const int32 MaxCount = RibbonTypeData->GetMaxParticleInTrailCount();
	const float SpawnDistance = RibbonTypeData->GetSpawnDistance();

	if (!bTrailHasPoints && RibbonTypeData->IsSpawnInitialParticle())
	{
		KillOldest(ActiveParticles + 1 - MaxCount);
		SpawnRibbonParticle(LastSourcePos, DeltaTime);
	}

	// 이번 프레임 이동 구간에서 SpawnDistance 간격으로 점이 놓일 자리를 찾는다
	const float OldAccum = DistanceAccum;
	const float Total = OldAccum + Step;
	int32 Number = FMath::FloorToInt(Total / SpawnDistance);
	DistanceAccum = Total - static_cast<float>(Number) * SpawnDistance;

	int32 FirstK = 1;
	if (Number > MaxCount)
	{
		FirstK = Number - MaxCount + 1;
	}
	const int32 SpawnCount = Number - FirstK + 1;
	if (SpawnCount > 0)
	{
		KillOldest(ActiveParticles + SpawnCount - MaxCount);
		for (int32 k = FirstK; k <= Number; ++k)
		{
			const float T = (std::clamp)((static_cast<float>(k) * SpawnDistance - OldAccum) / Step, 0.0f, 1.0f);
			SpawnRibbonParticle(LastSourcePos + Delta * T, DeltaTime * (1.0f - T));
		}
	}

	LastSourcePos = Cur;
}

void FParticleRibbonEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment)
{
	if (!RibbonTypeData)
	{
		FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment);
		return;
	}

	Count = (std::min)(Count, RibbonTypeData->GetMaxParticleInTrailCount());
	KillOldest(ActiveParticles + Count - RibbonTypeData->GetMaxParticleInTrailCount());

	const FVector Origin = GetEmitterOrigin();
	for (int32 i = 0; i < Count; ++i)
	{
		SpawnRibbonParticle(Origin, StartTime - Increment * static_cast<float>(i));
	}
}

void FParticleRibbonEmitterInstance::SpawnRibbonParticle(const FVector& Position, float SpawnTime)
{
	if (ActiveParticles >= MaxActiveParticles)
	{
		const int32 NewMax = (std::min)(MaxActiveParticles * 2, 65535);
		if (NewMax <= MaxActiveParticles || !Resize(NewMax))
			return;
	}

	const uint16 SlotIndex = ParticleIndices[ActiveParticles];
	FBaseParticle* Particle = GetParticle(SlotIndex);

	PreSpawn(Particle, Position);
	for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
	{
		if (!Module->IsEnabled())
			continue;
		Module->Spawn(*this, SpriteTemplate->GetModuleOffset(Module), SpawnTime, *Particle);
	}

	// 수명과 크기는 트레일 첫 점에서 한 번만 뽑는다. 점마다 다르면 꼬리 중간이 먼저 사라진다
	if (!bTrailHasPoints)
	{
		TrailOneOverLifetime = Particle->OneOverMaxLifetime;
		TrailBaseSize = Particle->BaseSize;
		TiledDistance = 0.0f;
	}
	else
	{
		TiledDistance += (Position - LastSpawnPos).Length();
	}
	Particle->OneOverMaxLifetime = TrailOneOverLifetime;
	Particle->RelativeTime = SpawnTime * TrailOneOverLifetime;
	Particle->BaseSize = TrailBaseSize;
	Particle->Size = TrailBaseSize;

	PostSpawn(Particle, SpawnTime);

	FRibbonPayloadData& Payload = GetRibbonPayload(*Particle);
	const float TilingDistance = RibbonTypeData->GetTilingDistance();
	Payload.TiledU = TilingDistance > 0.0f ? TiledDistance / TilingDistance : 0.0f;
	Payload.TrailId = TrailId;

	LastSpawnPos = Position;
	bTrailHasPoints = true;
	ActiveParticles++;
}

void FParticleRibbonEmitterInstance::KillOldest(int32 Count)
{
	if (Count <= 0 || ActiveParticles == 0)
		return;
	Count = (std::min)(Count, ActiveParticles);

	KillScratch.SetNum(ActiveParticles, false);
	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		KillScratch[i] = GetParticle(ParticleIndices[i]);
	}
	FBaseParticle** First = KillScratch.GetData();
	std::partial_sort(First, First + Count, First + ActiveParticles,
		[](const FBaseParticle* A, const FBaseParticle* B)
		{
			return (A->Flags & STATE_CounterMask) < (B->Flags & STATE_CounterMask);
		});
	for (int32 i = 0; i < Count; ++i)
	{
		First[i]->RelativeTime = 1.1f;
	}
	KillParticles();
}

void FParticleRibbonEmitterInstance::UpdateBoundingBox(float DeltaTime)
{
	FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);

	if (!RibbonTypeData || !RibbonTypeData->IsRenderHead() || ActiveParticles == 0 || !Component)
		return;

	const FVector Scale = GetComponentScale();
	const float MaxScale = (std::max)(std::abs(Scale.X), (std::max)(std::abs(Scale.Y), std::abs(Scale.Z)));
	const FVector Head = Component->GetWorldLocation();
	const FVector Extent(std::abs(TrailBaseSize.X) * MaxScale * 0.5f);
	BoundingBox.Expand(FBox(Head - Extent, Head + Extent));
}
