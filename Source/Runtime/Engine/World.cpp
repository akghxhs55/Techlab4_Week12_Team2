#include "EnginePCH.h"
#include "Render/LineBatcher.h"
#include "World.h"
#include "Level.h"

#include "Core/EngineStatics.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/InputSystem.h"

#include "UObject/UObjectIterator.h"

#include "Collision/Ray.h"
#include "Components/BillboardComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"

#include "Math/Frustum.h"
#include "Math/BVH.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <vector>

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"
#include "Render/ObjectConstants.h"

#include "UObject/UObjectGlobals.h"

#include "Script/LuaBindings.h"
#include "Physics/Collision.h"
#include "Components/ShapeComponent.h"

namespace
{
	UWorld::FOnWorldActorDestroyed GWorldActorDestroyedCallback;

	inline void WritePerObjectConstants(uint8* Dest, const FMatrix& LocalToWorld, const FMatrix& WorldToLocal, bool bReceiveShadow)
	{
		FPerObjectConstants Constants{};
		Constants.World = LocalToWorld;
		Constants.NormalMatrix = WorldToLocal.GetTransposed();
		Constants.ShadowFlags.X = bReceiveShadow ? 1.0f : 0.0f;
		std::memcpy(Dest, &Constants, sizeof(Constants));
	}
}

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick); // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정

UWorld::UWorld()
	: CoroutineScheduler(*this)
{
}

UWorld::~UWorld()
{

}

bool UWorld::Init()
{
	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		KRS_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	//레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	//카메라 생성
	CreateMainCamera();

	// 디버그 선 도구를 만든다. FLineBatcher::Init의 렌더러 인자는 쓰이지 않는다.
	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(nullptr, this);

	return true;
}

