#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/Module/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Render/Mesh.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleStats.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Engine/ParticleSystemSceneProxy.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Module/ParticleModuleEventReceiverBase.h"
#include "UObject/UObjectHash.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bCastShadow = false;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

	// 에디터 월드는 BeginPlay가 없으므로 여기서 시작
	if (IsAutoActivate())
	{
		ActivateSystem();
	}
}

void UParticleSystemComponent::OnUnregister()
{
	ResetParticles();
	Super::OnUnregister();
}

void UParticleSystemComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	ActivateSystem(bReset);
}

void UParticleSystemComponent::Deactivate()
{
	// 부모가 Tick을 끄면 남은 파티클이 그 자리에 멈추므로 다시 켠다
	Super::Deactivate();
	SetComponentTickEnabled(true);
	DeactivateSystem();
}

void UParticleSystemComponent::InitializeSystem(UParticleSystem* InTemplate)
{
	ResetParticles();

	if (InTemplate)
	{
		Template = InTemplate;
	}
	InitializedTemplate = Template;
	if (!Template)
		return;

	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (!Emitter)
			continue;
		if (FParticleEmitterInstance* Instance = Emitter->CreateInstance(this))
		{
			EmitterInstances.Add(Instance);
		}
	}
	bWasCompleted = false;

	// 이전 템플릿에서 쌓인 이벤트와 로그 누적이 새 템플릿 이름으로 찍히지 않게 비운다
	ClearEvents();
	ResetEventLog();

	// 새 인스턴스는 LOD 0으로 시작하므로 지금 LOD를 다시 적용한다 (DirectSet 값, 미리보기 유지)
	ApplyLODLevel(IsPreviewLODActive() ? PreviewLODOverride : LODLevel);
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* NewTemplate)
{
	if (Template == NewTemplate && !EmitterInstances.IsEmpty())
	{
		return;
	}

	ResetParticles();
	Template = NewTemplate;
	bWasCompleted = false;

	if (!Template)
		return;

	if (IsRegistered() && (IsActive() || IsAutoActivate()))
	{
		ActivateSystem(true);
	}
	else
	{
		InitializeSystem();
	}
}

void UParticleSystemComponent::ActivateSystem(bool bReset)
{
	// 인스턴스가 다른 템플릿으로 만들어졌으면(씬 로드로 Template만 바뀐 경우 등) 다시 만든다
	if (EmitterInstances.IsEmpty() || bReset || InitializedTemplate != Template)
	{
		InitializeSystem();
	}

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = true;
	}
	bWasCompleted = false;

	// 첫 Tick 전에 거리로 정해 둔다. 멀리서 터진 폭발이 LOD 0 Burst로 시작하지 않게 한다
	LODCheckTimer = 0.0f;
	bPendingActivateLOD = false;
	if (Template && !IsPreviewLODActive() && Template->GetLODMethod() != EParticleSystemLODMethod::DirectSet)
	{
		const int32 DesiredLOD = ComputeDesiredLOD();
		if (DesiredLOD >= 0)
		{
			ApplyLODLevel(DesiredLOD);
		}
		else
		{
			// 아직 그려진 뷰가 없으면(시작 직후) 뷰가 생기는 첫 Tick에서 정한다
			bPendingActivateLOD = true;
		}
	}
}

void UParticleSystemComponent::DeactivateSystem()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = false;
	}
}

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.Empty();
	SendRenderDynamicData();
}

