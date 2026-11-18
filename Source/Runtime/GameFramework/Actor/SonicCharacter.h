#pragma once

#include "GameFramework/Actor/MyCharacter.h"
#include "Math/RandomStream.h"
#include "SonicCharacter.generated.h"

class UParticleSystemComponent;

// 맞기만 하는 소닉 샌드백. 평소엔 Sad Idle, 맞으면 Head Hit을 한 번 재생하고 코인을 흩뿌린다.
// 애니메이션은 UMyAnimInstance(Locomotion / Hit 상태 머신)를 그대로 쓰고 경로만 소닉 클립으로 채운다.
UCLASS()
class ASonicCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	ASonicCharacter();

	virtual void BeginPlay() override;
	virtual void SetIsHit() override;

	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Sonic_SadIdle/Sonic_SadIdle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString HitAnimationPath = "FBX/Sonic_HeadHit/Sonic_HeadHit.anim.bin";
	UPROPERTY(EditAnywhere)
	FString CoinParticlePath = "Particles/Coin.particle";

	// 두 번째 피격부터 추가로 뿌리는 코인 개수 범위 (첫 피격은 템플릿의 Burst를 쓴다)
	UPROPERTY(EditAnywhere)
	int32 CoinBurstMin = 10;
	UPROPERTY(EditAnywhere)
	int32 CoinBurstMax = 16;

	UParticleSystemComponent* CoinParticle = nullptr;

private:
	void EnsureCoinTemplate();

	FRandomStream CoinRandom{ 7 };
};
