#pragma once

#include "PrimitiveComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEventTypes.h"
#include "Core/Delegate/Delegate.h"
#include <map>
#include "ParticleSystemComponent.generated.h"

class UWorld;
class FLineBatcher;
class AActor;
class UObject;
struct FParticleEmitterInstance;
class UParticleSystemComponent;

// 게임 코드가 파티클 이벤트를 받는 델리게이트. EventGenerator가 내보낸 이벤트만 온다
DECLARE_DELEGATE(FOnParticleSpawnSignature, UParticleSystemComponent*, const FParticleEventSpawnData&);
DECLARE_DELEGATE(FOnParticleDeathSignature, UParticleSystemComponent*, const FParticleEventDeathData&);
DECLARE_DELEGATE(FOnParticleCollideSignature, UParticleSystemComponent*, const FParticleEventCollisionData&);
DECLARE_DELEGATE(FOnParticleBurstSignature, UParticleSystemComponent*, const FParticleEventBurstData&);

UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	~UParticleSystemComponent() override;

	void OnRegister() override;
	void OnUnregister() override;

	void Activate(bool bReset = false) override;
	void Deactivate() override;

	void InitializeSystem(UParticleSystem* InTemplate = nullptr);
	void ActivateSystem(bool bReset = false);
	// 생성만 멈춘다. 살아 있는 파티클은 수명이 끝날 때까지 계속 갱신
	void DeactivateSystem();
	void ResetParticles();

	void TickComponent(float DeltaTime) override;

	UParticleSystem* GetTemplate() const { return Template; }
	void SetTemplate(UParticleSystem* NewTemplate);
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; }
	bool HasCompleted() const { return bWasCompleted; }
	double GetSendRenderDataMs() const { return SendRenderDataMs; }
	double GetEventDispatchMs() const { return EventDispatchMs; }

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcBounds() const override;
	FBox CalcLocalBounds() const override;

	using Super::Serialize; // FArchive& 오버로드가 가려지지 않도록
	void Serialize(FStructuredArchive::FRecord Record) override;

	// 지금 모든 에미터 인스턴스가 쓰는 LOD
	int32 GetLODLevel() const { return LODLevel; }
	// LOD를 직접 정한다. 에셋이 Automatic이면 다음 거리 검사에서 다시 바뀐다
	void SetLODLevel(int32 InLODLevel);
	// 카메라 거리로 고른 LOD. 이번 프레임에 기록된 뷰가 없으면 -1
	int32 ComputeDesiredLOD() const;
	// 직전 프레임에 그린 카메라 중 가장 가까운 것까지의 거리. 뷰가 없으면 -1
	float GetDistanceToNearestView() const;

	// 디버그: 선택과 상관없이 모든 컴포넌트의 LOD 경계 구를 그린다 (콘솔 p.ShowLOD)
	static bool bShowAllLODDistances;

	// 에미터 인스턴스가 이번 프레임에 생긴 사건을 보고한다. 값으로 복사해 종류별 버퍼에 모은다
	void ReportEventSpawn(const FParticleEventSpawnData& Event) { SpawnEvents.Add(Event); }
	void ReportEventDeath(const FParticleEventDeathData& Event) { DeathEvents.Add(Event); }
	void ReportEventCollision(const FParticleEventCollisionData& Event) { CollisionEvents.Add(Event); }
	void ReportEventBurst(const FParticleEventBurstData& Event) { BurstEvents.Add(Event); }

	// 아직 배달하지 않은 이벤트. Tick 끝의 배달 단계에서 비운다
	const TArray<FParticleEventSpawnData>& GetSpawnEvents() const { return SpawnEvents; }
	const TArray<FParticleEventDeathData>& GetDeathEvents() const { return DeathEvents; }
	const TArray<FParticleEventCollisionData>& GetCollisionEvents() const { return CollisionEvents; }
	const TArray<FParticleEventBurstData>& GetBurstEvents() const { return BurstEvents; }
	int32 GetEmitterIndex(const FParticleEmitterInstance* Instance) const;

	// 디버그: 이벤트 수를 1초마다 로그 (콘솔 p.LogEvents)
	static bool bLogEvents;

	// 디버그: 충돌 지점과 법선을 잠깐 그린다 (콘솔 p.ShowCollisions). Generator 없이도 기록된다
	static bool bShowCollisions;
	struct FDebugCollision
	{
		FVector Location;
		FVector Normal;
		float TimeLeft;
	};
	void AddDebugCollision(const FVector& WorldLocation, const FVector& WorldNormal);
	const TArray<FDebugCollision>& GetDebugCollisions() const { return DebugCollisions; }

	// Receiver 배달이 끝난 뒤 Tick의 맨 마지막에 부른다.
	// 콜백 안에서 SetTemplate·Deactivate·액터 파괴를 해도 된다 (이후 이 컴포넌트를 건드리지 않는다)
	FOnParticleSpawnSignature OnParticleSpawn;
	FOnParticleDeathSignature OnParticleDeath;
	FOnParticleCollideSignature OnParticleCollide;
	FOnParticleBurstSignature OnParticleBurst;

	// 에디터 미리보기 전용 강제 LOD. -1이면 끔. 저장하지 않고 게임 월드에서는 무시한다
	int32 GetPreviewLODOverride() const { return PreviewLODOverride; }
	void SetPreviewLODOverride(int32 InLODLevel);

