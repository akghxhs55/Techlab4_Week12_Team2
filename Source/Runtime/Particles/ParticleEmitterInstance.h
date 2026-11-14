#pragma once

#include "Core/Types.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Math/RandomStream.h"

struct FBaseParticle;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystemComponent;

struct FParticleEmitterInstance
{
	FParticleEmitterInstance() = default;
	~FParticleEmitterInstance();
	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance&) = delete;

	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleSystemComponent* Component = nullptr;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	int32   ParticleSize = 0;   // 에미터가 계산한 크기
	int32   ParticleStride = 0;   // 16바이트로 올림한 실제 칸 크기
	int32   ActiveParticles = 0;   // 살아 있는 수
	int32   MaxActiveParticles = 0;   // 블록에 들어가는 최대 수
	uint32  ParticleCounter = 0;   // 생성 순번

	float SpawnFraction = 0.0f;   // 초당 생성에서 남은 소수점
	float EmitterTime = 0.0f;   // 이번 루프 안에서의 시간
	float SecondsSinceCreation = 0.0f;
	FRandomStream RandomStream;

	FBox BoundingBox;

	void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent);
	bool Resize(int32 NewMaxActiveParticles);
	void Tick(float DeltaTime);

private:
	void KillParticles();
	void ResetParticleParameters(float DeltaTime);
	void Spawn(float DeltaTime);                                        // 이번 프레임 개수 결정
	void SpawnParticles(int32 Count, float StartTime, float Increment); // 실제 N개 생성
	void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation);
	void PostSpawn(FBaseParticle* Particle, float SpawnTime);
	void UpdateBoundingBox(float DeltaTime);                            // 위치 적분 + 바운드

	FBaseParticle* GetParticle(int32 SlotIndex) const;   // ParticleData + SlotIndex × ParticleStride
};