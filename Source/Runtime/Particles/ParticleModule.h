#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleTypes.h"
#include "ParticleModule.generated.h"

struct FParticleEmitterInstance;

// 에디터의 모듈 분류(색, Required/Spawn 고정)와 종류 전용 모듈 검색에 쓴다
// UPROPERTY로 쓰지 않으므로 리플렉션 없는 일반 enum
enum EModuleType : int
{
	EPMT_General,    // 모든 에미터 종류에서 사용
	EPMT_TypeData,   // 에미터 종류를 바꾸는 TypeData
	EPMT_Beam,       // Beam 에미터 전용
	EPMT_Trail,      // Ribbon/Trail 에미터 전용
	EPMT_Spawn,      // 모든 에미터에 필수
	EPMT_Required,   // 모든 에미터에 필수
	EPMT_Event,
	EPMT_Light,
	EPMT_SubUV,
	EPMT_MAX,
};

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:

	virtual void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& ParticleBase);
	virtual void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime);

	virtual uint32 RequiredBytes() const { return 0; }
	virtual void BakeDistributions() {}

	virtual EModuleType GetModuleType() const { return EPMT_General; }

	// 디테일 패널에서 값이 바뀌면 분포를 다시 굽는다
	void PostEditChangeProperty(const FProperty& Property) override;

	bool IsSpawnModule() const { return bSpawnModule; }
	bool IsUpdateModule() const { return bUpdateModule; }
	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

protected:
	bool bSpawnModule = false;   // true면 파티클이 생성될 때 호출되는 모듈
	bool bUpdateModule = false;  // true면 파티클이 갱신될 때 호출되는 모듈
	UPROPERTY(EditAnywhere)
	bool bEnabled = true;      // false면 모듈이 비활성화되어 호출되지 않는다.
};