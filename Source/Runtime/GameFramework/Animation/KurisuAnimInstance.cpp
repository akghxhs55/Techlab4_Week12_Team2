#include "EnginePCH.h"
#include "GameFramework/Animation/KurisuAnimInstance.h"
#include "GameFramework/Actor/KurisuCharacter.h"
#include "Animation/BlendSpace1D.h"
#include "Asset/AssetManager.h"
#include <algorithm>

namespace
{
	UAnimSequence* LoadSequence(const FString& Path)
	{
		return Path.empty() ? nullptr : UAssetManager::Get().LoadAnimSequence(Path);
	}
}

void UKurisuAnimInstance::NativeInitializeAnimation()
{
	// UAnimInstance::InitializeAnimation이 매번 RootNode를 비우므로 그래프는 한 번만 만들고 RootNode만 다시 건다
	RootNode = &StateMachine;
	if (bGraphBuilt)
	{
		return;
	}
	bGraphBuilt = true;

	// ---- Locomotion Blend Space ----
	// 샘플 위치 = 클립이 실제로 가는 속도. 그래야 Blend Space 입력(원하는 속도)과 루트 모션 속도가 같아진다
	UAnimSequence* Idle = LoadSequence(IdleAnimationPath);
	UAnimSequence* Walk = LoadSequence(WalkAnimationPath);
	UAnimSequence* SlowRun = LoadSequence(SlowRunAnimationPath);
	UAnimSequence* Run = LoadSequence(RunAnimationPath);
	if (Walk)    WalkSpeed = MixamoRootMotion::GetTravelSpeed(Walk);
	if (SlowRun) SlowRunSpeed = std::max(MixamoRootMotion::GetTravelSpeed(SlowRun), WalkSpeed + 1.0f);
	if (Run)     RunSpeed = std::max(MixamoRootMotion::GetTravelSpeed(Run), SlowRunSpeed + 1.0f);

	UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>();
	if (Idle)    BlendSpace->AddSample(Idle, FVector(0.0f, 0.0f, 0.0f));
	if (Walk)    BlendSpace->AddSample(Walk, FVector(WalkSpeed, 0.0f, 0.0f));
	if (SlowRun) BlendSpace->AddSample(SlowRun, FVector(SlowRunSpeed, 0.0f, 0.0f));
	if (Run)     BlendSpace->AddSample(Run, FVector(RunSpeed, 0.0f, 0.0f));
	LocomotionPlayer.BlendSpace = BlendSpace;
	LocomotionPlayer.X = 0.0f;
	KRS_LOG(Info, "[Kurisu] root motion speeds (cm/s): Walk {} / SlowRun {} / Run {}", WalkSpeed, SlowRunSpeed, RunSpeed);

	// ---- 액션 클립 ----
	// 점프 클립은 제자리라 수평 루트 모션이 없다. 뜨는 높이는 물리 점프가 내고, 클립이 올린 높이는 포즈에서 뺀다
	JumpPlayer.Sequence = LoadSequence(JumpAnimationPath);
	JumpPlayer.bLooping = false;
	JumpPlayer.bExtractRootMotion = false;
	JumpPlayer.StartTime = JumpStartTime;
	JumpPlayer.AirborneStartTime = JumpTakeoffTime;
	JumpPlayer.AirborneEndTime = JumpLandTime;

	RollPlayer.Sequence = LoadSequence(RollAnimationPath);
	RollPlayer.bLooping = false;

	SlidePlayer.Sequence = LoadSequence(SlideAnimationPath);
	SlidePlayer.bLooping = false;

	// ---- 상태 ----
	LocomotionState = StateMachine.AddState("Locomotion", &LocomotionPlayer);

	FAnimStateDesc JumpDesc;
	JumpDesc.Name = "Jump";
	JumpDesc.Pose.SetLinkNode(&JumpPlayer);
	JumpDesc.OnEnter = [this](UAnimInstance*)
	{
		bJumpRequested = false;
		bJumpTakeoffDone = false;
		bJumpTakeoffPending = false;
	};
	JumpState = StateMachine.AddState(JumpDesc);

	FAnimStateDesc RollDesc;
	RollDesc.Name = "Roll";
	RollDesc.Pose.SetLinkNode(&RollPlayer);
	RollDesc.OnEnter = [this](UAnimInstance*) { bRollRequested = false; };
	const int32 Roll = StateMachine.AddState(RollDesc);

	FAnimStateDesc SlideDesc;
	SlideDesc.Name = "Slide";
	SlideDesc.Pose.SetLinkNode(&SlidePlayer);
	SlideDesc.OnEnter = [this](UAnimInstance*) { bSlideRequested = false; };
	const int32 Slide = StateMachine.AddState(SlideDesc);

	StateMachine.SetInitialState(LocomotionState);

	// ---- 전환 ----
	FAnimTransitionDesc LocoToJump;
	LocoToJump.From = LocomotionState;
	LocoToJump.To = JumpState;
	LocoToJump.CrossfadeDuration = 0.15f;
	LocoToJump.Rule = [this](const FAnimTransitionContext&) { return bJumpRequested; };
	StateMachine.AddTransition(LocoToJump);

	// 착지하고 일어서는 동작이 어느 정도 나온 뒤 나간다. 도약을 못 했으면(막힘 등) 클립 끝에서 나간다
	FAnimTransitionDesc JumpToLoco;
	JumpToLoco.From = JumpState;
	JumpToLoco.To = LocomotionState;
	JumpToLoco.CrossfadeDuration = 0.3f;
	JumpToLoco.Rule = [this](const FAnimTransitionContext&)
	{
		if (bIsInAir) return false;
		return (bJumpTakeoffDone && JumpPlayer.CurrentTime >= JumpExitTime) || JumpPlayer.IsFinished();
	};
	StateMachine.AddTransition(JumpToLoco);

	FAnimTransitionDesc LocoToRoll;
	LocoToRoll.From = LocomotionState;
	LocoToRoll.To = Roll;
	LocoToRoll.Priority = 1;
	LocoToRoll.CrossfadeDuration = 0.1f;
	LocoToRoll.Rule = [this](const FAnimTransitionContext&) { return bRollRequested; };
	StateMachine.AddTransition(LocoToRoll);

	FAnimTransitionDesc RollToLoco;
	RollToLoco.From = Roll;
	RollToLoco.To = LocomotionState;
	RollToLoco.CrossfadeDuration = ActionBlendOutTime;
	RollToLoco.Rule = [this](const FAnimTransitionContext&) { return RollPlayer.GetTimeRemaining() <= ActionBlendOutTime; };
	StateMachine.AddTransition(RollToLoco);

	FAnimTransitionDesc LocoToSlide;
	LocoToSlide.From = LocomotionState;
	LocoToSlide.To = Slide;
	LocoToSlide.Priority = 1;
	LocoToSlide.CrossfadeDuration = 0.15f;
	LocoToSlide.Rule = [this](const FAnimTransitionContext&) { return bSlideRequested; };
	StateMachine.AddTransition(LocoToSlide);

	FAnimTransitionDesc SlideToLoco;
	SlideToLoco.From = Slide;
	SlideToLoco.To = LocomotionState;
	SlideToLoco.CrossfadeDuration = ActionBlendOutTime;
	SlideToLoco.Rule = [this](const FAnimTransitionContext&) { return SlidePlayer.GetTimeRemaining() <= ActionBlendOutTime; };
	StateMachine.AddTransition(SlideToLoco);
}

void UKurisuAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (const AKurisuCharacter* Character = Cast<AKurisuCharacter>(TryGetPawnOwner()))
	{
		// Blend Space 축은 실제 속도가 아니라 "원하는 속도"다. 실제 속도는 이 값으로 고른 클립의 루트 모션이 만든다
		LocomotionPlayer.X = Character->GetDesiredGroundSpeed();
		bIsInAir = Character->IsFalling();
	}

	// 지난 프레임에 클립이 도약 시각을 넘었으면 캐릭터에 알린다
	if (StateMachine.GetCurrentStateIndex() == JumpState && !bJumpTakeoffDone
		&& JumpPlayer.CurrentTime >= JumpTakeoffTime)
	{
		bJumpTakeoffDone = true;
		bJumpTakeoffPending = true;
	}
}

bool UKurisuAnimInstance::RequestJump()
{
	if (IsInAction() || bIsInAir) return false;
	bJumpRequested = true;
	return true;
}

bool UKurisuAnimInstance::RequestRoll()
{
	if (IsInAction() || bIsInAir) return false;
	bRollRequested = true;
	return true;
}

bool UKurisuAnimInstance::RequestSlide()
{
	if (IsInAction() || bIsInAir) return false;
	bSlideRequested = true;
	return true;
}

bool UKurisuAnimInstance::IsInAction() const
{
	// 요청은 다음 Update에서 상태가 바뀌므로, 아직 Locomotion이어도 요청이 걸려 있으면 액션 중으로 본다
	return bJumpRequested || bRollRequested || bSlideRequested
		|| StateMachine.GetCurrentStateIndex() != LocomotionState;
}

bool UKurisuAnimInstance::IsInJumpWindup() const
{
	return bJumpRequested || (StateMachine.GetCurrentStateIndex() == JumpState && !bJumpTakeoffDone);
}

bool UKurisuAnimInstance::ConsumeJumpTakeoff()
{
	const bool bResult = bJumpTakeoffPending;
	bJumpTakeoffPending = false;
	return bResult;
}
