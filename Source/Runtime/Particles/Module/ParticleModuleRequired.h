#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "ParticleModuleRequired.generated.h"

// 에미터 전체 규칙 . 파티클을 직접 건드리지 않으므로 Spawn/Update 플래그는 둘 다 false.
// LODLevel의 RequiredModule 슬롯에만 둔다
UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	float GetEmitterDuration() const { return EmitterDuration; }
	int32 GetEmitterLoops() const { return EmitterLoops; }
	float GetEmitterDelay() const { return EmitterDelay; }
	bool  IsDelayFirstLoopOnly() const { return bDelayFirstLoopOnly; }
	bool  IsUseLocalSpace() const { return bUseLocalSpace; }

	// 이번 루프에 적용할 지연
	float GetDelayForLoop(int32 LoopIndex) const
	{
		return (bDelayFirstLoopOnly && LoopIndex > 0) ? 0.0f : EmitterDelay;
	}

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
};
