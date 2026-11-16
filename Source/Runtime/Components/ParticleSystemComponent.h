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
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; }
	bool HasCompleted() const { return bWasCompleted; }

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FBox CalcLocalBounds() const override;

private:
	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	bool bWasCompleted = false;
};

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected = nullptr);