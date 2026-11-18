#include "EnginePCH.h"
#include "GameFramework/Animation/GundamAnimInstance.h"
#include "Asset/AssetManager.h"

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
