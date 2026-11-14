#pragma once

#include "PrimitiveComponent.h"
#include "ParticleSystemComponent.generated.h"

class UObject;
class UParticleSystem;
struct FParticleEmitterInstance;

// Cascade 파티클 시스템 런타임 (UE UParticleSystemComponent)
// - Template(에셋)은 가리키기만 한다. CreateDefaultTemplate으로 직접 만든 경우만 소유.
// - EmitterInstances는 이 컴포넌트가 소유한다 (CreateInstance가 new로 만든 것).
UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	~UParticleSystemComponent() override;

	void OnRegister() override;
	void OnUnregister() override;

	// 엔진 활성화 흐름 (BeginPlay가 Activate()를 부른다). bReset = 처음부터 다시 시작
	void Activate(bool bReset = false) override;   // 기본값은 부모와 동일하게
	void Deactivate() override;

	// InTemplate이 null이 아니면 교체한 뒤 에미터 인스턴스를 새로 만든다
	void InitializeSystem(UParticleSystem* InTemplate = nullptr);
	void ActivateSystem(bool bReset = false);
	// 생성만 멈춘다. 살아 있는 파티클은 수명이 끝날 때까지 계속 갱신 (UE 동작)
	void DeactivateSystem();
	// 에미터 인스턴스 전부 해제
	void ResetParticles();

	void TickComponent(float DeltaTime) override;

	UParticleSystem* GetTemplate() const { return Template; }
	const TArray<FParticleEmitterInstance*>& GetEmitterInstances() const { return EmitterInstances; }
	bool HasCompleted() const { return bWasCompleted; }

private:
	// 에셋이 없을 때 쓰는 테스트 템플릿 (Required + Spawn + Lifetime + Velocity)
	void CreateDefaultTemplate();
	// CreateDefaultTemplate으로 만든 UObject 해제
	void DestroyOwnedTemplate();

	UParticleSystem* Template = nullptr;
	bool bOwnsTemplate = false;
	TArray<UObject*> OwnedObjects;   // bOwnsTemplate일 때 소멸자에서 delete

	TArray<FParticleEmitterInstance*> EmitterInstances;
	bool bWasCompleted = false;
};
