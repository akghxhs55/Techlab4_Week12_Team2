#pragma once
#include "ParticleEmitterInstance.h"
#include "ParticleTypes.h"

class UParticleModuleTypeDataBeam;
class UParticleModuleBeamNoise;

struct FParticleBeamEmitterInstance : public FParticleEmitterInstance
{
	void Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent) override;
	void UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const override;
	FBeamPayloadData& GetBeamPayload(FBaseParticle& Particle) const;

protected:
	int32 GetInstancePayloadSize() const override { return static_cast<int32>(sizeof(FBeamPayloadData)); }
	void SpawnParticles(int32 Count, float StartTime, float Increment) override;
	void UpdateBoundingBox(float DeltaTime) override;

private:
	void ResolveBeamPoints(FBeamPayloadData& Payload) const;
	// 현재 LOD에서 켜져 있는 BeamNoise 모듈. 없으면 nullptr
	UParticleModuleBeamNoise* FindNoiseModule() const;
	UParticleModuleTypeDataBeam* BeamTypeData = nullptr;
};