AActor* UWorld::DuplicateActor(AActor* Source)
{
	if (!Source || bBegunPlay) return nullptr;
	FObjectDuplicationParameters Params(Source, PersistentLevel);
	Params.DestName = MakeUniqueObjectName(PersistentLevel, Source->GetClass(), Source->GetFName());
	AActor* Copy = Cast<AActor>(StaticDuplicateObjectEx(Params));
	if (!Copy) return nullptr;
	Copy->World = this;
	Copy->Level = PersistentLevel;
	PersistentLevel->AddActor(Copy);
	Copy->RegisterAllComponents();
	return Copy;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (!Class) return nullptr;
	if (!Class->IsChildOf(AActor::StaticClass())) return nullptr;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(PersistentLevel, Class, InName);

	if (!NewActor)
	{
		KRS_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor에 World/Level 연결
	NewActor->World = this;
	NewActor->Level = PersistentLevel;

	// 3. Transform 적용
	const FTransform SpawnTransform = Transform ? *Transform : FTransform::Identity;

	if (NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(SpawnTransform);
	}
	
	// 4. Level->Actors에 등록한 뒤 컴포넌트를 월드에 연결한다.
	PersistentLevel->AddActor(NewActor);
	NewActor->RegisterAllComponents();

	// 월드가 재생 중일 때 생성된 액터만 별도 BeginPlay 대기열에 넣는다.
	if (bBegunPlay) BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float RealDeltaTime)
{
	bIsTicking = true;

	// Hit Stop 시간 흐름
	if (HitStopTimeRemaining > 0.0f)
	{
		HitStopTimeRemaining = std::max(HitStopTimeRemaining - RealDeltaTime, 0.0f);
	}

	const float DeltaTime = RealDeltaTime * GetEffectiveTimeDilation();
	RealTimeSeconds += RealDeltaTime;
	++TickCount;

	// World 시간 흐름
	if (!bIsPaused)
	{
		TimeSeconds += DeltaTime;
	}

	// 모든 SceneComponent의 Transform 갱신
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	if (IsGameWorld())
	{
		GameWorldTick(DeltaTime);
	}

	if (WorldType == EWorldType::Editor)
	{
		EditorWorldTick(DeltaTime);
	}
	
	if (WorldType == EWorldType::EditorPreview)
	{
		PreviewWorldTick(DeltaTime);
	}

	// 월드 Tick에서 변경된 변환과 바운드를 렌더링 전에 반영합니다.
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	// Tick 안의 모든 순회(틱 함수, 오브젝트 이터레이터, 오버랩 콜백)가 끝났으므로 이제 해제해도 안전하다.
	bIsTicking = false;
	FlushPendingDestroyActors();
}

void UWorld::AddRenderedViewLocation(const FVector& ViewLocation)
{
	// 새 Tick 뒤 첫 기록이면 직전 프레임 목록을 버리고 이번 프레임 것으로 다시 모은다
	if (ViewLocationsTickCount != TickCount)
	{
		ViewLocationsRenderedLastFrame.Empty();
		ViewLocationsTickCount = TickCount;
	}
	if (ViewLocationsRenderedLastFrame.Num() < MaxRenderedViewLocations)
	{
		ViewLocationsRenderedLastFrame.Add(ViewLocation);
	}
}

void UWorld::GameWorldTick(float DeltaTime)
{
	// 새로운 Actor들의 BeginPlay 실행
	while (!BeginPlayList.IsEmpty())
	{
		AActor* Actor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (Actor)
		{
			Actor->BeginPlay();
		}
	}

	// 일시 정지 상태라면, 스킵
	if (bIsPaused)
	{
		return;
	}

	// 모든 Actor를 도는 대신 등록된 Tick 함수만 실행한다
	{
		SCOPE_CYCLE_COUNTER(STAT_ActorTick);
		TickTaskManager.RunAllTickGroups(DeltaTime);
	}

	// 코루틴 Tick
	CoroutineScheduler.Tick(DeltaTime);

	// ShapeComponent 충돌 처리
	UpdateOverlaps();

	// Lua Script 제거 처리
	FLuaBindings::FlushPendingDestroy(this);

	// 플레이어 카메라 업데이트
	if (PlayerController)
	{
		PlayerController->UpdateCameraManager(DeltaTime);
	}
}

void UWorld::EditorWorldTick(float DeltaTime)
{
	// Editor World에서 실행

	// 에디터 월드에서도 파티클 시뮬레이션은 틱이 돌아야 한다 (게임 Tick이 없으므로 직접 호출)
	for (TObjectIterator<UParticleSystemComponent> Comp; Comp; ++Comp)
	{
		// 다른 월드(PIE, 프리뷰) 소속이거나 등록 안 된 컴포넌트는 제외 (PIE 컴포넌트 이중 Tick 방지)
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}
		Comp->TickComponent(DeltaTime);
	}

	// 스프링 암 컴포넌트도 에디터에서 틱이 돌아야 한다
	for (TObjectIterator<USpringArmComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		Comp->UpdateInEditor();
	}


	for (TObjectIterator<UDecalComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}
		Comp->UpdateInEditor();
	}
}

void UWorld::PreviewWorldTick(float DeltaTime)
{
	// 스킨드 메시 컴포넌트의 본이 수정되면 갱신되어야 함
	for (TObjectIterator<USkinnedMeshComponent> Comp; Comp; ++Comp)
	{
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}

		Comp->TickComponent(DeltaTime);
	}

	for (TObjectIterator<UParticleSystemComponent> Comp; Comp; ++Comp)
	{
		// 다른 월드(PIE, 프리뷰) 소속이거나 등록 안 된 컴포넌트는 제외 (PIE 컴포넌트 이중 Tick 방지)
		if (!Comp || !Comp->IsRegistered() || Comp->GetWorld() != this)
		{
			continue;
		}
		Comp->TickComponent(DeltaTime);
	}
}

