#pragma once
#include "Particles/Module/ParticleModuleTypeDataBase.h"
#include "Particles/Distributions.h"
#include "Render/Mesh.h"
#include "ParticleModuleTypeDataMesh.generated.h"

UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:

	UParticleModuleTypeDataMesh();
	FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent) override;
	void BakeDistributions() override;

	void SetMesh(UStaticMesh* InMesh) { Mesh = InMesh; }
	UStaticMesh* GetMesh() const { return Mesh; }
	void SetCastShadow(bool bInCastShadow) { bCastShadows = bInCastShadow; }
	bool IsCastShadow() const { return bCastShadows; }
	bool IsOverrideMaterial() const { return bOverrideMaterial; }
	void SetOverrideMaterial(bool bInOverride) { bOverrideMaterial = bInOverride; }
	// 파티클이 생길 때마다 읽으므로 복사하지 않도록 참조로 반환
	const FVectorDistribution& GetRollPitchYaw() const { return RollPitchYaw; }
	FVectorDistribution& GetRollPitchYaw() { return RollPitchYaw; }

private:
	UPROPERTY(EditAnywhere)
	UStaticMesh* Mesh = nullptr;
	UPROPERTY(EditAnywhere)
	bool bCastShadows = false;
	// true면 메시 섹션 머티리얼 대신 Required 모듈의 머티리얼로 모든 섹션을 그린다
	UPROPERTY(EditAnywhere)
	bool bOverrideMaterial = false;
	// 생성 시 초기 회전(도). X = Roll, Y = Pitch, Z = Yaw
	UPROPERTY(EditAnywhere)
	FVectorDistribution RollPitchYaw;
};
