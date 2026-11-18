#pragma once

#include "GameFramework/Actor/MyCharacter.h"
#include "Math/RandomStream.h"
#include "SonicCharacter.generated.h"

class UParticleSystemComponent;
class USonicAnimInstance;

// 맞으면 도망가는 소닉 샌드백. 평소엔 Sad Idle, 맞으면 Head Hit을 재생하고 코인을 흩뿌린 뒤
// 때린 쪽 반대로 FleeDuration 동안 달아났다가 멈춰 서서 다시 Idle로 돌아간다. 도망가는 중에 맞으면 처음부터 반복한다.
// 애니메이션은 USonicAnimInstance(Idle / Run / Hit 상태 머신)가 맡고, 이동은 CharacterMovement로 직접 한다.
UCLASS()
class ASonicCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	ASonicCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetIsHit(AActor* Attacker = nullptr) override;

	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Sonic_SadIdle/Sonic_SadIdle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RunAnimationPath = "FBX/Sonic_Running/Sonic_Running.anim.bin";
	UPROPERTY(EditAnywhere)
	FString HitAnimationPath = "FBX/Sonic_HeadHit/Sonic_HeadHit.anim.bin";
	UPROPERTY(EditAnywhere)
	FString CoinParticlePath = "Particles/Coin.particle";

	// 피격 모션이 끝난 뒤 달아나는 시간 (초)
	UPROPERTY(EditAnywhere)
	float FleeDuration = 4.0f;
	// 달리기 클립 속도에 곱하는 배율. 1이면 발이 바닥에 맞는다
	UPROPERTY(EditAnywhere)
	float FleeSpeedScale = 1.0f;
	// 시작 위치에서 이 거리(m)보다 멀어지면 바깥으로는 더 가지 않고 옆으로 돈다
	UPROPERTY(EditAnywhere)
	float LeashRadius = 9.0f;
	// 도망 방향으로 몸을 돌리는 속도 (deg/s)
	UPROPERTY(EditAnywhere)
	float FleeTurnRate = 900.0f;
	// 멈춰 선 뒤 때린 쪽을 돌아보는 속도 (deg/s). 0이면 돌아보지 않는다
	UPROPERTY(EditAnywhere)
	float LookBackTurnRate = 240.0f;

	// 두 번째 피격부터 추가로 뿌리는 코인 개수 범위 (첫 피격은 템플릿의 Burst를 쓴다)
	UPROPERTY(EditAnywhere)
	int32 CoinBurstMin = 10;
	UPROPERTY(EditAnywhere)
	int32 CoinBurstMax = 16;

	UParticleSystemComponent* CoinParticle = nullptr;

private:
	void ApplyMovementSettings();
	void EnsureCoinTemplate();
	void SpawnCoins();
	USonicAnimInstance* GetSonicAnimInstance() const;
	// 도망 방향을 정리한다: 울타리(LeashRadius) 밖으로 나가려 하면 바깥 성분을 뺀다
	FVector GetLeashedFleeDirection() const;
	void TurnTowardYaw(float DesiredYaw, float MaxDelta);

	FRandomStream CoinRandom{ 7 };

	FVector HomeLocation = FVector::ZeroVector;
	FVector LastAttackerLocation = FVector::ZeroVector;
	bool bHasAttacker = false;
	FVector FleeDirection = FVector::ZeroVector;   // 수평 단위 벡터
	float FleeTimeLeft = 0.0f;
	float StuckTime = 0.0f;                        // 달리는데 속도가 안 나는 시간 (벽·기둥에 막힘)
};
