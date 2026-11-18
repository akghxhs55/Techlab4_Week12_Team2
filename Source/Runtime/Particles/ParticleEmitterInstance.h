#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Math/RandomStream.h"
#include "Particles/ParticleEventTypes.h"

struct FBaseParticle;
class UPrimitiveComponent;
struct FDynamicEmitterReplayDataBase;
class FDynamicEmitterDataBase;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystemComponent;

struct FParticleEmitterStats
{
	uint64 UpdateCycles = 0;
	uint64 SpawnCycles = 0;
	double UpdateMs = 0.0;
	double SpawnMs = 0.0;
};

struct FParticleEmitterInstance
{
	FParticleEmitterInstance() = default;
	virtual ~FParticleEmitterInstance();   // Sprite/Mesh 인스턴스가 상속한다
	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance&) = delete;

	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleSystemComponent* Component = nullptr;
	UParticleLODLevel* CurrentLODLevel = nullptr;
	int32 CurrentLODIndex = 0;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	int32   ParticleSize = 0;   // 에미터가 계산한 크기
	int32   ParticleStride = 0;   // 16바이트로 올림한 실제 칸 크기
	int32   ActiveParticles = 0;   // 살아 있는 수
	int32   MaxActiveParticles = 0;   // 블록에 들어가는 최대 수
	uint32  ParticleCounter = 0;   // 생성 순번
	int32 SubUVDataOffset = 0;
	// 인스턴스 전용 Payload 시작 위치. 모듈 Payload 뒤에 붙는다. 0이면 없음
	int32 InstancePayloadOffset = 0;

	float SpawnFraction = 0.0f;   // 초당 생성에서 남은 소수점
	float EmitterTime = 0.0f;   // 이번 루프 안에서의 시간
	float SecondsSinceCreation = 0.0f;
	int32 LoopCount = 0;          // 완료한 루프 수
	bool  bSpawningEnabled = true;  // false면 생성만 멈춤, 살아 있는 파티클은 계속 갱신
	TArray<uint8> BurstFired;     // Burst별 이번 루프에 터졌는지
	FRandomStream RandomStream;

	FBox BoundingBox;   // 월드 공간. 파티클 크기와 컴포넌트 스케일까지 포함

	FParticleEmitterStats Stats;
	uint64 GetParticleMemoryBytes() const
	{
		if (MaxActiveParticles <= 0)
			return 0;
		return static_cast<uint64>(MaxActiveParticles) * ParticleStride + sizeof(uint16) * (MaxActiveParticles + 1);
	}

	// 디버그: Burst가 터질 때마다 로그 (콘솔 p.LogBursts)
	static bool bLogBursts;

	virtual void Init(UParticleEmitter* InTemplate, UParticleSystemComponent* InComponent);
	virtual bool Resize(int32 NewMaxActiveParticles);
	virtual void Tick(float DeltaTime);
	// 자기 타입의 렌더 데이터를 만들거나 재사용해 이번 프레임 상태를 복사한다. 프록시는 그 결과만 그린다
	virtual void UpdateDynamicData(TUniquePtr<FDynamicEmitterDataBase>& InOutData) const;

	// 실행할 LOD 레벨을 바꾼다. 범위 밖이면 에미터의 마지막 레벨로 맞춘다.
	// 살아 있는 파티클은 그대로 두고 다음 Tick부터 새 레벨의 모듈로 갱신·생성한다
	virtual void SetCurrentLODIndex(int32 InLODIndex);

	// 루프를 모두 끝내 더 이상 생성하지 않는 상태
	bool HasCompletedLoops() const;
	// (루프 종료 또는 비활성화) + 살아 있는 파티클 없음
	bool IsComplete() const { return (HasCompletedLoops() || !bSpawningEnabled) && ActiveParticles == 0; }

	FVector GetEmitterOrigin() const;
	bool IsLocalSpace() const;
	FVector SimulationToWorld(const FVector& SimulationPosition) const;
	// 에미터 기준 방향/오프셋 → 시뮬레이션 공간 (회전·스케일만, 이동 제외)
	FVector EmitterToSimulationVector(const FVector& EmitterVector) const;
	FVector GetComponentScale() const;
	// 월드 위치 → 시뮬레이션 공간 (SimulationToWorld의 반대)
	FVector WorldToSimulation(const FVector& WorldPosition) const;
	// 월드 방향 → 시뮬레이션 공간 (로컬 공간이면 컴포넌트 역변환, 이동 제외)
	FVector WorldToSimulationVector(const FVector& WorldVector) const;
	// 시뮬레이션 공간 방향 → 월드 (로컬 공간이면 컴포넌트 변환, 이동 제외)
	FVector SimulationToWorldVector(const FVector& SimulationVector) const;

	// 이벤트 Receiver용: 월드 위치를 기준점으로 Count개를 바로 생성한다. Spawn 모듈 값 위에 WorldVelocity를 더한다
	void ForceSpawn(int32 Count, const FVector& WorldLocation, const FVector& WorldVelocity);
	// 살아 있는 파티클을 모두 지운다 (Death 이벤트는 보내지 않는다)
	void KillAllParticles();

	// 현재 LOD에 켜진 EventGenerator가 이 종류를 내보내는지
	bool WantsEvent(EParticleEventType Type) const;
	// Collision 모듈이 충돌을 처리할 때 부른다. 위치·법선은 월드 공간.
	// bFirstHit / bLastHit는 Generator의 FirstTimeOnly / LastTimeOnly 판단에 쓴다
	void ReportCollisionEvent(const FBaseParticle& Particle, const FVector& WorldLocation, const FVector& WorldNormal,
		float Time, UPrimitiveComponent* HitComponent, bool bFirstHit, bool bLastHit);