void UParticleSystemComponent::SendRenderDynamicData()
{
	FParticleSystemSceneProxy* Proxy = static_cast<FParticleSystemSceneProxy*>(GetSceneProxy());
	if (!Proxy)
		return;

	FScopeCycleCounter Timer{ TStatId{} };
	TArray<TUniquePtr<FDynamicEmitterDataBase>>& RenderData = Proxy->GetDynamicEmitterData();
	if (RenderData.Num() != EmitterInstances.Num())
	{
		RenderData.Empty();
		RenderData.SetNum(EmitterInstances.Num());
	}
	for (int32 Index = 0; Index < EmitterInstances.Num(); ++Index)
	{
		if (FParticleEmitterInstance* Instance = EmitterInstances[Index])
			Instance->UpdateDynamicData(RenderData[Index]);
		else
			RenderData[Index].reset();
	}
	SendRenderDataMs = SmoothParticleStatMs(SendRenderDataMs, Timer.Finish());
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// 이벤트 버퍼는 여기서 비우지 않는다. 지난 Tick의 배달 중에 생긴 이벤트(Receiver가 생성한 파티클의 Spawn 등)가
	// 이번 Tick에 이어서 배달돼야 한다. 배달한 이벤트는 DispatchEvents가 버퍼에서 꺼낸다

	// 디버그 충돌 표시는 잠깐 남겼다가 사라진다
	for (int32 i = DebugCollisions.Num() - 1; i >= 0; --i)
	{
		DebugCollisions[i].TimeLeft -= DeltaTime;
		if (DebugCollisions[i].TimeLeft <= 0.0f)
		{
			DebugCollisions.RemoveAt(static_cast<uint32>(i));
		}
	}

	if (EmitterInstances.IsEmpty())
		return;

	// 인스턴스가 이번 Tick을 새 LOD의 모듈로 돌도록 먼저 고른다
	UpdateLODLevel(DeltaTime);

	bool bAllComplete = true;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->Tick(DeltaTime);
		if (!Instance->IsComplete())
		{
			bAllComplete = false;
		}
	}
	bWasCompleted = bAllComplete;

	if (bLogEvents)
	{
		LogEventCounts(DeltaTime);
	}

	// 모든 에미터 Tick이 끝난 뒤에 배달한다. 순회 중인 파티클 배열을 Receiver가 건드리지 않게
	// Receiver가 ForceSpawn으로 만드는 파티클 비용도 여기 포함된다.
	// BroadcastGameEvents는 콜백이 이 컴포넌트를 파괴할 수 있어 재지 않는다
	FScopeCycleCounter DispatchTimer{ TStatId{} };
	const int32 Delivered = DispatchEvents();
	EventDispatchMs = SmoothParticleStatMs(EventDispatchMs, DispatchTimer.Finish());
	if (bLogEvents && Delivered > 0)
	{
		LoggedEventCounts["Delivered"] += Delivered;
	}

	// 바운드는 Transform이 바뀔 때만 갱신되므로 매 프레임 요청
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->GetScene()->MarkDirty(Proxy);
	}
	SendRenderDynamicData();

	// 게임 코드 콜백은 반드시 맨 마지막. 콜백이 이 컴포넌트를 바꾸거나 파괴할 수 있다
	BroadcastGameEvents();
}