void UWorld::ClearWorld()
{
	// 지연 삭제 대기 중인 액터는 이미 레벨에서 빠져 있으므로 따로 해제한다.
	FlushPendingDestroyActors();

	EndPlay();

	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}

	// 레벨 액터를 메인 카메라로 쓰고 있었다면 ClearActors가 지운 뒤 댕글링이 되므로 기본 카메라로 되돌린다.
	if (MainCamera && MainCamera->GetLevel())
	{
		MainCamera = nullptr;
		CreateMainCamera();
	}

	for (ULevel* Level : Levels)
	{
		for (AActor* Actor : Level->Actors)
		{
			if (!Actor) continue;
			if (Actor->HasBegunPlay())
				Actor->EndPlay();
			Actor->UnregisterAllComponents();
			Actor->RegisterAllActorTickFunctions(false);

			// DestroyActor와 마찬가지로 에디터 등 외부 구독자가 포인터를 정리할 기회를 준다.
			if (GWorldActorDestroyedCallback)
			{
				GWorldActorDestroyedCallback(this, Actor);
			}
		}
		Level->ClearActors();
	}
	// 액터 EndPlay에서 새로 등록한 작업까지 최종 정리한다.
	CoroutineScheduler.Reset();
	// 프록시와 틱 등록은 위 루프에서 액터마다 delete 전에 이미 풀었다. 여기서는 그 경로로 빠지지 않고
	// FScene에 남은 프록시를 마저 비운다. 남겨 두면 지워진 컴포넌트를 가리키는 프록시가 다음 프레임에 터진다.
	Scene.RemoveAllLights();
	Scene.RemoveAllDecals();
	Scene.RemoveAllPrimitives();
	KRS_LOG(Info, "ClearWorld : {} actors remain", PersistentLevel->GetActorNum());
}

// 메인 카메라 생성
void UWorld::CreateMainCamera()
{
	if (MainCamera)
		return;

	MainCamera = NewObject<ACameraActor>();

	if (!MainCamera)
	{
		KRS_LOG(Error, "Failed to create MainCamera");
		return;
	}

	MainCamera->World = this;
	MainCamera->Level = nullptr;
	MainCamera->GetCameraComponent()->SetRelativeLocation(FVector(-5.0f, -5.0f, 5.0f));

	// 메인 카메라는 Level에 속하지 않아 BeginPlay를 거치지 않으므로 여기서 등록한다.
	MainCamera->RegisterAllActorTickFunctions(true);
}

