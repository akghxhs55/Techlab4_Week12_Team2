#include "EnginePCH.h"
#include "ParticleModuleMeshRotationRate.h"
#include "ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleMeshEmitterInstance.h"

UParticleModuleMeshRotationRate::UParticleModuleMeshRotationRate()
{
	bSpawnModule = true;

	StartRotationRate.Mode = EDistributionMode::Uniform;
	StartRotationRate.MinValue = FVector(-180.0f, -180.0f, -180.0f);
	StartRotationRate.MaxValue = FVector(180.0f, 180.0f, 180.0f);
	StartRotationRate.bLockAxes = false;

	BakeDistributions();
}

void UParticleModuleMeshRotationRate::BakeDistributions()
{
	StartRotationRate.Bake();
}

void UParticleModuleMeshRotationRate::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	// 메시 인스턴스는 TypeDataMesh만 만들므로 TypeData로 판별한다
	if (!Owner.CurrentLODLevel || !Cast<UParticleModuleTypeDataMesh>(Owner.CurrentLODLevel->GetTypeDataModule()))
		return;

	FMeshRotationPayloadData& Payload = static_cast<FParticleMeshEmitterInstance&>(Owner).GetMeshRotationPayload(Particle);
	const FVector Rate = StartRotationRate.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);
	Payload.RotationRate += Rate;
	Payload.RotationRateBase += Rate;
}