void UParticleSystemComponent::BroadcastGameEvents()
{
	const bool bScripts = GetOwner() != nullptr;
	if (DispatchingCollisionEvents.IsEmpty() && DispatchingSpawnEvents.IsEmpty() &&
		DispatchingDeathEvents.IsEmpty() && DispatchingBurstEvents.IsEmpty())
		return;
	if (!bScripts && !OnParticleSpawn.IsBound() && !OnParticleDeath.IsBound() &&
		!OnParticleCollide.IsBound() && !OnParticleBurst.IsBound())
		return;

	// 콜백이 SetTemplate·Deactivate(이벤트 버퍼를 비움)나 액터 파괴를 할 수 있으므로
	// 보낼 목록을 지역 복사본으로 옮기고, 콜백마다 이 컴포넌트가 아직 살아 있는지 확인한다
	const TArray<FParticleEventSpawnData> Spawns = DispatchingSpawnEvents;
	const TArray<FParticleEventDeathData> Deaths = DispatchingDeathEvents;
	const TArray<FParticleEventCollisionData> Collisions = DispatchingCollisionEvents;
	const TArray<FParticleEventBurstData> Bursts = DispatchingBurstEvents;

	UParticleSystemComponent* Self = this;
	auto IsAlive = [Self]()
		{
			uint32 UUID = 0;
			return TryGetLiveObjectUUID(Self, nullptr, UUID);
		};

	// 닿은 대상도 앞선 콜백이 파괴할 수 있다(파괴는 즉시 delete). 지금은 모두 살아 있으므로 UUID를 적어 두고,
	// 콜백마다 같은 객체가 아직 살아 있는지 확인해 죽었으면 nullptr로 넘긴다. 같은 주소에 새 객체가 생겨도 UUID로 구분된다
	struct FHitIds { uint32 Actor = 0; uint32 Component = 0; };
	TArray<FHitIds> HitIds;
	HitIds.Reserve(Collisions.Num());
	for (const FParticleEventCollisionData& Event : Collisions)
	{
		FHitIds Ids;
		if (!Event.HitActor || !TryGetLiveObjectUUID(Event.HitActor, nullptr, Ids.Actor)) Ids.Actor = 0;
		if (!Event.HitComponent || !TryGetLiveObjectUUID(Event.HitComponent, nullptr, Ids.Component)) Ids.Component = 0;
		HitIds.Add(Ids);
	}
	auto IsSameLive = [](const UObject* Object, uint32 ExpectedUUID)
		{
			uint32 UUID = 0;
			return ExpectedUUID != 0 && TryGetLiveObjectUUID(Object, nullptr, UUID) && UUID == ExpectedUUID;
		};

	for (int32 i = 0; i < Collisions.Num(); ++i)
	{
		FParticleEventCollisionData Event = Collisions[i];
		if (!IsSameLive(Event.HitActor, HitIds[i].Actor)) Event.HitActor = nullptr;
		if (!IsSameLive(Event.HitComponent, HitIds[i].Component)) Event.HitComponent = nullptr;

		OnParticleCollide.Broadcast(this, Event);
		if (!IsAlive()) return;
		// 델리게이트가 파괴했을 수 있으므로 Lua에 넘기기 직전에 한 번 더 확인한다
		if (!IsSameLive(Event.HitActor, HitIds[i].Actor)) Event.HitActor = nullptr;
		// 소유 액터의 스크립트(Lua)에 OnParticleCollide(닿은 액터)로 넘긴다
		if (AActor* Owner = GetOwner())
		{
			Owner->CallScripts("OnParticleCollide", Event.HitActor);
			if (!IsAlive()) return;
		}
	}
	for (const FParticleEventSpawnData& Event : Spawns)
	{
		OnParticleSpawn.Broadcast(this, Event);
		if (!IsAlive()) return;
	}
	for (const FParticleEventDeathData& Event : Deaths)
	{
		OnParticleDeath.Broadcast(this, Event);
		if (!IsAlive()) return;
	}
	for (const FParticleEventBurstData& Event : Bursts)
	{
		OnParticleBurst.Broadcast(this, Event);
		if (!IsAlive()) return;
	}
}

bool UParticleSystemComponent::bLogEvents = false;
bool UParticleSystemComponent::bShowCollisions = false;

void UParticleSystemComponent::AddDebugCollision(const FVector& WorldLocation, const FVector& WorldNormal)
{
	// 파티클이 많아도 선이 넘치지 않게 개수를 제한한다
	constexpr int32 MaxDebugCollisions = 256;
	if (DebugCollisions.Num() >= MaxDebugCollisions)
		return;
	DebugCollisions.Add({ WorldLocation, WorldNormal, 0.5f });
}

int32 UParticleSystemComponent::GetEmitterIndex(const FParticleEmitterInstance* Instance) const
{
	for (int32 i = 0; i < EmitterInstances.Num(); ++i)
	{
		if (EmitterInstances[i] == Instance)
			return i;
	}
	return -1;
}

