#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
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
	virtual ~FParticleEmitterInstance();   // Sprite/Mesh 인스턴스가 상속한다
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
	int32 SubUVDataOffset = 0;

	float SpawnFraction = 0.0f;   // 초당 생성에서 남은 소수점
	float EmitterTime = 0.0f;   // 이번 루프 안에서의 시간
	float SecondsSinceCreation = 0.0f;
	int32 LoopCount = 0;          // 완료한 루프 수
	bool  bSpawningEnabled = true;  // false면 생성만 멈춤, 살아 있는 파티클은 계속 갱신
	TArray<uint8> BurstFired;     // Burst별 이번 루프에 터졌는지
	FRandomStream RandomStream;

	FBox BoundingBox;

	virtual void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent);
	virtual bool Resize(int32 NewMaxActiveParticles);
	virtual void Tick(float DeltaTime);

	// 루프를 모두 끝내 더 이상 생성하지 않는 상태
	bool HasCompletedLoops() const;
	// (루프 종료 또는 비활성화) + 살아 있는 파티클 없음
	bool IsComplete() const { return (HasCompletedLoops() || !bSpawningEnabled) && ActiveParticles == 0; }

	FVector GetEmitterOrigin() const;
	bool IsLocalSpace() const;
	FVector SimulationToWorld(const FVector& SimulationPosition) const;

protected:
	virtual void UpdateEmitterTime(float DeltaTime);
	int32 ComputeBurstCount();
	virtual void KillParticles();
	virtual void ResetParticleParameters(float DeltaTime);
	virtual void Spawn(float DeltaTime);                                        // 이번 프레임 개수 결정
	virtual void SpawnParticles(int32 Count, float StartTime, float Increment); // 실제 N개 생성
	virtual void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation);
	virtual void PostSpawn(FBaseParticle* Particle, float SpawnTime);
	virtual void UpdateBoundingBox(float DeltaTime);

	FBaseParticle* GetParticle(int32 SlotIndex) const;   // ParticleData + SlotIndex × ParticleStride
	void FreeParticleMemory();                           // 블록 해제 + MaxActiveParticles = 0
};