#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "GameFramework/Animation/MixamoAnimNodes.h"
#include "SonicAnimInstance.generated.h"

// 소닉 샌드백용 AnimInstance. Idle / Run / Hit 상태 머신.
// 이동은 캐릭터가 CharacterMovement로 직접 하고, Run 클립은 제자리 포즈로만 재생한다 (진행 성분은 포즈에서 뺀다).
// Hit은 Any State 전환이다. 맞는 도중에 또 맞아도 처음부터 다시 재생되도록 같은 클립을 쓰는 Hit 상태를 둘(HitA / HitB) 두고 번갈아 들어간다.
// (상태 머신은 자기 자신으로 가는 Any State 전환을 막으므로, 다른 슬롯으로 넘어가며 크로스페이드한다)
UCLASS()
class USonicAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Sonic_SadIdle/Sonic_SadIdle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RunAnimationPath = "FBX/Sonic_Running/Sonic_Running.anim.bin";
	UPROPERTY(EditAnywhere)
	FString HitAnimationPath = "FBX/Sonic_HeadHit/Sonic_HeadHit.anim.bin";

	// Hit 클립이 끝나기 이만큼 전부터 다음 상태(Run / Idle)로 섞어 나간다
	UPROPERTY(EditAnywhere)
	float HitBlendOutTime = 0.2f;
	// Run 클립이 제자리라 속도를 잴 수 없을 때 쓰는 달리기 속도 (cm/s)
	UPROPERTY(EditAnywhere)
	float FallbackRunSpeed = 450.0f;

	// 피격 요청. 다음 Update에서 Any State 전환으로 Hit에 들어가며 요청이 소비된다
	void RequestHit() { bHitRequested = true; }
	// Hit이 끝난 뒤 Run으로 갈지, 지금 달리는 중이면 계속 달릴지
	void SetWantsToRun(bool bInWantsToRun) { bWantsToRun = bInWantsToRun; }

	bool IsInHit() const;
	bool IsRunning() const;
	// 캐릭터가 이 속도로 움직이면 발이 덜 미끄러진다 (cm/s)
	float GetRunSpeed() const { return RunSpeed; }

private:
	bool bHitRequested = false;
	bool bWantsToRun = false;
	float RunSpeed = 450.0f;

	int32 IdleState = INDEX_NONE;
	int32 RunState = INDEX_NONE;
	int32 HitStateA = INDEX_NONE;
	int32 HitStateB = INDEX_NONE;
	bool bGraphBuilt = false;

	FAnimNode_StateMachine         StateMachine;
	FAnimNode_MixamoSequencePlayer IdlePlayer;
	FAnimNode_MixamoSequencePlayer RunPlayer;
	FAnimNode_MixamoSequencePlayer HitPlayerA;
	FAnimNode_MixamoSequencePlayer HitPlayerB;

	void AddHitState(FName Name, FAnimNode_MixamoSequencePlayer& Player, int32& OutState, int32 AnyPriority);

	virtual void NativeInitializeAnimation() override;
};