int32 UParticleSystemComponent::DispatchEvents()
{
	// 지난 Tick에 배달한 목록이 이번 Tick의 델리게이트로 다시 나가지 않게 먼저 비운다
	DispatchingSpawnEvents.Reset();
	DispatchingDeathEvents.Reset();
	DispatchingCollisionEvents.Reset();
	DispatchingBurstEvents.Reset();

	if (SpawnEvents.IsEmpty() && DeathEvents.IsEmpty() && CollisionEvents.IsEmpty() && BurstEvents.IsEmpty())
		return 0;

	// 배달할 목록을 꺼내고 버퍼를 비운다. 배달 중에 새로 생긴 이벤트는 버퍼에 쌓여 다음 Tick에 배달된다
	// (A → B → C 연쇄가 한 프레임에 한 단계씩만 진행되어, 서로를 계속 부르는 설정이어도 한 프레임에 끝난다)
	DispatchingSpawnEvents = SpawnEvents;
	DispatchingDeathEvents = DeathEvents;
	DispatchingCollisionEvents = CollisionEvents;
	DispatchingBurstEvents = BurstEvents;
	ClearEvents();

	int32 Delivered = 0;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		// 꺼진 LOD 레벨이면 이 에미터는 이벤트를 받지 않는다
		if (!Instance || !Instance->CurrentLODLevel || !Instance->CurrentLODLevel->IsEnabled())
			continue;

		// 먼 LOD에서 Receiver를 끄면 그 에미터는 이벤트를 받지 않는다
		for (UParticleModuleEventReceiverBase* Receiver : Instance->CurrentLODLevel->GetEventReceivers())
		{
			if (!Receiver || !Receiver->IsEnabled())
				continue;

			auto Deliver = [&](const auto& Events)
				{
					for (const auto& Event : Events)
					{
						if (Receiver->WillProcessEvent(Event))
						{
							Receiver->ProcessEvent(*Instance, Event);
							++Delivered;
						}
					}
				};
			Deliver(DispatchingSpawnEvents);
			Deliver(DispatchingDeathEvents);
			Deliver(DispatchingCollisionEvents);
			Deliver(DispatchingBurstEvents);
		}
	}
	return Delivered;
}

void UParticleSystemComponent::ClearEvents()
{
	SpawnEvents.Reset();
	DeathEvents.Reset();
	CollisionEvents.Reset();
	BurstEvents.Reset();
}

void UParticleSystemComponent::LogEventCounts(float DeltaTime)
{
	// 종류:이름 별로 1초 동안 누적 (같은 종류라도 Generator 설정마다 이름이 다르다)
	auto Count = [this](const char* TypeName, const FParticleEventData& Event)
		{
			const FString Name = Event.EventName.IsNone() ? FString("None") : Event.EventName.ToString();
			++LoggedEventCounts[FString(TypeName) + ":" + Name];
		};
	for (const FParticleEventSpawnData& Event : SpawnEvents) Count("Spawn", Event);
	for (const FParticleEventDeathData& Event : DeathEvents) Count("Death", Event);
	for (const FParticleEventCollisionData& Event : CollisionEvents) Count("Collision", Event);
	for (const FParticleEventBurstData& Event : BurstEvents) Count("Burst", Event);

	// 1초에 한 번, 그 프레임 충돌 하나의 내용도 보여 준다 (위치·법선 확인용)
	if (!CollisionEvents.IsEmpty())
	{
		LastLoggedCollision = CollisionEvents[0];
		bHasLoggedCollision = true;
	}

	EventLogTimer += DeltaTime;
	if (EventLogTimer < 1.0f)
		return;

	FString Summary;
	for (const auto& [Key, Value] : LoggedEventCounts)
	{
		Summary += (Summary.empty() ? "" : ", ") + Key + " " + std::to_string(Value);
	}
	KRS_LOG(Info, "Particle events / {:.1f}s ({}): {}", EventLogTimer,
		Template ? Template->GetAssetPath() : FString("None"), Summary.empty() ? FString("none") : Summary);

	if (bHasLoggedCollision)
	{
		const FParticleEventCollisionData& Hit = LastLoggedCollision;
		KRS_LOG(Info, "  sample collision '{}': emitter {} at ({:.0f}, {:.0f}, {:.0f}) normal ({:.0f}, {:.0f}, {:.0f}) t {:.2f}",
			Hit.EventName.ToString(), Hit.EmitterIndex, Hit.Location.X, Hit.Location.Y, Hit.Location.Z,
			Hit.Normal.X, Hit.Normal.Y, Hit.Normal.Z, Hit.Time);
	}
	ResetEventLog();
}

