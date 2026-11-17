#include "EnginePCH.h"
#include "ParticleBeamEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "Module/ParticleModuleTypeDataBeam.h"

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

	// 부모 박스는 파티클 위치만 감싸므로 Source~Target 전체로 다시 만든다
	FBox NewBox(FVector(FLT_MAX), FVector(-FLT_MAX));
	ParticleHelper::ForEachParticle(*this, [&](FBaseParticle& Particle, uint8*)
		{
			FBeamPayloadData& Payload = GetBeamPayload(Particle);
			ResolveBeamPoints(Payload);

			const FVector Extent(std::abs(Particle.Size.X) * MaxScale * 0.5f);
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
