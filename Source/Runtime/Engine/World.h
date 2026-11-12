#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Engine/EngineTypes.h"
#include "Render/SceneRenderData.h"
#include "Script/CoroutineScheduler.h"
#include "World.generated.h"

class ULevel;
class FLineBatcher;
class UBillboardComponent;
class APawn;
class APlayerController;

struct FViewContext;

UCLASS()
class UWorld : public UObject
{
	GENERATED_BODY()

public:
	UWorld();
	virtual ~UWorld();

	bool Init();

	// 이 월드의 디버그 선(충돌 도형, 라이트 범위, 본 등)을 모아 그리는 도구. Init에서 만든다.
	// UE의 UWorld::LineBatcher와 같은 자리이고 UE에서도 public 멤버다.
	// UE는 선을 쌓아 두면 씬과 함께 그려지지만, 이 엔진은 쓰는 쪽이 BeginFrame, Add..., OnRender 순서로 직접 그린다.
	TUniquePtr<FLineBatcher> LineBatcher;
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);
	
	static bool AreAABBsOverlapping(const FBox& A, const FBox& B)
	{
		return
			A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
			A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y &&
			A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
	
	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float RealDeltaTime);

	// 모든 액터 Tick이 끝난 뒤 1회. 지난 프레임 겹침 쌍과 비교해 End → Begin 순으로 보낸다. 담당: C
	void UpdateOverlaps();

	void ClearWorld();


	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	const TArray<ULevel*>& GetLevels() const { return Levels; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }

	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix(*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
		FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr);
	// bBlockingOnly: bBlockComponent인 도형/메시만 검사한다. 기본값은 기존 투사체 동작을 유지한다.
	bool SweepSingle(const FRay& WorldRay, float MaxDistance, float Radius, FHitResult& OutHit, AActor* IgnoreActor = nullptr,
		bool bBlockingOnly = false);

	void BeginPlay();
	void EndPlay();
	bool HasBegunPlay() const { return bBegunPlay; }
	APawn* GetPlayerPawn() const { return PlayerPawn; }
	APlayerController* GetPlayerController() const { return PlayerController; }
	bool IsPaused() const { return bIsPaused; }
	void SetPaused(bool bPaused) { bIsPaused = bPaused; }

	FScene& GetScene() { return Scene; }
	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }
	FCoroutineScheduler& GetCoroutineScheduler() { return CoroutineScheduler; }


	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }
	bool IsGameWorld() const { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;


	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// Slomo-hitstop
	float GetEffectiveTimeDilation() const;
	void StartHitStop(float Duration, float Dilation);

private:

	void GameWorldTick(float DeltaTime);
	void EditorWorldTick(float DeltaTime);
	void PreviewWorldTick(float DeltaTime);

	EWorldType WorldType = EWorldType::None;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;
	FCoroutineScheduler CoroutineScheduler;

	TQueue<AActor*> BeginPlayList;

	//메인 카메라 
	ACameraActor* MainCamera = nullptr;
	APawn* PlayerPawn = nullptr;
	APlayerController* PlayerController = nullptr;
	bool bIsPaused = false;
	bool bBegunPlay = false;

	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;

	// Slomo-hitstop
	float GlobalTimeDilation = 1.0f;
	float HitStopTimeRemaining = 0.0f;	// 실제 시간 (초)
	float HitStopDilation = 1.0f;
	float TimeSeconds = 0.0f;			// 게임 시간 (dilated 누적)
	float RealTimeSeconds = 0.0f;		// 실제 시간 누적

	static constexpr float MinTimeDilation = 0.0001f;
	static constexpr float MaxTimeDilation = 20.0f;
};
