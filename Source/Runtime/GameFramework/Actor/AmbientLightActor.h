#pragma once

#include "GameFramework/Actor/LightActorBase.h"
#include "Components/AmbientLightComponent.h"
#include "AmbientLightActor.generated.h"

UCLASS()
class AAmbientLightActor : public ALightActorBase
{
	GENERATED_BODY()


public:
	AAmbientLightActor();
	virtual ~AAmbientLightActor() override = default;

    virtual ULightComponent* GetLightComponent() const { return AmbientLightComponent; }
    UAmbientLightComponent* GetAmbientLightComponent() const { return AmbientLightComponent; }

private:
    UAmbientLightComponent* AmbientLightComponent = nullptr;

};
