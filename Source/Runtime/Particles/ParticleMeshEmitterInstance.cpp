#include "EnginePCH.h"
#include "ParticleMeshEmitterInstance.h"
#include "ParticleHelper.h"
#include "ParticleLODLevel.h"
#include "ParticleTypes.h"
#include "Module/ParticleModuleTypeDataMesh.h"
#include "Render/Mesh.h"
#include "Engine/ParticleSystemRender.h"

void FParticleMeshEmitterInstance::Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent)
{
	// 부모가 메모리 할당과 Stride(인스턴스 Payload 포함) 계산, CurrentLODLevel 설정까지 한다
	FParticleEmitterInstance::Init(InTemplate, InComponent);

	MeshTypeData = CurrentLODLevel ? Cast<UParticleModuleTypeDataMesh>(CurrentLODLevel->GetTypeDataModule()) : nullptr;
}

void FParticleMeshEmitterInstance::UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const
{
	if (!InOutData || InOutData->GetType() != EDynamicEmitterType::Mesh)
		InOutData = MakeUnique<FDynamicMeshEmitterData>();

	FDynamicMeshEmitterReplayData& Source = static_cast<FDynamicMeshEmitterData&>(*InOutData).Source;
	FillReplayData(Source);
	Source.Mesh = MeshTypeData ? MeshTypeData->GetMesh() : nullptr;
	Source.bOverrideMaterial = MeshTypeData && MeshTypeData->IsOverrideMaterial();
	Source.MeshRotationPayloadOffset = InstancePayloadOffset;
}

FMeshRotationPayloadData& FParticleMeshEmitterInstance::GetMeshRotationPayload(FBaseParticle& Particle) const
{
	return ParticleHelper::GetPayload<FMeshRotationPayloadData>(Particle, InstancePayloadOffset);
}

const FMeshRotationPayloadData& FParticleMeshEmitterInstance::GetMeshRotationPayload(const FBaseParticle& Particle) const
{
	return ParticleHelper::GetPayload<FMeshRotationPayloadData>(Particle, InstancePayloadOffset);
}

void FParticleMeshEmitterInstance::ResetParticleParameters(float DeltaTime)
{
	FParticleEmitterInstance::ResetParticleParameters(DeltaTime);

	ParticleHelper::ForEachParticle(*this, [&](FBaseParticle& Particle, uint8*)
	{
		FMeshRotationPayloadData& Payload = GetMeshRotationPayload(Particle);
		Payload.RotationRate = Payload.RotationRateBase;
	});
}

void FParticleMeshEmitterInstance::PostSpawn(FBaseParticle* Particle, float SpawnTime)
{
	FParticleEmitterInstance::PostSpawn(Particle, SpawnTime);

	// Rotation/RotationRate는 PreSpawn이 0으로 지웠고, 회전 속도 모듈이 PostSpawn 전에 값을 넣었을 수 있으므로 건드리지 않는다
	if (MeshTypeData)
	{
		FMeshRotationPayloadData& Payload = GetMeshRotationPayload(*Particle);
		Payload.InitialOrientation = MeshTypeData->GetRollPitchYaw().GetVectorValue(EmitterTime, RandomStream);
	}
}

void FParticleMeshEmitterInstance::UpdateBoundingBox(float DeltaTime)
{
	// 부모가 JustSpawned 플래그를 지우므로 회전 적분을 먼저 한다
	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticle(ParticleIndices[i]);
		// 이번 프레임에 생성된 파티클은 위치처럼 회전도 건너뛴다
		if (Particle->Flags & STATE_Particle_JustSpawned)
			continue;
		// 부모의 위치 적분과 같은 규칙으로 Freeze 계열은 회전하지 않는다
		if (Particle->Flags & (STATE_Particle_Freeze | STATE_Particle_FreezeRotation))
			continue;

		FMeshRotationPayloadData& Payload = GetMeshRotationPayload(*Particle);
		Payload.Rotation += Payload.RotationRate * DeltaTime;
	}

	// 에디터에서 메시를 바꿀 수 있으므로 매번 다시 읽는다
	MeshRadius = 0.0f;
	if (const UStaticMesh* Mesh = MeshTypeData ? MeshTypeData->GetMesh() : nullptr)
	{
		const FBox& MeshBounds = Mesh->GetRenderBounds();
		const FVector MaxCorner(
			std::max(std::abs(MeshBounds.Min.X), std::abs(MeshBounds.Max.X)),
			std::max(std::abs(MeshBounds.Min.Y), std::abs(MeshBounds.Max.Y)),
			std::max(std::abs(MeshBounds.Min.Z), std::abs(MeshBounds.Max.Z)));
		MeshRadius = MaxCorner.Length();
	}

	FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);
}

float FParticleMeshEmitterInstance::GetParticleRadius(const FBaseParticle& Particle) const
{
	const float MaxSize = std::max(std::abs(Particle.Size.X), std::max(std::abs(Particle.Size.Y), std::abs(Particle.Size.Z)));
	return MeshRadius * MaxSize;
}
