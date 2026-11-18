#pragma once
#include "ParticleEmitterInstance.h"
#include "ParticleTypes.h"

class UParticleModuleTypeDataMesh;

struct FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
	FParticleMeshEmitterInstance() = default;
	~FParticleMeshEmitterInstance() override = default;

	void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent) override;
	void UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const override;

	UParticleModuleTypeDataMesh* GetMeshTypeData() const { return MeshTypeData; }
	FMeshRotationPayloadData& GetMeshRotationPayload(FBaseParticle& Particle) const;
	const FMeshRotationPayloadData& GetMeshRotationPayload(const FBaseParticle& Particle) const;

protected:
	int32 GetInstancePayloadSize() const override { return static_cast<int32>(sizeof(FMeshRotationPayloadData)); }
	void ResetParticleParameters(float DeltaTime) override;
	void PostSpawn(FBaseParticle* Particle, float SpawnTime) override;
	void UpdateBoundingBox(float DeltaTime) override;
	float GetParticleRadius(const FBaseParticle& Particle) const override;

private:
	UParticleModuleTypeDataMesh* MeshTypeData = nullptr;
	float MeshRadius = 0.0f;   // 메시 원점에서 가장 먼 꼭짓점까지. UpdateBoundingBox마다 갱신
};
