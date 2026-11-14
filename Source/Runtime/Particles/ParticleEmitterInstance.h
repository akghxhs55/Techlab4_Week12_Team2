#pragma once

struct FBaseParticle;
class UParticleLODLevel;
class UParticleSystemComponent;
class UParticleEmitter;

// i번째 파티클: (FBaseParticle*)(ParticleData.GetData() + ParticleStride * ParticleIndices[i])
struct FParticleEmitterInstance
{
	explicit FParticleEmitterInstance(UParticleSystemComponent& InComponent);
	virtual ~FParticleEmitterInstance() = default;

	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance(FParticleEmitterInstance&&) = delete;
	FParticleEmitterInstance& operator=(FParticleEmitterInstance&&) = delete;

	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleSystemComponent& Component;

	int32 CurrentLODLevelIndex = 0;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	TArray<uint8> ParticleData;
	TArray<uint16> ParticleIndices;

	int32 ParticleSize = 0; // sizeof(FBaseParticle)
	int32 ParticleStride = 0;
	int32 PayloadOffset = 0;

	int32 ActiveParticles = 0;
	int32 MaxActiveParticles = 0;

	float SpawnFraction = 0.0f;
	float EmitterTime = 0.0f;

	FVector Location;
	FVector OldLocation;

	virtual void InitParameters(UParticleEmitter* InEmitter);
	virtual void Init();
	virtual void Tick(float DeltaTime, bool bSuppressSpawning);

	virtual bool Resize(int32 NewMaxActiveParticles);

	FBaseParticle* GetParticle(int32 Index);

	void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity);
	void KillParticles();
};
