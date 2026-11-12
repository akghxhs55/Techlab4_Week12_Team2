#pragma once

#include "LightComponent.h"
#include "AmbientLightComponent.generated.h"

UCLASS()
class UAmbientLightComponent : public ULightComponent
{
	GENERATED_BODY()
public:
	UAmbientLightComponent();
	virtual ~UAmbientLightComponent() override;

	ELightType GetLightType() const override { return ELightType::Ambient; }
	
protected:
private:
};