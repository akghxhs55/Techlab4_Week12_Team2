#include "EnginePCH.h"
#include "GameFramework/Animation/SonicAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Asset/AssetManager.h"

namespace
{
	UAnimSequence* LoadSequence(const FString& Path)
	{
		return Path.empty() ? nullptr : UAssetManager::Get().LoadAnimSequence(Path);
	}
}

void USonicAnimInstance::NativeInitializeAnimation()
{
	// UAnimInstance::InitializeAnimation이 RootNode를 비우므로 그래프는 한 번만 만들고 RootNode만 다시 건다
	RootNode = &StateMachine;
	if (bGraphBuilt)
	{
		return;
	}
	bGraphBuilt = true;

	// 모든 클립은 제자리로 재생한다. 실제 이동은 캐릭터의 CharacterMovement가 한다
	IdlePlayer.Sequence = LoadSequence(IdleAnimationPath);
	IdlePlayer.bLooping = true;
	IdlePlayer.bExtractRootMotion = false;

	RunPlayer.Sequence = LoadSequence(RunAnimationPath);
	RunPlayer.bLooping = true;
	RunPlayer.bExtractRootMotion = false;
	const float ClipSpeed = RunPlayer.Sequence ? MixamoRootMotion::GetTravelSpeed(RunPlayer.Sequence) : 0.0f;
	RunSpeed = ClipSpeed > 1.0f ? ClipSpeed : FallbackRunSpeed;
	KRS_LOG(Info, "[Sonic] run speed {} cm/s (clip {})", RunSpeed, ClipSpeed);

	IdleState = StateMachine.AddState("Idle", &IdlePlayer);
	RunState = StateMachine.AddState("Run", &RunPlayer);
	// A를 먼저 보고(우선순위가 더 작다), A에 있을 때는 자기 자신 전환이 빠지므로 B로 간다
	AddHitState("HitA", HitPlayerA, HitStateA, -2);
	AddHitState("HitB", HitPlayerB, HitStateB, -1);

	StateMachine.SetInitialState(IdleState);

	FAnimTransitionDesc IdleToRun;
	IdleToRun.From = IdleState;
	IdleToRun.To = RunState;
	IdleToRun.CrossfadeDuration = 0.15f;
	IdleToRun.Rule = [this](const FAnimTransitionContext&) { return bWantsToRun; };
	StateMachine.AddTransition(IdleToRun);

	FAnimTransitionDesc RunToIdle;
	RunToIdle.From = RunState;
	RunToIdle.To = IdleState;
	RunToIdle.CrossfadeDuration = 0.3f;
	RunToIdle.Rule = [this](const FAnimTransitionContext&) { return !bWantsToRun; };
	StateMachine.AddTransition(RunToIdle);
}

void USonicAnimInstance::AddHitState(FName Name, FAnimNode_MixamoSequencePlayer& Player, int32& OutState, int32 AnyPriority)
{
	Player.Sequence = LoadSequence(HitAnimationPath);
	Player.bLooping = false;
	Player.bExtractRootMotion = false;

	FAnimStateDesc Desc;
	Desc.Name = Name;
	Desc.Pose.SetLinkNode(&Player);
	// 들어오는 순간 요청을 소비한다. 이후에 또 맞으면 다시 켜져 다른 Hit 슬롯으로 넘어간다
	Desc.OnEnter = [this](UAnimInstance*) { bHitRequested = false; };
	OutState = StateMachine.AddState(Desc);

	FAnimTransitionDesc AnyToHit;
	AnyToHit.From = INDEX_NONE;
	AnyToHit.To = OutState;
	AnyToHit.Priority = AnyPriority;
	AnyToHit.CrossfadeDuration = 0.08f;
	AnyToHit.Rule = [this](const FAnimTransitionContext&) { return bHitRequested; };
	StateMachine.AddTransition(AnyToHit);

	FAnimNode_MixamoSequencePlayer* PlayerPtr = &Player;
	FAnimTransitionDesc HitToRun;
	HitToRun.From = OutState;
	HitToRun.To = RunState;
	HitToRun.CrossfadeDuration = HitBlendOutTime;
	HitToRun.Rule = [this, PlayerPtr](const FAnimTransitionContext&) { return bWantsToRun && PlayerPtr->GetTimeRemaining() <= HitBlendOutTime; };
	StateMachine.AddTransition(HitToRun);

	FAnimTransitionDesc HitToIdle;
	HitToIdle.From = OutState;
	HitToIdle.To = IdleState;
	HitToIdle.CrossfadeDuration = HitBlendOutTime;
	HitToIdle.Rule = [this, PlayerPtr](const FAnimTransitionContext&) { return !bWantsToRun && PlayerPtr->GetTimeRemaining() <= HitBlendOutTime; };
	StateMachine.AddTransition(HitToIdle);
}

bool USonicAnimInstance::IsInHit() const
{
	const int32 State = StateMachine.GetCurrentStateIndex();
	return bHitRequested || State == HitStateA || State == HitStateB;
}

bool USonicAnimInstance::IsRunning() const
{
	return !bHitRequested && StateMachine.GetCurrentStateIndex() == RunState;
}
