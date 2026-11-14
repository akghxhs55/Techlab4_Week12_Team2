#pragma once

#include "ParticleEmitter.generated.h"

class UParticleSystemComponent;
struct FParticleEmitterInstance;
class UParticleLODLevel;

// 언리얼의 UParticleSpriteEmitter도 포함
UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	TUniquePtr<FParticleEmitterInstance> CreateInstance(UParticleSystemComponent* Component);

	int32 CreateLODLevel(int32 LODLevel);
	void SetToSensibleDefaults();

	const TArray<UParticleLODLevel*>& GetLODLevels() const;

private:
	TArray<UParticleLODLevel*> LODLevels;
};
