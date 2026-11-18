#include "EnginePCH.h"
#include "GameFramework/Animation/GundamAnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include <algorithm>
#include <cmath>

namespace
{
	void ApplyParentAxisDelta(FTransform& BoneTransform, float RollDeg, float YawDeg)
	{
		const FQuat DeltaRoll = FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), FMath::DegreesToRadians(RollDeg));
		const FQuat DeltaYaw = FQuat::MakeFromAxisAngle(FVector(0.0f, 0.0f, 1.0f), FMath::DegreesToRadians(YawDeg));
		BoneTransform.Rotation = (DeltaYaw * DeltaRoll * BoneTransform.Rotation).Normalized();
	}
}

void UGundamAnimInstance::RequestShoot()
{
	bIsShooting = true;
	ShootPlayer.CurrentTime = 0.0f;
	ShootPlayer.PrevTime = 0.0f;
}

void UGundamAnimInstance::NativeInitializeAnimation()
{
	IdlePlayer.Sequence = IdleAnimationPath.empty() ? nullptr : UAssetManager::LoadAnimSequence(IdleAnimationPath);
	IdlePlayer.bLooping = true;

	ShootPlayer.Sequence = ShootAnimationPath.empty() ? nullptr : UAssetManager::LoadAnimSequence(ShootAnimationPath);
	ShootPlayer.bLooping = false;

	const int32 IdleStateIndex = StateMachine.AddState("Idle", &IdlePlayer);

	FAnimStateDesc ShootStateDesc;
	ShootStateDesc.Name = "Shoot";
	ShootStateDesc.Pose.SetLinkNode(&ShootPlayer);
	ShootStateDesc.OnExit = [this](UAnimInstance*) { bIsShooting = false; };
	const int32 ShootStateIndex = StateMachine.AddState(ShootStateDesc);

	StateMachine.SetInitialState(IdleStateIndex);

	FAnimTransitionDesc IdleToShoot;
	IdleToShoot.From = IdleStateIndex;
	IdleToShoot.To = ShootStateIndex;
	IdleToShoot.CrossfadeDuration = 0.06f;
	IdleToShoot.Rule = [this](const FAnimTransitionContext&) { return bIsShooting; };
	StateMachine.AddTransition(IdleToShoot);

	FAnimTransitionDesc ShootToIdle;
	ShootToIdle.From = ShootStateIndex;
	ShootToIdle.To = IdleStateIndex;
	ShootToIdle.CrossfadeDuration = 0.18f;
	ShootToIdle.Rule = [this](const FAnimTransitionContext&) { return ShootPlayer.IsFinished(); };
	StateMachine.AddTransition(ShootToIdle);

	RootNode = &StateMachine;
}

void UGundamAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Alpha = 1.0f - std::exp(-10.0f * DeltaSeconds);
	WingBoostAlpha += (WingBoostTarget - WingBoostAlpha) * Alpha;
}

void UGundamAnimInstance::NativeEvaluateAnimation(FPoseContext& Output)
{
	const TArray<FBoneInfo>& RefBones = Output.GetRefSkeleton();
	if (!bWingBonesCached && !RefBones.IsEmpty())
	{
		static constexpr const char* WingNames[10] = {
			"Wing_L", "Wing_L_1A", "Wing_L_1B", "Wing_L_2A", "Wing_L_2B",
			"Wing_R", "Wing_R_1A", "Wing_R_1B", "Wing_R_2A", "Wing_R_2B"
		};
		for (int32 i = 0; i < 10; ++i)
		{
			WingBoneIndices[i] = -1;
			for (int32 b = 0; b < RefBones.Num(); ++b)
			{
				if (RefBones[b].Name == WingNames[i])
				{
					WingBoneIndices[i] = b;
					break;
				}
			}
		}
		bWingBonesCached = true;
	}

	const int32 NumPoseBones = Output.Pose.GetNumBones();
	const float Spread = 0.92f;

	// 날개 전체를 부채꼴로 펼치기 위한 (RollDeg, YawDeg) 테이블
	struct FWingSpreadOffset
	{
		float RollDeg;
		float YawDeg;
	};
	static constexpr FWingSpreadOffset Offsets[10] = {
		{ -22.0f, -12.0f }, // Wing_L
		{ -38.0f,  -4.0f }, // Wing_L_1A
		{ -16.0f,  -2.0f }, // Wing_L_1B
		{   2.0f,   2.0f }, // Wing_L_2A
		{  10.0f,   4.0f }, // Wing_L_2B
		{  22.0f,  12.0f }, // Wing_R
		{  38.0f,   4.0f }, // Wing_R_1A
		{  16.0f,   2.0f }, // Wing_R_1B
		{  -2.0f,  -2.0f }, // Wing_R_2A
		{ -10.0f,  -4.0f }, // Wing_R_2B
	};

	for (int32 i = 0; i < 10; ++i)
	{
		const int32 BoneIdx = WingBoneIndices[i];
		if (BoneIdx >= 0 && BoneIdx < NumPoseBones)
		{
			ApplyParentAxisDelta(Output.Pose[BoneIdx], Offsets[i].RollDeg * Spread, Offsets[i].YawDeg * Spread);
		}
	}
}
