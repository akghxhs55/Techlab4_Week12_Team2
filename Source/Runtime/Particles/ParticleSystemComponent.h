#pragma once

#include "Components/PrimitiveComponent.h"
#include "ParticleEmitterInstance.h"
#include "Components/BillboardComponent.h"
#include "ParticleSystemComponent.generated.h"

class UParticleSystem;

UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	virtual ~UParticleSystemComponent() override;

	UParticleSystemComponent(const UParticleSystemComponent&) = delete;
	UParticleSystemComponent& operator=(const UParticleSystemComponent&) = delete;
	UParticleSystemComponent(UParticleSystemComponent&&) = delete;
	UParticleSystemComponent& operator=(UParticleSystemComponent&&) = delete;

	void SetTemplate(UParticleSystem* InTemplate);

	void InitParticles();
	void ResetParticles();

	void TickComponent(float DeltaTime) override;

	virtual FBox CalcLocalBounds() const override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

private:
	UParticleSystem* Template = nullptr;
	TArray<TUniquePtr<FParticleEmitterInstance>> EmitterInstances;

	bool bSuppressSpawning = false;

	UStaticMesh* QuadMesh = nullptr;
	TArray<FSubUVConstants> Constants;
};