void UWorld::SetMainCamera(ACameraActor* Camera)
{
	if (MainCamera == Camera)
		return;

	if (MainCamera)
		MainCamera->RegisterAllActorTickFunctions(false);

	MainCamera = Camera;

	if (MainCamera)
	{
		MainCamera->World = this;
		MainCamera->RegisterAllActorTickFunctions(true);
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

void UWorld::SetOnActorDestroyedCallback(FOnWorldActorDestroyed InCallback)
{
	GWorldActorDestroyedCallback = std::move(InCallback);
}

bool UWorld::IsPendingDestroy(const AActor* Actor) const
{
	return Actor && PendingDestroyActors.Contains(const_cast<AActor*>(Actor));
}

void UWorld::ReleaseWorldReferences(AActor* Actor)
{
	if (!Actor)
		return;

	// 컨트롤러가 사라질 폰을 계속 빙의하고 있지 않게 먼저 푼다. 폰이 아직 살아 있을 때 UnPossessed가 불린다.
	if (PlayerController && PlayerController->GetPawn() == Actor)
	{
		PlayerController->UnPossess();
	}
	if (PlayerPawn == Actor)
	{
		PlayerPawn = nullptr;
	}
	if (PlayerController == Actor)
	{
		PlayerController->ClearInputState();
		PlayerController->UnPossess();
		PlayerController = nullptr;
	}
	if (MainCamera == Actor)
	{
		// 렌더러는 메인 카메라가 항상 있다고 가정하므로 기본 카메라를 다시 만든다.
		MainCamera = nullptr;
		CreateMainCamera();
	}
}

void UWorld::FlushPendingDestroyActors()
{
	if (PendingDestroyActors.IsEmpty())
		return;

	// 소멸자 안에서 다시 Destroy가 불려도 목록이 꼬이지 않게 옮겨 놓고 해제한다.
	TArray<AActor*> ToDelete = std::move(PendingDestroyActors);
	PendingDestroyActors.Reset();
	for (AActor* Actor : ToDelete)
	{
		delete Actor;
	}
}

bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor)
		return false;

	// 이번 Tick에 이미 정리된 액터. 두 번 EndPlay/delete되지 않게 막는다.
	if (IsPendingDestroy(Actor))
		return true;

	ULevel* Level = Actor->GetLevel();

	if (!Level)
		return false;

	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 2. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	ReleaseWorldReferences(Actor);

	if (Actor->HasBegunPlay())
		Actor->EndPlay();

	// 3. 프록시 제거
	Actor->UnregisterAllComponents();

	Actor->RegisterAllActorTickFunctions(false);

	// 에디터 등 외부 구독자가 댕글링 포인터를 남기지 않도록 실제 메모리 해제 직전에 통지
	if (GWorldActorDestroyedCallback)
	{
		GWorldActorDestroyedCallback(this, Actor);
	}

	// 4. Actor 삭제. Tick 중이면 호출자(자기 Tick, 오버랩 콜백, 이터레이터 루프)가
	// 아직 이 포인터를 들고 있을 수 있으므로 메모리 해제만 Tick 끝으로 미룬다.
	if (bIsTicking)
	{
		PendingDestroyActors.Add(Actor);
	}
	else
	{
		delete Actor;
	}

	KRS_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor,
	bool bBlockingOnly)
{
	OutHit = FHitResult(1.0f);
	if (MaxDistance <= 0.0f || !std::isfinite(MaxDistance) || Radius < 0.0f || !std::isfinite(Radius)) return false;
	Scene.UpdateAllTransforms();
	float NearestT = MaxDistance;
	const FVector Extent(Radius, Radius, Radius);
	if (bBlockingOnly)
	{
		// 도형은 렌더 프록시가 없으므로 직접 검사한다. 표시 여부는 충돌 여부와 별개다.
		// 기존 메시 Sweep과 같은 확장 AABB 방식이며, 바닥/트리거는 bBlockComponent=false로 둔다.
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			UPrimitiveComponent* Component = *It;
			AActor* Owner = Component->GetOwner();
			if (!Component->IsRegistered() || Component->GetWorld() != this || !Component->bBlockComponent ||
				!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision()) continue;
			if (!Cast<UShapeComponent>(Component) && !Component->GetMeshData()) continue;

			const FBox Bounds = Component->CalcBounds();
			const FVector Min = Bounds.Min - Extent, Max = Bounds.Max + Extent;
			float Enter = 0.0f, Exit = NearestT;
			FVector Normal = -WorldRay.Direction;
			bool bIntersects = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Origin = WorldRay.Origin[Axis], Direction = WorldRay.Direction[Axis];
				if (std::abs(Direction) <= SMALL_NUMBER)
				{
					if (Origin < Min[Axis] || Origin > Max[Axis]) { bIntersects = false; break; }
					continue;
				}
				float Near = (Min[Axis] - Origin) / Direction, Far = (Max[Axis] - Origin) / Direction;
				if (Near > Far) std::swap(Near, Far);
				if (Near > Enter)
				{
					Enter = Near;
					Normal = FVector::ZeroVector;
					Normal[Axis] = Direction > 0.0f ? -1.0f : 1.0f;
				}
				Exit = std::min(Exit, Far);
				if (Enter > Exit) { bIntersects = false; break; }
			}
			if (!bIntersects) continue;
			NearestT = Enter;
			OutHit.bBlockingHit = true;
			OutHit.bStartPenetrating = Enter == 0.0f;
			OutHit.HitComponent = Component;
			OutHit.Distance = Enter;
			OutHit.Time = Enter / MaxDistance;
			OutHit.Location = WorldRay.PointAt(Enter);
			OutHit.ImpactPoint = OutHit.Location - Normal * Radius;
			OutHit.Normal = OutHit.ImpactNormal = Normal;
		}
		return OutHit.bBlockingHit;
	}
	const FPreparedRay PreparedRay(WorldRay);
	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT)
		{
			return RayIntersectsAABB(PreparedRay, Bounds.Min - Extent, Bounds.Max + Extent, OutEnterT);
		},
		[&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;
			if (!Component || !Component->IsShown(true) ||
				!Component->GetOwner() || !Component->GetOwner()->GetActorEnableCollision() ||
				Component->GetOwner() == IgnoreActor || Component->GetOwner()->GetWorld() != this)
				return false;
			if (!Proxy->GetMesh())
			{
				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};
				float T = InOutNearestT;
				if (!Component->LineTraceComponentLocal(LocalRay, T) || T > InOutNearestT) return false;
				OutHit.bBlockingHit = true;
				OutHit.HitComponent = Component;
				OutHit.Distance = T;
				OutHit.Time = T / MaxDistance;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				OutHit.Location = OutHit.ImpactPoint;
				OutHit.ImpactNormal = -WorldRay.Direction;
				OutHit.Normal = OutHit.ImpactNormal;
				InOutNearestT = T;
				return true;
			}

			FAABB Bounds = Proxy->GetBounds();
			Bounds.Extent += Extent;
			float T = InOutNearestT;
			if (!RayIntersectsAABB(WorldRay, Bounds.Center - Bounds.Extent, Bounds.Center + Bounds.Extent, T) ||
				T > InOutNearestT) return false;

			const FVector Point = WorldRay.Origin + WorldRay.Direction * T;
			const FVector Offset = Point - Bounds.Center;
			const float X = Bounds.Extent.X > 0.0f ? Offset.X / Bounds.Extent.X : 0.0f;
			const float Y = Bounds.Extent.Y > 0.0f ? Offset.Y / Bounds.Extent.Y : 0.0f;
			const float Z = Bounds.Extent.Z > 0.0f ? Offset.Z / Bounds.Extent.Z : 0.0f;
			const float AX = std::abs(X), AY = std::abs(Y), AZ = std::abs(Z);
			FVector Normal = AX >= AY && AX >= AZ ? FVector(X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
				: AY >= AZ ? FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f)
				: FVector(0.0f, 0.0f, Z >= 0.0f ? 1.0f : -1.0f);
			if (Normal.Dot(WorldRay.Direction) > 0.0f) return false;

			OutHit.bBlockingHit = true;
			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.Time = T / MaxDistance;
			OutHit.ImpactPoint = Point;
			OutHit.Location = Point;
			OutHit.ImpactNormal = Normal;
			OutHit.Normal = Normal;
			InOutNearestT = T;
			return true;
		}, NearestT);
	return OutHit.bBlockingHit;
}

