#pragma once

#include "PrimitiveComponent.h"
#include "Particles/ParticleSystem.h"
#include "ParticleSystemComponent.generated.h"

class UWorld;
class FLineBatcher;
class AActor;
class UObject;
struct FParticleEmitterInstance;

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

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

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

	// 에디터 미리보기 전용 강제 LOD. -1이면 끔. 저장하지 않고 게임 월드에서는 무시한다
	int32 GetPreviewLODOverride() const { return PreviewLODOverride; }
	void SetPreviewLODOverride(int32 InLODLevel);

private:
	// 에셋의 LODMethod에 따라 주기적으로 LOD를 다시 고른다
	void UpdateLODLevel(float DeltaTime);
	// 범위를 맞춰 저장하고 모든 인스턴스에 전달
	void ApplyLODLevel(int32 InLODLevel);
	bool IsPreviewLODActive() const;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	bool bWasCompleted = false;

	int32 LODLevel = 0;
	float LODCheckTimer = 0.0f;
	int32 PreviewLODOverride = -1;
	// 활성화할 때 뷰가 없어 LOD를 못 정했으면 true
	bool bPendingActivateLOD = false;
};

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected = nullptr);
// 선택한 액터(p.ShowLOD가 켜져 있으면 전부)의 다음 LOD 경계 거리를 위도·경도 선 구로 그린다
void DrawParticleLODDistances(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);