void UParticleSystemComponent::ResetEventLog()
{
	EventLogTimer = 0.0f;
	LoggedEventCounts.clear();
	bHasLoggedCollision = false;
}

void UParticleSystemComponent::SetLODLevel(int32 InLODLevel)
{
	ApplyLODLevel(InLODLevel);
}

void UParticleSystemComponent::SetPreviewLODOverride(int32 InLODLevel)
{
	PreviewLODOverride = InLODLevel < 0 ? -1 : InLODLevel;
	if (IsPreviewLODActive())
	{
		ApplyLODLevel(PreviewLODOverride);
	}
	else
	{
		// 미리보기를 끄면 다음 Tick에서 바로 거리로 다시 고르게 한다
		LODCheckTimer = Template ? Template->GetLODDistanceCheckTime() : 0.0f;
	}
}

bool UParticleSystemComponent::IsPreviewLODActive() const
{
	if (PreviewLODOverride < 0)
		return false;
	const UWorld* World = GetWorld();
	return !(World && World->IsGameWorld());
}

namespace
{
	// 여러 뷰 중 가장 가까운 카메라까지의 제곱거리 (가장 자세히 보이는 곳에 맞춘다). 뷰가 없으면 -1
	float NearestViewDistanceSq(const UParticleSystemComponent& Component)
	{
		const UWorld* World = Component.GetWorld();
		if (!World)
			return -1.0f;

		const TArray<FVector>& ViewLocations = World->GetViewLocationsRenderedLastFrame();
		if (ViewLocations.IsEmpty())
			return -1.0f;

		const FVector Location = Component.GetWorldLocation();
		float MinDistanceSq = FLT_MAX;
		for (const FVector& ViewLocation : ViewLocations)
		{
			const FVector Delta = ViewLocation - Location;
			const float DistanceSq = Delta.Dot(Delta);
			if (DistanceSq < MinDistanceSq)
			{
				MinDistanceSq = DistanceSq;
			}
		}
		return MinDistanceSq;
	}
}

bool UParticleSystemComponent::bShowAllLODDistances = false;

float UParticleSystemComponent::GetDistanceToNearestView() const
{
	const float DistanceSq = NearestViewDistanceSq(*this);
	return DistanceSq < 0.0f ? -1.0f : std::sqrt(DistanceSq);
}

int32 UParticleSystemComponent::ComputeDesiredLOD() const
{
	if (!Template)
		return -1;

	const float MinDistanceSq = NearestViewDistanceSq(*this);
	if (MinDistanceSq < 0.0f)
		return -1;

	// 제곱끼리 비교해 sqrt를 피한다
	const TArray<float>& Distances = Template->GetLODDistances();
	for (int32 i = Distances.Num() - 1; i > 0; --i)
	{
		if (MinDistanceSq >= Distances[i] * Distances[i])
			return i;
	}
	return 0;
}

void UParticleSystemComponent::UpdateLODLevel(float DeltaTime)
{
	if (!Template || IsPreviewLODActive())
		return;

	if (bPendingActivateLOD)
	{
		const int32 DesiredLOD = ComputeDesiredLOD();
		if (DesiredLOD >= 0)
		{
			ApplyLODLevel(DesiredLOD);
			bPendingActivateLOD = false;
			LODCheckTimer = 0.0f;
		}
		return;
	}

	// DirectSet은 코드가 정한 값, ActivateAutomatic은 활성화 때 정한 값을 유지한다
	if (Template->GetLODMethod() != EParticleSystemLODMethod::Automatic)
		return;

	LODCheckTimer += DeltaTime;
	if (LODCheckTimer < Template->GetLODDistanceCheckTime())
		return;
	LODCheckTimer = 0.0f;

	const int32 DesiredLOD = ComputeDesiredLOD();
	if (DesiredLOD >= 0 && DesiredLOD != LODLevel)
	{
		ApplyLODLevel(DesiredLOD);
	}
}