void UWorld::GatherSweepCandidates(const FBox& Bounds, const AActor* IgnoreActor, TArray<FPrimitiveSceneProxy*>& OutCandidates)
{
	OutCandidates.Reset();
	// 이번 프레임 한 번만. 파티클마다 부르지 않는다
	Scene.UpdateAllTransforms();

	Scene.BVH.Query(
		[&Bounds](const FBox& NodeBounds)
		{
			return NodeBounds.Min.X <= Bounds.Max.X && NodeBounds.Max.X >= Bounds.Min.X &&
				NodeBounds.Min.Y <= Bounds.Max.Y && NodeBounds.Max.Y >= Bounds.Min.Y &&
				NodeBounds.Min.Z <= Bounds.Max.Z && NodeBounds.Max.Z >= Bounds.Min.Z;
		},
		[&](FPrimitiveSceneProxy* Proxy)
		{
			// SweepSingle의 메시 경로와 같은 조건. 메시가 없는 프록시(파티클, 빌보드)는 제외
			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;
			if (!Component || !Proxy->GetMesh() || !Component->IsShown(true))
				return;
			const AActor* Owner = Component->GetOwner();
			if (!Owner || Owner == IgnoreActor || !Owner->GetActorEnableCollision() || Owner->GetWorld() != this)
				return;
			OutCandidates.Add(Proxy);
		});
}

