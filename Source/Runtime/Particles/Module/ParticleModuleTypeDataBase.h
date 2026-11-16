#pragma once
#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "ParticleModuleTypeDataBase.generated.h"

class UParticleSystemComponent;
class UParticleEmitter;
struct FParticleEmitterInstance;

// 에미터 종류(Mesh, Beam, Ribbon ...)를 정하는 모듈. 없으면 스프라이트
// LODLevel의 TypeDataModule 슬롯에만 둔다
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()
public:
	EModuleType GetModuleType() const override { return EPMT_TypeData; }

	// 종류에 맞는 인스턴스를 만들고 Init까지 한다. 호출자가 소유
	// nullptr이면 에미터가 기본(스프라이트) 인스턴스를 만든다
	virtual FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent);
};
