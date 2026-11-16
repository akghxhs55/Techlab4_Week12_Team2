#include "EnginePCH.h"
#include "ParticleModuleTypeDataMesh.h"
#include "Render/Mesh.h"
#include "Particles/ParticleMeshEmitterInstance.h"

UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh()
{
	RollPitchYaw.Mode = EDistributionMode::Uniform;
	RollPitchYaw.MinValue = FVector(-180.0f, -180.0f, -180.0f);
	RollPitchYaw.MaxValue = FVector(180.0f, 180.0f, 180.0f);
	// 축마다 다른 랜덤이어야 파편처럼 제각각 돈다
	RollPitchYaw.bLockAxes = false;
	BakeDistributions();
}

FParticleEmitterInstance* UParticleModuleTypeDataMesh::CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	FParticleMeshEmitterInstance* Instance = new FParticleMeshEmitterInstance();
	Instance->Init(InEmitter, InComponent);
	return Instance;
}

void UParticleModuleTypeDataMesh::BakeDistributions()
{
	RollPitchYaw.Bake();
}
