#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "Containers/Map.h"
#include "ParticleModule.h"
#include "ParticleEmitter.generated.h"

class UParticleLODLevel;
class UParticleSystemComponent;
struct FParticleEmitterInstance;

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	// 모듈 구성이 바뀐 뒤 한 번 호출
	void Build();

	// 호출자가 소유하고 delete한다
	virtual FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent);

	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }
	UParticleLODLevel* GetLODLevel(int32 Index) const { return LODLevels.IsValidIndex(Index) ? LODLevels[Index] : nullptr; }
	void AddLODLevel(UParticleLODLevel* InLODLevel);

	int32 GetParticleSize() const { return ParticleSize; }
	int32 GetModuleOffset(UParticleModule* Module) const;   // Payload가 없는 모듈이면 0

private:
	// 파티클 한 칸의 크기와 모듈별 Payload 오프셋을 계산
	void CacheEmitterModuleInfo();

	int32 ParticleSize = 0;
	TArray<UParticleLODLevel*> LODLevels;
	TMap<UParticleModule*, int32> ModuleOffsetMap;
};