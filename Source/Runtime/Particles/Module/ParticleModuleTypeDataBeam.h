#pragma once
#include "Particles/Module/ParticleModuleTypeDataBase.h"
#include <algorithm>
#include "ParticleModuleTypeDataBeam.generated.h"

UENUM()
enum class EBeamMethod : uint8
{
	Distance,
	Target,
};

UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent) override;

	EBeamMethod GetBeamMethod() const { return BeamMethod; }
	int32 GetMaxBeamCount() const { return std::max(1, MaxBeamCount); }
	int32 GetSheets() const { return std::max(1, Sheets); }
	int32 GetSegments() const { return std::max(1, Segments); }
	float GetTextureTile() const { return TextureTile; }
	float GetDistance() const { return Distance; }
	const FVector& GetTargetPoint() const { return TargetPoint; }

private:
	UPROPERTY(EditAnywhere)
	EBeamMethod BeamMethod = EBeamMethod::Distance;

	UPROPERTY(EditAnywhere)
	int32 MaxBeamCount = 1;

	UPROPERTY(EditAnywhere)
	int32 Sheets = 1;

	UPROPERTY(EditAnywhere)
	int32 Segments = 10;     // UE의 InterpolationPoints 역할

	UPROPERTY(EditAnywhere)
	float TextureTile = 1.f; // 빔 전체 길이에 텍스처가 몇 번 반복되는지

	UPROPERTY(EditAnywhere)
	float Distance = 100.f;  // Distance 모드: 에미터의 X축 방향으로 이 거리만큼

	UPROPERTY(EditAnywhere)
	FVector TargetPoint;     // Target 모드: 에미터 기준 위치
};
