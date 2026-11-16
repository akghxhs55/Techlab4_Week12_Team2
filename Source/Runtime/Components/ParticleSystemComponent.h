#pragma once

#include "PrimitiveComponent.h"
#include "ParticleSystemComponent.generated.h"

class UWorld;
class FLineBatcher;
class AActor;
class UObject;
class UParticleSystem;
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
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; }
	bool HasCompleted() const { return bWasCompleted; }

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

private:
	// 에셋이 없을 때 쓰는 테스트 템플릿
	void CreateDefaultTemplate();
	void DestroyOwnedTemplate();

	UParticleSystem* Template = nullptr;
	bool bOwnsTemplate = false;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	bool bWasCompleted = false;
};

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected = nullptr);