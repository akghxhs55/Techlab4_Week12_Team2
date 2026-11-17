#pragma once
#include "Particles/Module/ParticleModuleTypeDataBase.h"
#include <algorithm>
#include "ParticleModuleTypeDataRibbon.generated.h"

UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent) override;

	int32 GetMaxParticleInTrailCount() const { return std::max(2, MaxParticleInTrailCount); }
	float GetSpawnDistance() const { return SpawnDistance; }
	float GetTilingDistance() const { return TilingDistance; }
	float GetMaxSpawnDistance() const { return MaxSpawnDistance; }
	int32 GetSheets() const { return std::max(1, Sheets); }
	bool IsRenderHead() const { return bRenderHead; }
	bool IsSpawnInitialParticle() const { return bSpawnInitialParticle; }

private:
	UPROPERTY(EditAnywhere)
	int32 MaxParticleInTrailCount = 64;

	UPROPERTY(EditAnywhere)
	float SpawnDistance = 10.f;     // 0이면 거리 기준 생성 끔

	UPROPERTY(EditAnywhere)
	float TilingDistance = 0.f;     // 0이면 Stretch

	UPROPERTY(EditAnywhere)
	float MaxSpawnDistance = 500.f; // 한 프레임에 이보다 멀리 가면 텔레포트로 보고 잇지 않음

	UPROPERTY(EditAnywhere)
	int32 Sheets = 1;

	UPROPERTY(EditAnywhere)
	bool bRenderHead = true;

	UPROPERTY(EditAnywhere)
	bool bSpawnInitialParticle = true;   // 움직이기 시작하면 SpawnDistance를 채우기 전에 첫 점을 만든다
};