#pragma once
#include "ParticleEmitterInstance.h"
#include "Math/Vector.h"

class UParticleModuleTypeDataMesh;

// 단위는 모두 도(degree)
struct FMeshRotationPayloadData
{
	FVector InitialOrientation;		// 생성될 때의 회전. X = Roll, Y = Pitch, Z = Yaw
	FVector Rotation;				// 생성 후 누적된 회전
	FVector RotationRate;			// 이번 프레임 회전 속도(도/초). 모듈이 바꿀 수 있는 현재 값
	FVector RotationRateBase;		// 회전 속도의 기준값. 매 프레임 RotationRate로 복원
};
static_assert(sizeof(FMeshRotationPayloadData) == 48, "FMeshRotationPayloadData 크기 오류, 48이어야 합니다.");

struct FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
	FParticleMeshEmitterInstance() = default;
	~FParticleMeshEmitterInstance() override = default;

	void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent) override;

	UParticleModuleTypeDataMesh* GetMeshTypeData() const { return MeshTypeData; }
	FMeshRotationPayloadData& GetMeshRotationPayload(FBaseParticle& Particle) const;
	const FMeshRotationPayloadData& GetMeshRotationPayload(const FBaseParticle& Particle) const;

protected:
	int32 GetInstancePayloadSize() const override { return static_cast<int32>(sizeof(FMeshRotationPayloadData)); }
	void ResetParticleParameters(float DeltaTime) override;
	void PostSpawn(FBaseParticle* Particle, float SpawnTime) override;
	void UpdateBoundingBox(float DeltaTime) override;

private:
	UParticleModuleTypeDataMesh* MeshTypeData = nullptr;
};
