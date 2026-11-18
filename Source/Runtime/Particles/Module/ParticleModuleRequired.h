#pragma once

#include "Core/Types.h"
#include "Render/Material.h"
#include "Particles/ParticleModule.h"
#include "ParticleModuleRequired.generated.h"

UENUM()
enum class EParticleSubUVInterpMethod : uint8
{
	None,
	Linear,
	Linear_Blend,
	Random,
	Random_Blend,
};

UENUM()
enum class EParticleSortMode : uint8
{
	None,
	ViewProjDepth,
	DistanceToView,
	Age_OldestFirst,
	Age_NewestFirst,
};

// 에미터 전체 규칙 . 파티클을 직접 건드리지 않으므로 Spawn/Update 플래그는 둘 다 false.
// LODLevel의 RequiredModule 슬롯에만 둔다
UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	EModuleType GetModuleType() const override { return EPMT_Required; }

	float GetEmitterDuration() const { return EmitterDuration; }
	int32 GetEmitterLoops() const { return EmitterLoops; }
	float GetEmitterDelay() const { return EmitterDelay; }
	bool  IsDelayFirstLoopOnly() const { return bDelayFirstLoopOnly; }
	bool  IsUseLocalSpace() const { return bUseLocalSpace; }
	// 동시에 살아 있을 수 있는 최대 파티클 수. 0이면 제한 없음
	int32 GetMaxActiveParticles() const { return MaxActiveParticles; }

	// 이번 루프에 적용할 지연
	float GetDelayForLoop(int32 LoopIndex) const
	{
		return (bDelayFirstLoopOnly && LoopIndex > 0) ? 0.0f : EmitterDelay;
	}

	EParticleSubUVInterpMethod GetSubUVMethod() const { return InterpolationMethod; }
	int32 GetSubImagesHorizontal() const { return SubImages_Horizontal; }
	int32 GetSubImagesVertical() const { return SubImages_Vertical; }
	int32 GetTotalSubImages() const { return SubImages_Horizontal * SubImages_Vertical; }
	float GetRandomImageTime() const { return RandomImageTime; }
	bool UsesSubUV() const { return InterpolationMethod != EParticleSubUVInterpMethod::None && GetTotalSubImages() > 1; }

	void SetSubUV(EParticleSubUVInterpMethod InMethod, int32 InHorizontal, int32 InVertical, float InRandomImageTime)
	{
		InterpolationMethod = InMethod;
		SubImages_Horizontal = InHorizontal > 0 ? InHorizontal : 1;
		SubImages_Vertical = InVertical > 0 ? InVertical : 1;
		RandomImageTime = InRandomImageTime;
	}

	UMaterial* GetMaterial() const { return Material; }
	void SetMaterial(UMaterial* InMaterial) { Material = InMaterial; }
	EParticleSortMode GetSortMode() const { return SortMode; }
	void SetSortMode(EParticleSortMode InSortMode) { SortMode = InSortMode; }

private:
	// 한 루프 길이. 0 이하면 루프 없이 계속 생성
	UPROPERTY(EditAnywhere)
	float EmitterDuration = 1.0f;

	// 반복 횟수. 0이면 무한 
	UPROPERTY(EditAnywhere)
	int32 EmitterLoops = 0;

	// 루프 시작 전 대기 시간. 이 동안은 생성하지 않는다
	UPROPERTY(EditAnywhere)
	float EmitterDelay = 0.0f;

	UPROPERTY(EditAnywhere)
	bool bDelayFirstLoopOnly = false;

	// true면 파티클이 컴포넌트를 따라 움직인다
	UPROPERTY(EditAnywhere)
	bool bUseLocalSpace = false;

	UPROPERTY(EditAnywhere)
	EParticleSubUVInterpMethod InterpolationMethod = EParticleSubUVInterpMethod::None;

	UPROPERTY(EditAnywhere)
	int32 SubImages_Horizontal = 1;

	UPROPERTY(EditAnywhere)
	int32 SubImages_Vertical = 1;

	// Random 모드에서 칸을 다시 뽑는 간격 (RelativeTime 단위). 0이면 매 프레임, 1 이상이면 생성 때 한 번
	UPROPERTY(EditAnywhere)
	float RandomImageTime = 0.0f;

	UPROPERTY(EditAnywhere)
	UMaterial* Material = nullptr;

	UPROPERTY(EditAnywhere)
	EParticleSortMode SortMode = EParticleSortMode::None;

	// 동시에 살아 있을 수 있는 최대 파티클 수. 0이면 제한 없음 (메모리 한도 65535까지).
	// LOD 레벨마다 Required가 따로 있으므로 LOD별 상한이 된다. 상한에 닿으면 새 파티클을 버린다
	UPROPERTY(EditAnywhere)
	int32 MaxActiveParticles = 0;
};