bool UWorld::SweepAgainstCandidates(const TArray<FPrimitiveSceneProxy*>& Candidates, const FRay& WorldRay,
	float MaxDistance, float Radius, FHitResult& OutHit)
{
	OutHit = FHitResult(1.0f);
	if (Candidates.IsEmpty() || MaxDistance <= 0.0f)
		return false;

	const FVector Extent(Radius, Radius, Radius);
	float NearestT = MaxDistance;
	for (FPrimitiveSceneProxy* Proxy : Candidates)
	{
		FAABB Bounds = Proxy->GetBounds();
		Bounds.Extent += Extent;
		const FVector Min = Bounds.Center - Bounds.Extent;
		const FVector Max = Bounds.Center + Bounds.Extent;

		float T = NearestT;
		if (!RayIntersectsAABB(WorldRay, Min, Max, T) || T > NearestT)
			continue;

		// 시작점이 이미 박스 안 → 법선을 믿을 수 없으므로 호출자가 건너뛰게 표시
		const FVector& O = WorldRay.Origin;
		const bool bInside = O.X > Min.X && O.X < Max.X && O.Y > Min.Y && O.Y < Max.Y && O.Z > Min.Z && O.Z < Max.Z;

		const FVector Point = WorldRay.Origin + WorldRay.Direction * T;
		const FVector Offset = Point - Bounds.Center;
		const float X = Bounds.Extent.X > 0.0f ? Offset.X / Bounds.Extent.X : 0.0f;
		const float Y = Bounds.Extent.Y > 0.0f ? Offset.Y / Bounds.Extent.Y : 0.0f;
		const float Z = Bounds.Extent.Z > 0.0f ? Offset.Z / Bounds.Extent.Z : 0.0f;
		const float AX = std::abs(X), AY = std::abs(Y), AZ = std::abs(Z);
		const FVector Normal = AX >= AY && AX >= AZ ? FVector(X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
			: AY >= AZ ? FVector(0.0f, Y >= 0.0f ? 1.0f : -1.0f, 0.0f)
			: FVector(0.0f, 0.0f, Z >= 0.0f ? 1.0f : -1.0f);
		// 면에서 멀어지는 방향이면 이미 빠져나가는 중
		if (!bInside && Normal.Dot(WorldRay.Direction) > 0.0f)
			continue;

		NearestT = T;
		OutHit.bBlockingHit = true;
		OutHit.bStartPenetrating = bInside;
		OutHit.HitComponent = Proxy->GetComponent();
		OutHit.Distance = T;
		OutHit.Time = T / MaxDistance;
		OutHit.ImpactPoint = Point;
		OutHit.Location = Point;
		OutHit.ImpactNormal = Normal;
		OutHit.Normal = Normal;
	}
	return OutHit.bBlockingHit;
}

bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
	FBillboardTraceTransform ResolveBillboard, const void* ViewContext)
{
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
			{
				if (!Proxy->IsVisible())
					return false;

				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};

				float T = InOutNearestT;
				if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
					return false;

				OutHit.HitComponent = Proxy->GetComponent();
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;

			if (!Component || !Component->IsVisible())
				return false;

			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				if (!ResolveBillboard)
				{
					FHitResult Hit;
					if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
						Hit.Distance >= InOutNearestT)
					{
						return false;
					}

					OutHit = Hit;
					InOutNearestT = Hit.Distance;
					return true;
				}

				const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

				const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

				float T = InOutNearestT;
				if (!Billboard->LineTraceComponentLocal(LocalRay, T))
				{
					return false;
				}

				OutHit.HitComponent = Billboard;
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!Component->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT) { return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT) { return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
	if (!IsGameWorld() || bBegunPlay) return;

	//TODO : PlayerPawn, PlayerController Class 설정을 GameMode로 옮겨 수정 가능하게 바꿔야 한다.
	if (!PlayerPawn)
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (APawn* Pawn = Cast<APawn>(Actor)) { PlayerPawn = Pawn; break; }
		}
	}
	if (!PlayerPawn)
	{
		FTransform Transform = MainCamera ? MainCamera->GetActorTransform() : FTransform::Identity;
		PlayerPawn = SpawnActor<ADefaultPawn>(NAME_None, &Transform);
	}
	if (!PlayerController) 
		PlayerController = SpawnActor<AMyPlayerController>();
	
	if (PlayerController)
		PlayerController->SpawnPlayerCameraManager();
	
	if (PlayerController && PlayerPawn) PlayerController->Possess(PlayerPawn);

	// 기존 액터 목록을 고정한 후 시작한다. BeginPlay 중 생성된 액터는 대기열로 간다.
	TArray<AActor*> InitialActors;
	for (ULevel* Level : Levels)
		if (Level)
			for (AActor* Actor : Level->GetActors())
				if (Actor) InitialActors.Add(Actor);
	bBegunPlay = true;
	for (AActor* Actor : InitialActors)
		Actor->BeginPlay();

	// BeginPlay 중 생성된 액터도 PIE 입력 전에 시작한다.
	while (!BeginPlayList.IsEmpty())
	{
		AActor* Actor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();
		if (Actor) Actor->BeginPlay();
	}
}

