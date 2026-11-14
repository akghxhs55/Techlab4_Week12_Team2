#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleTypes.h"
#include "ParticleModule.generated.h"

struct FParticleEmitterInstance;

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:

	virtual void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& ParticleBase);
	virtual void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime);

	virtual uint32 RequiredBytes() const { return 0; }
	virtual void BakeDistributions() {}

	bool IsSpawnModule() const { return bSpawnModule; }
	bool IsUpdateModule() const { return bUpdateModule; }
	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

protected:
	bool bSpawnModule = false;   // true면 파티클이 생성될 때 호출되는 모듈
	bool bUpdateModule = false;  // true면 파티클이 갱신될 때 호출되는 모듈
	bool bEnabled = true;      // false면 모듈이 비활성화되어 호출되지 않는다.
};