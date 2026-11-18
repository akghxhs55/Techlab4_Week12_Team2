#pragma once
#include "ParticleEmitterInstance.h"
#include "ParticleTypes.h"

class UParticleModuleTypeDataRibbon;

struct FParticleRibbonEmitterInstance : public FParticleEmitterInstance
{
	void Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent) override;
	void UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const override;
	FRibbonPayloadData& GetRibbonPayload(FBaseParticle& Particle) const;

protected:
	int32 GetInstancePayloadSize() const override { return static_cast<int32>(sizeof(FRibbonPayloadData)); }
	void Spawn(float DeltaTime) override;
	void SpawnParticles(int32 Count, float StartTime, float Increment) override;
	void UpdateBoundingBox(float DeltaTime) override;

private:
	void SpawnRibbonParticle(const FVector& Position, float SpawnTime);
	// 상한을 넘지 않도록 가장 오래된 점부터 죽인다
	void KillOldest(int32 Count);
	void BreakTrail();

	UParticleModuleTypeDataRibbon* RibbonTypeData = nullptr;

	FVector LastSourcePos = FVector::ZeroVector;
	FVector LastSpawnPos = FVector::ZeroVector;
	float DistanceAccum = 0.0f;   // 아직 점 하나 분량이 안 된 이동거리
	float TiledDistance = 0.0f;   // 트레일 누적 이동거리
	float TrailOneOverLifetime = 0.0f;
	FVector TrailBaseSize = FVector(1.0f);
	uint32 TrailId = 0;
	bool bHasLastSource = false;
	bool bTrailHasPoints = false;

	TArray<FBaseParticle*> KillScratch;
};
