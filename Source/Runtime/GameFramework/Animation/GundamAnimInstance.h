#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "GundamAnimInstance.generated.h"

UCLASS()
class UGundamAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Strike_Freedom/Strike_Freedom.anim.bin";

	UPROPERTY(EditAnywhere)
	FString ShootAnimationPath = "FBX/Strike_Freedom/Strike_Freedom_Shoot.anim.bin";

	void RequestShoot();

private:
	virtual void NativeInitializeAnimation() override;

	bool bIsShooting = false;

	FAnimNode_StateMachine   StateMachine;
	FAnimNode_SequencePlayer IdlePlayer;
	FAnimNode_SequencePlayer ShootPlayer;
};