void UParticleSystemComponent::ApplyLODLevel(int32 InLODLevel)
{
	const int32 NumLODs = Template ? Template->GetNumLODs() : 1;
	const int32 OldLODLevel = LODLevel;
	LODLevel = (std::clamp)(InLODLevel, 0, (std::max)(NumLODs - 1, 0));

	if (LODLevel != OldLODLevel)
	{
		int32 ActiveParticles = 0;
		for (const FParticleEmitterInstance* Instance : EmitterInstances)
		{
			ActiveParticles += Instance ? Instance->ActiveParticles : 0;
		}
		KRS_LOG(Info, "Particle LOD {} -> {} ({}, active particles {})", OldLODLevel, LODLevel,
			Template ? Template->GetAssetPath() : FString("None"), ActiveParticles);
	}

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			Instance->SetCurrentLODIndex(LODLevel);
		}
	}
}

void UParticleSystemComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	// 액터 생성자가 기본 템플릿으로 인스턴스를 만들고, 씬 로드는 SpawnActor(등록·활성화) 뒤에 속성을 읽는다.
	// 그대로 두면 Template만 바뀌고 옛 템플릿의 에미터가 계속 도므로, SetTemplate과 같은 규칙으로 다시 시작한다
	if (Record.GetUnderlyingArchive().IsLoading() && InitializedTemplate != Template)
	{
		if (IsRegistered() && (IsActive() || IsAutoActivate()))
		{
			ActivateSystem(true);
		}
		else
		{
			InitializeSystem();
		}
	}
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

FBox UParticleSystemComponent::CalcBounds() const
{
	// 에미터가 자기 공간(Local/World)에 맞춰 월드 박스를 만들어 두므로 합치기만 한다
	FBox Bounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAny = false;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		Bounds.Expand(Instance->BoundingBox);
		bAny = true;
	}
	if (!bAny)
	{
		const FVector Location = GetWorldLocation();
		return FBox(Location - FVector(1.0f), Location + FVector(1.0f));
	}
	return Bounds;
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	// 로컬 박스를 요구하는 곳(디버그 박스)용. 회전이 있으면 실제보다 커진다
	return CalcBounds().GetWorldAABB(GetWorldMatrix().Inverse());
}

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher)
		return;

	constexpr float HalfLength = 3.0f;
	const FVector4 Color(1.0f, 1.0f, 0.0f, 1.0f);

	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* PSC = *It;
		if (!PSC || !PSC->IsRegistered() || PSC->GetWorld() != World)
			continue;
		if (Selected && PSC->GetOwner() != Selected)
			continue;

		for (FParticleEmitterInstance* Instance : PSC->GetEmitterInstances())
		{
			if (!Instance || Instance->ActiveParticles == 0)
				continue;

			ParticleHelper::ForEachParticle(*Instance, [&](FBaseParticle& Particle, uint8* /*ParticleBase*/)
				{
						const FVector P = Instance->SimulationToWorld(Particle.Location);

					LineBatcher->AddLine(P - FVector(HalfLength, 0, 0), P + FVector(HalfLength, 0, 0), Color);
					LineBatcher->AddLine(P - FVector(0, HalfLength, 0), P + FVector(0, HalfLength, 0), Color);
					LineBatcher->AddLine(P - FVector(0, 0, HalfLength), P + FVector(0, 0, HalfLength), Color);
				});
		}

		// 충돌 지점(빨간 십자)과 법선(초록 선)
		if (UParticleSystemComponent::bShowCollisions)
		{
			const FVector4 HitColor(1.0f, 0.2f, 0.2f, 1.0f);
			const FVector4 NormalColor(0.2f, 1.0f, 0.3f, 1.0f);
			constexpr float Cross = 4.0f;
			constexpr float NormalLength = 20.0f;
			for (const UParticleSystemComponent::FDebugCollision& Hit : PSC->GetDebugCollisions())
			{
				const FVector& P = Hit.Location;
				LineBatcher->AddLine(P - FVector(Cross, 0, 0), P + FVector(Cross, 0, 0), HitColor);
				LineBatcher->AddLine(P - FVector(0, Cross, 0), P + FVector(0, Cross, 0), HitColor);
				LineBatcher->AddLine(P, P + Hit.Normal * NormalLength, NormalColor);
			}
		}
	}
}

