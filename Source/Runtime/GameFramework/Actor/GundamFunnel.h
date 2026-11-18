#pragma once

#include "GameFramework/Actor.h"
#include "GundamFunnel.generated.h"

class UStaticMeshComponent;
class UParticleSystemComponent;
class UProjectileMovementComponent;

UCLASS()
class AGundamFunnel : public AActor
{
	GENERATED_BODY()

public:
	AGundamFunnel();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void Launch(const FVector& WorldVelocity, float InActiveDuration = 1.15f, float InTrailFadeOut = 0.35f);
	bool IsLaunched() const { return bLaunched; }

	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	UParticleSystemComponent* GetTrailParticleComponent() const { return TrailParticleComponent; }
	UProjectileMovementComponent* GetProjectileMovementComponent() const { return ProjectileMovementComponent; }

private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	UParticleSystemComponent* TrailParticleComponent = nullptr;
	UProjectileMovementComponent* ProjectileMovementComponent = nullptr;

	float ActiveDuration = 1.15f;
	float TrailFadeOutDuration = 0.35f;
	float ElapsedTime = 0.0f;
	bool bLaunched = false;
	bool bFadingOut = false;
};
