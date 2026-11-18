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
	FString ShootAnimationPath = "FBX/Strike_Freedom_Shoot/Strike_Freedom_Shoot.anim.bin";

	void RequestShoot();
	void SetWingBoostTarget(float TargetAlpha) { WingBoostTarget = TargetAlpha; }

private:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeEvaluateAnimation(FPoseContext& Output) override;

	bool bIsShooting = false;
	bool bWingBonesCached = false;
	float WingBoostAlpha = 0.0f;
	float WingBoostTarget = 0.0f;

	// [0..4]: L root, L_1A, L_1B, L_2A, L_2B / [5..9]: R root, R_1A, R_1B, R_2A, R_2B
	int32 WingBoneIndices[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

	FAnimNode_StateMachine   StateMachine;
	FAnimNode_SequencePlayer IdlePlayer;
	FAnimNode_SequencePlayer ShootPlayer;
};
