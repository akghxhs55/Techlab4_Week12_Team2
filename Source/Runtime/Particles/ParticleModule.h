#pragma once

#include "ParticleModule.generated.h"

struct FBaseParticle;
struct FParticleEmitterInstance;

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:
	bool bSpawnModule = false;
	bool bUpdateModule = false;

	struct FSpawnContext
	{
		FParticleEmitterInstance* Owner;
		int32 Offset;
		float SpawnTime;
		FBaseParticle* ParticleBase;
	};
	virtual void Spawn(const FSpawnContext& Context) {}

	struct FUpdateContext
	{
		FParticleEmitterInstance* Owner;
		int32 Offset;
		float DeltaTime;
	};
	virtual void Update(const FUpdateContext& Context) {}
};