namespace
{
	// 위도 원과 경도 원으로 지구본처럼 그린다. 큰 구도 둥글게 보이도록 선을 촘촘히 둔다
	void AddWireGlobe(FLineBatcher& LineBatcher, const FVector& Center, float Radius, const FVector4& Color)
	{
		constexpr int32 LatitudeRings = 12;     // 극점 제외 위도 원 11개
		constexpr int32 MeridianCircles = 8;    // 경도 원 8개 = 경선 16개
		constexpr int32 Segments = 64;

		auto CirclePoint = [](float Angle) { return FVector(std::cos(Angle), std::sin(Angle), 0.0f); };

		for (int32 Ring = 1; Ring < LatitudeRings; ++Ring)
		{
			const float Latitude = PI * static_cast<float>(Ring) / LatitudeRings - PI * 0.5f;
			const float RingRadius = Radius * std::cos(Latitude);
			const FVector RingCenter = Center + FVector(0.0f, 0.0f, Radius * std::sin(Latitude));
			for (int32 i = 0; i < Segments; ++i)
			{
				const FVector A = CirclePoint(2.0f * PI * i / Segments) * RingRadius;
				const FVector B = CirclePoint(2.0f * PI * (i + 1) / Segments) * RingRadius;
				LineBatcher.AddLine(RingCenter + A, RingCenter + B, Color);
			}
		}

		for (int32 Meridian = 0; Meridian < MeridianCircles; ++Meridian)
		{
			const float Longitude = PI * static_cast<float>(Meridian) / MeridianCircles;
			const FVector Horizontal(std::cos(Longitude), std::sin(Longitude), 0.0f);
			for (int32 i = 0; i < Segments; ++i)
			{
				const float AngleA = 2.0f * PI * i / Segments;
				const float AngleB = 2.0f * PI * (i + 1) / Segments;
				const FVector A = Horizontal * (Radius * std::cos(AngleA)) + FVector(0.0f, 0.0f, Radius * std::sin(AngleA));
				const FVector B = Horizontal * (Radius * std::cos(AngleB)) + FVector(0.0f, 0.0f, Radius * std::sin(AngleB));
				LineBatcher.AddLine(Center + A, Center + B, Color);
			}
		}
	}
}

void DrawParticleLODDistances(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	const bool bAll = UParticleSystemComponent::bShowAllLODDistances;
	if (!World || !LineBatcher || (!Selected && !bAll))
		return;

	const FVector4 Color(0.2f, 1.0f, 0.3f, 1.0f);

	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* PSC = *It;
		if (!PSC || !PSC->IsRegistered() || PSC->GetWorld() != World)
			continue;
		if (!bAll && PSC->GetOwner() != Selected)
			continue;

		const UParticleSystem* Template = PSC->GetTemplate();
		if (!Template)
			continue;

		const FVector Center = PSC->GetWorldLocation();
		const TArray<float>& Distances = Template->GetLODDistances();
		if (Distances.Num() < 2)
			continue;

		// 경계 i는 LOD i-1과 LOD i 사이. 여러 개를 겹쳐 그리면 어지러우므로
		// 지금 LOD에서 다음 LOD로 넘어가는 바깥쪽 경계 하나만 그린다 (마지막 LOD면 안쪽 경계)
		const int32 CurrentLOD = PSC->GetLODLevel();
		const int32 Boundary = (std::min)(CurrentLOD + 1, Distances.Num() - 1);
		AddWireGlobe(*LineBatcher, Center, Distances[Boundary], Color);
	}
}