void UWorld::EndPlay()
{
	CoroutineScheduler.Reset();
	bBegunPlay = false;
	if (PlayerController)
	{
		PlayerController->ClearInputState();
		PlayerController->UnPossess();
	}
	PlayerController = nullptr;
	PlayerPawn = nullptr;
	bIsPaused = false;
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->Init();

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld * InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);

	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
				SceneComp->MarkTransformDirty();

		Actor->RegisterAllComponents();

	}

	Scene.BuildBVH();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));
}

void UWorld::UpdateOverlaps()
{
	// 1. 이 월드에서 살아 있고 등록된 모든 충돌 도형을 수집한다.
	// bGenerateOverlapEvents가 false인 도형도 넣는다.
	// 그래야 실행 중 이벤트를 껐을 때 기존 Overlap에 End를 보낼 수 있다.
	TArray<UShapeComponent*> Shapes;

	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;

		if (!Shape ||
			!Shape->IsRegistered() ||
			Shape->GetWorld() != this ||
			!Shape->GetOwner())
		{
			continue;
		}

		Shapes.Add(Shape);
	}

	// 2. 도형은 렌더용 Scene.BVH에 없으므로 이번 프레임의 도형 전용 BVH를 만든다.
	std::vector<int32> Indices;
	std::unordered_map<UPrimitiveComponent*, int32> ShapeIndices;
	Indices.reserve(Shapes.Num());
	ShapeIndices.reserve(Shapes.Num());
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		Indices.push_back(i);
		ShapeIndices.emplace(Shapes[i], i);
	}

	TBVH<int32> OverlapBVH([&](const int32& Index) -> FBox
		{
			return Shapes[Index]->GetWorldAABB();
		});
	OverlapBVH.Build(std::span<const int32>(Indices.data(), Indices.size()));

	std::vector<int32> Candidates;
	for (int32 i = 0; i < Shapes.Num(); ++i)
	{
		UShapeComponent* A = Shapes[i];
		// 앞선 쌍의 오버랩 콜백에서 Destroy되어 등록 해제된 도형. 메모리는 Tick 끝까지 살아 있지만
		// 여기서 새 겹침을 만들면 곧 해제될 도형이 상대의 OverlapInfos에 남는다.
		if (!A->IsRegistered())
			continue;
		AActor* OwnerA = A->GetOwner();

		Candidates.clear();
		const FBox QueryBounds = A->GetWorldAABB();
		OverlapBVH.Query(
			[&](const FBox& Bounds) { return AreAABBsOverlapping(QueryBounds, Bounds); },
			[&](int32 j)
			{
				if (j > i) Candidates.push_back(j);
			});

		// BVH 후보에서 빠진 이전 겹침도 검사해야 분리/충돌 설정 해제 시 End를 보낼 수 있다.
		for (const FOverlapInfo& Info : A->GetOverlapInfos())
		{
			const auto It = ShapeIndices.find(Info.OtherComp);
			if (It != ShapeIndices.end() && It->second > i)
				Candidates.push_back(It->second);
		}

		// 후보 중복을 없애고 기존 i < j 순서대로 이벤트를 전달한다.
		std::sort(Candidates.begin(), Candidates.end());
		Candidates.erase(std::unique(Candidates.begin(), Candidates.end()), Candidates.end());

		for (int32 j : Candidates)
		{
			// 콜백으로 A나 B가 이번 루프 도중 등록 해제될 수 있다. 해제 시 OverlapInfos는 이미 정리되었다.
			if (!A->IsRegistered())
				break;
			UShapeComponent* B = Shapes[j];
			if (!B->IsRegistered())
				continue;
			AActor* OwnerB = B->GetOwner();

			// 같은 Actor의 콜라이더끼리는 여기서는 무시한다.
			// 필요하면 나중에 self-collision 정책을 별도로 추가한다.
			const bool bCanGenerateOverlap =
				OwnerA &&
				OwnerB &&
				OwnerA != OwnerB &&
				OwnerA->GetActorEnableCollision() &&
				OwnerB->GetActorEnableCollision() &&
				A->bGenerateOverlapEvents &&
				B->bGenerateOverlapEvents;

			bool bIsOverlappingNow = false;

			if (bCanGenerateOverlap)
			{
				// 3. 값싼 AABB 검사에서 먼저 탈락시킨다.
				const FBox BoundsA = A->GetWorldAABB();
				const FBox BoundsB = B->GetWorldAABB();

				if (AreAABBsOverlapping(BoundsA, BoundsB))
				{
					// 4. 실제 도형 충돌: Sphere/Box/Capsule 조합
					bIsOverlappingNow = FCollision::Overlap(A, B);
				}
			}

			// 5. 지난 프레임의 상태
			const bool bWasOverlapping =
				A->IsOverlappingComponent(B);

			// false -> true: 첫 접촉
			if (!bWasOverlapping && bIsOverlappingNow)
			{
				A->BeginComponentOverlap(B);
				B->BeginComponentOverlap(A);

				// 지금은 “처음 막혔을 때 한 번” Hit를 보내는 정책
				if (A->bBlockComponent && B->bBlockComponent)
				{
					A->DispatchHit(B);
					B->DispatchHit(A);
				}
			}
			// true -> false: 분리
			else if (bWasOverlapping && !bIsOverlappingNow)
			{
				A->EndComponentOverlap(B);
				B->EndComponentOverlap(A);
			}
		}
	}
}

float UWorld::GetEffectiveTimeDilation() const
{
	const float HitStop = (HitStopTimeRemaining > 0.0f) ? HitStopDilation : 1.0f;
	return FMath::Clamp(GlobalTimeDilation * HitStop, MinTimeDilation, MaxTimeDilation);
}

void UWorld::StartHitStop(float Duration, float Dilation)
{
	// 진행 중인 히트스탑이 있으면 더 강한(느린) 쪽을 유지한다.
	// 약하고 짧은 히트스탑이 들어와 진행 중인 강한 히트스탑을 덮어쓰지 않게 한다.
	if (HitStopTimeRemaining > 0.0f)
	{
		HitStopDilation = std::min(HitStopDilation, Dilation);
	}
	else
	{
		HitStopDilation = Dilation;
	}
	HitStopTimeRemaining = std::max(HitStopTimeRemaining, Duration);
}
