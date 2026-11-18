#include "EnginePCH.h"
#include "ParticleBeamEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleEmitter.h"
#include "Module/ParticleModuleTypeDataBeam.h"
#include "Module/ParticleModuleBeamNoise.h"

#include "Components/ParticleSystemComponent.h"

#include "Engine/ParticleSystemRender.h"

#include <algorithm>

void FParticleBeamEmitterInstance::Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	FParticleEmitterInstance::Init(InEmitter, InComponent);

	BeamTypeData = CurrentLODLevel ? Cast<UParticleModuleTypeDataBeam>(CurrentLODLevel->GetTypeDataModule()) : nullptr;
}

void FParticleBeamEmitterInstance::UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const
{
	if (!InOutData || InOutData->GetType() != EDynamicEmitterType::Beam)
		InOutData = MakeUnique<FDynamicBeamEmitterData>();

	FDynamicBeamEmitterReplayData& Source = static_cast<FDynamicBeamEmitterData&>(*InOutData).Source;
	FillReplayData(Source);
	Source.BeamPayloadOffset = InstancePayloadOffset;
	if (BeamTypeData)
	{
		Source.Sheets = BeamTypeData->GetSheets();
		Source.Segments = BeamTypeData->GetSegments();
		Source.TextureTile = BeamTypeData->GetTextureTile();
	}

	Source.NoiseOffset = 0;
	if (UParticleModuleBeamNoise* Noise = FindNoiseModule())
	{
		// 시뮬레이션이 모듈에 넘기는 것과 같은 오프셋이다
		Source.NoiseOffset = SpriteTemplate->GetModuleOffset(Noise);
		Source.NoiseFrequency = Noise->GetFrequency();
		Source.bNoiseSmooth = Noise->IsSmooth();
		Source.NoiseTessellation = Noise->GetNoiseTessellation();
	}
}

UParticleModuleBeamNoise* FParticleBeamEmitterInstance::FindNoiseModule() const
{
	if (!CurrentLODLevel || !SpriteTemplate)
		return nullptr;
	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		UParticleModuleBeamNoise* Noise = Cast<UParticleModuleBeamNoise>(Module);
		if (Noise && Noise->IsEnabled())
			return Noise;
	}
	return nullptr;
}

FBeamPayloadData& FParticleBeamEmitterInstance::GetBeamPayload(FBaseParticle& Particle) const
{
	return ParticleHelper::GetPayload<FBeamPayloadData>(Particle, InstancePayloadOffset);
}

void FParticleBeamEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment)
{
	if (!BeamTypeData)
		return;

	// Rate와 Burst가 같은 프레임에 겹쳐도 MaxBeamCount를 넘지 않도록 여기서 자른다
	Count = std::min(Count, BeamTypeData->GetMaxBeamCount() - ActiveParticles);
	if (Count <= 0)
		return;

	FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment);
}

void FParticleBeamEmitterInstance::UpdateBoundingBox(float DeltaTime)
{
	FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);

	const FVector Scale = GetComponentScale();
	const float MaxScale = std::max(std::abs(Scale.X), std::max(std::abs(Scale.Y), std::abs(Scale.Z)));
	// 노이즈 점은 축에서 (Right, Up) 두 방향으로 최대 NoiseRange씩 벗어난다
	const UParticleModuleBeamNoise* Noise = FindNoiseModule();
	const float NoiseExtent = Noise ? Noise->GetNoiseRange() * 1.4143f : 0.0f;

	// 부모 박스는 파티클 위치만 감싸므로 Source~Target 전체로 다시 만든다
	FBox NewBox(FVector(FLT_MAX), FVector(-FLT_MAX));
	ParticleHelper::ForEachParticle(*this, [&](FBaseParticle& Particle, uint8*)
		{
			FBeamPayloadData& Payload = GetBeamPayload(Particle);
			ResolveBeamPoints(Payload);

			const FVector Extent(std::abs(Particle.Size.X) * MaxScale * 0.5f + NoiseExtent);
			NewBox.Expand(FBox(Payload.SourcePoint - Extent, Payload.SourcePoint + Extent));
			NewBox.Expand(FBox(Payload.TargetPoint - Extent, Payload.TargetPoint + Extent));
		});
	BoundingBox = NewBox;
}

void FParticleBeamEmitterInstance::ResolveBeamPoints(FBeamPayloadData& Payload) const
{
	if (!BeamTypeData || !Component)
		return;

	// Payload는 월드 좌표다. 시뮬레이션 공간 함수(GetEmitterOrigin 등)는 로컬 스페이스에서 원점을 돌려주므로 쓰지 않는다
	const FMatrix& WorldMatrix = Component->GetWorldMatrix();
	Payload.SourcePoint = WorldMatrix.TransformPosition(FVector::ZeroVector);

	switch (BeamTypeData->GetBeamMethod())
	{
	case EBeamMethod::Distance:
	{
		const FVector Forward = WorldMatrix.TransformVector(FVector(1.0f, 0.0f, 0.0f)).Normalized();
		Payload.TargetPoint = Payload.SourcePoint + Forward * BeamTypeData->GetDistance();
		break;
	}
	case EBeamMethod::Target:
		// 에미터 기준 위치. 액터를 옮기거나 돌려도 Source와의 관계가 유지된다
		Payload.TargetPoint = WorldMatrix.TransformPosition(BeamTypeData->GetTargetPoint());
		break;
	default:
		break;
	}
}
