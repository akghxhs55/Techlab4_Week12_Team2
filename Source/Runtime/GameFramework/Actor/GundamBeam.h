#pragma once

#include "GameFramework/Actor.h"
#include "GundamBeam.generated.h"

class UParticleSystemComponent;

UCLASS()
class AGundamBeam : public AActor
{
	GENERATED_BODY()

public:
	AGundamBeam();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere)
	float LifeTime = 0.5f;

	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	float ElapsedTime = 0.0f;
};