protected:
	// 서브클래스가 파티클마다 추가로 필요한 바이트 수. Init 안에서 불린다
	virtual int32 GetInstancePayloadSize() const { return 0; }
	virtual void UpdateEmitterTime(float DeltaTime);
	int32 ComputeBurstCount();
	virtual void KillParticles();
	virtual void ResetParticleParameters(float DeltaTime);
	virtual void Spawn(float DeltaTime);                                        // 이번 프레임 개수 결정
	virtual void SpawnParticles(int32 Count, float StartTime, float Increment); // 실제 N개 생성
	// 기준 위치와 추가 속도(시뮬레이션 공간)를 정해 N개 생성. 위 함수는 에미터 원점·속도 0으로 이걸 부른다
	void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& ExtraVelocity);
	virtual void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation);
	virtual void PostSpawn(FBaseParticle* Particle, float SpawnTime);
	virtual void UpdateBoundingBox(float DeltaTime);
	// 스케일 적용 전 파티클 중심에서 가장 먼 점까지의 거리
	virtual float GetParticleRadius(const FBaseParticle& Particle) const;
	// 모든 에미터 타입이 공유하는 렌더 사본 필드
	void FillReplayData(FDynamicEmitterReplayDataBase& Out) const;

	FBaseParticle* GetParticle(int32 SlotIndex) const;   // ParticleData + SlotIndex × ParticleStride
	// 이벤트 공통 부분(에미터, 시간, 월드 위치·속도·방향)을 파티클에서 채운다
	void FillEventData(FParticleEventData& OutEvent, EParticleEventType Type, const FBaseParticle& Particle) const;
	// Generator 설정마다 빈도·첫/마지막 조건을 보고 이름을 붙여 컴포넌트에 보고한다
	template <typename TEvent>
	void SendEvent(TEvent& Event, void (UParticleSystemComponent::*Report)(const TEvent&), bool bFirstHit = true, bool bLastHit = true);

	// Generator 설정별로 지금까지 일어난 사건 수 (Frequency 판단용)
	TArray<int32> EventCounters;
	void FreeParticleMemory();                           // 블록 해제 + MaxActiveParticles = 0
};