#pragma once

#include "ParticleModule.h"
#include "ParticleModuleRequired.generated.h"

class UMaterial;

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	UMaterial* GetMaterial() const { return Material; }
	void SetMaterial(UMaterial* InMaterial) { Material = InMaterial; }

private:
	UMaterial* Material = nullptr;
	float EmitterDuration = 1.0f;
};