private:
	// 에미터마다 렌더 사본을 갱신해 프록시에 넘긴다
	void SendRenderDynamicData();
	// 에셋의 LODMethod에 따라 주기적으로 LOD를 다시 고른다
	void UpdateLODLevel(float DeltaTime);
	// 범위를 맞춰 저장하고 모든 인스턴스에 전달
	void ApplyLODLevel(int32 InLODLevel);
	bool IsPreviewLODActive() const;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	// EmitterInstances를 만든 템플릿. Template이 직렬화 등으로 바뀌면 이 값과 달라진다
	UParticleSystem* InitializedTemplate = nullptr;
	bool bWasCompleted = false;
	double SendRenderDataMs = 0.0;
	double EventDispatchMs = 0.0;

	int32 LODLevel = 0;
	float LODCheckTimer = 0.0f;
	int32 PreviewLODOverride = -1;
	// 활성화할 때 뷰가 없어 LOD를 못 정했으면 true
	bool bPendingActivateLOD = false;

	void ClearEvents();
	// 모인 이벤트를 각 에미터의 Receiver에 배달하고 버퍼를 비운다. 배달한 수를 돌려준다
	int32 DispatchEvents();
	// 방금 배달한 이벤트를 델리게이트와 소유 액터의 스크립트로 보낸다. Tick의 맨 마지막에서만 부른다
	void BroadcastGameEvents();
	void LogEventCounts(float DeltaTime);
	void ResetEventLog();

	TArray<FParticleEventSpawnData> SpawnEvents;
	TArray<FParticleEventDeathData> DeathEvents;
	TArray<FParticleEventCollisionData> CollisionEvents;
	TArray<FParticleEventBurstData> BurstEvents;

	// 배달 중인 목록. 배달하는 동안 새로 보고된 이벤트와 섞이지 않게 따로 둔다 (매 프레임 재할당을 피하려고 멤버)
	TArray<FParticleEventSpawnData> DispatchingSpawnEvents;
	TArray<FParticleEventDeathData> DispatchingDeathEvents;
	TArray<FParticleEventCollisionData> DispatchingCollisionEvents;
	TArray<FParticleEventBurstData> DispatchingBurstEvents;

	TArray<FDebugCollision> DebugCollisions;

	// p.LogEvents용 누적 ("종류:이름" → 개수). 템플릿이 바뀌면 비운다
	float EventLogTimer = 0.0f;
	std::map<FString, int32> LoggedEventCounts;
	FParticleEventCollisionData LastLoggedCollision;
	bool bHasLoggedCollision = false;
};

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected = nullptr);
// 선택한 액터(p.ShowLOD가 켜져 있으면 전부)의 다음 LOD 경계 거리를 위도·경도 선 구로 그린다
void DrawParticleLODDistances(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);