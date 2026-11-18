#include "EnginePCH.h"
#include "GameFramework/Animation/KurisuAnimInstance.h"
#include "GameFramework/Actor/KurisuCharacter.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/Animation/AnimNotifyState_AttackWindow.h"
#include "Asset/AssetManager.h"
#include <algorithm>

namespace
{
	UAnimSequence* LoadSequence(const FString& Path)
	{
		return Path.empty() ? nullptr : UAssetManager::Get().LoadAnimSequence(Path);
	}

	// 클립에 공격 판정 구간이 없으면 붙인다. 시퀀스는 캐시돼 다시 초기화될 수 있으므로 이미 있으면 건드리지 않는다
	void EnsureAttackWindow(UAnimSequence* Sequence, float StartTime, float EndTime)
	{
		if (!Sequence || EndTime <= StartTime)
			return;
		for (const FAnimNotifyEvent& Event : Sequence->GetNotifies())
		{
			if (Cast<UAnimNotifyState_AttackWindow>(Event.NotifyStateClass))
				return;
		}
		Sequence->AddNotify<UAnimNotifyState_AttackWindow>(StartTime, EndTime - StartTime);
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
	LocomotionPlayer.bExtractRootMotion = false;
	KRS_LOG(Info, "[Kurisu] root motion speeds (cm/s): Walk {} / SlowRun {} / Run {}", WalkSpeed, SlowRunSpeed, RunSpeed);

	// ---- 액션 클립 ----
	// 점프 클립은 제자리라 수평 루트 모션이 없다. 뜨는 높이는 물리 점프가 내고, 클립이 올린 높이는 포즈에서 뺀다
	JumpPlayer.Sequence = LoadSequence(JumpAnimationPath);
	JumpPlayer.bLooping = false;
	JumpPlayer.bExtractRootMotion = false;
	JumpPlayer.StartTime = JumpStartTime;
	JumpPlayer.AirborneStartTime = JumpTakeoffTime;
	JumpPlayer.AirborneEndTime = JumpLandTime;

	// 구르기·슬라이드도 루트 모션은 뽑지 않는다. 캐릭터가 클립의 순간 속도(GetDashTravelSpeed)를 읽어 직접 움직인다
	RollPlayer.Sequence = LoadSequence(RollAnimationPath);
	RollPlayer.bLooping = false;
	RollPlayer.bExtractRootMotion = false;
	RollPlayer.StartTime = RollStartTime;

	SlidePlayer.Sequence = LoadSequence(SlideAnimationPath);
	SlidePlayer.bLooping = false;
	SlidePlayer.bExtractRootMotion = false;

	// 공격 클립은 제자리라 루트 모션을 뽑지 않는다 (공격 중에는 MaxWalkSpeed 0이라 그 자리에 선다)
	JabPlayer.Sequence = LoadSequence(JabAnimationPath);
	JabPlayer.bLooping = false;
	JabPlayer.bExtractRootMotion = false;
	JabPlayer.StartTime = JabStartTime;

	PunchPlayer.Sequence = LoadSequence(PunchAnimationPath);
	PunchPlayer.bLooping = false;
	PunchPlayer.bExtractRootMotion = false;
	PunchPlayer.StartTime = PunchStartTime;

	EnsureAttackWindow(JabPlayer.Sequence, JabHitStartTime, JabHitEndTime);
	EnsureAttackWindow(PunchPlayer.Sequence, PunchHitStartTime, PunchHitEndTime);

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
		bJumpLeftGround = false;
	};
	JumpState = StateMachine.AddState(JumpDesc);

	FAnimStateDesc RollDesc;
	RollDesc.Name = "Roll";
	RollDesc.Pose.SetLinkNode(&RollPlayer);
	RollDesc.OnEnter = [this](UAnimInstance*) { bRollRequested = false; };
	RollState = StateMachine.AddState(RollDesc);
	const int32 Roll = RollState;

	FAnimStateDesc SlideDesc;
	SlideDesc.Name = "Slide";
	SlideDesc.Pose.SetLinkNode(&SlidePlayer);
	SlideDesc.OnEnter = [this](UAnimInstance*) { bSlideRequested = false; };
	SlideState = StateMachine.AddState(SlideDesc);
	const int32 Slide = SlideState;

	FAnimStateDesc JabDesc;
	JabDesc.Name = "Jab";
	JabDesc.Pose.SetLinkNode(&JabPlayer);
	JabDesc.OnEnter = [this](UAnimInstance*) { bJabRequested = false; };
	JabState = StateMachine.AddState(JabDesc);

	FAnimStateDesc PunchDesc;
	PunchDesc.Name = "Punch";
	PunchDesc.Pose.SetLinkNode(&PunchPlayer);
	PunchDesc.OnEnter = [this](UAnimInstance*) { bPunchRequested = false; };
	PunchState = StateMachine.AddState(PunchDesc);

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

	// 이동 입력이 있으면 천천히 일어서는 구간을 끊고 바로 달린다
	FAnimTransitionDesc RollMoveCancel;
	RollMoveCancel.From = Roll;
	RollMoveCancel.To = LocomotionState;
	RollMoveCancel.Priority = 1;
	RollMoveCancel.CrossfadeDuration = ActionBlendOutTime;
	RollMoveCancel.Rule = [this](const FAnimTransitionContext&) { return bHasMoveInput && RollPlayer.CurrentTime >= RollMoveCancelTime; };
	StateMachine.AddTransition(RollMoveCancel);

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

	FAnimTransitionDesc SlideMoveCancel;
	SlideMoveCancel.From = Slide;
	SlideMoveCancel.To = LocomotionState;
	SlideMoveCancel.Priority = 1;
	SlideMoveCancel.CrossfadeDuration = ActionBlendOutTime;
	SlideMoveCancel.Rule = [this](const FAnimTransitionContext&) { return bHasMoveInput && SlidePlayer.GetTimeRemaining() <= SlideMoveCancelRemainingTime; };
	StateMachine.AddTransition(SlideMoveCancel);

	// ---- 공격 ----
	// 시작은 짧게 섞어 손이 바로 나가게 한다
	FAnimTransitionDesc LocoToJab;
	LocoToJab.From = LocomotionState;
	LocoToJab.To = JabState;
	LocoToJab.Priority = 1;
	LocoToJab.CrossfadeDuration = 0.08f;
	LocoToJab.Rule = [this](const FAnimTransitionContext&) { return bJabRequested; };
	StateMachine.AddTransition(LocoToJab);

	FAnimTransitionDesc LocoToPunch;
	LocoToPunch.From = LocomotionState;
	LocoToPunch.To = PunchState;
	LocoToPunch.Priority = 1;
	LocoToPunch.CrossfadeDuration = 0.08f;
	LocoToPunch.Rule = [this](const FAnimTransitionContext&) { return bPunchRequested; };
	StateMachine.AddTransition(LocoToPunch);

	// 콤보: 손을 거둬들이기 시작한 뒤에 다음 공격이 걸려 있으면 끊고 넘어간다. 끝나서 나가는 전환보다 먼저 본다
	FAnimTransitionDesc JabToPunch;
	JabToPunch.From = JabState;
	JabToPunch.To = PunchState;
	JabToPunch.CrossfadeDuration = 0.1f;
	JabToPunch.Rule = [this](const FAnimTransitionContext&) { return bPunchRequested && JabPlayer.CurrentTime >= JabComboTime; };
	StateMachine.AddTransition(JabToPunch);

	FAnimTransitionDesc PunchToJab;
	PunchToJab.From = PunchState;
	PunchToJab.To = JabState;
	PunchToJab.CrossfadeDuration = 0.1f;
	PunchToJab.Rule = [this](const FAnimTransitionContext&) { return bJabRequested && PunchPlayer.CurrentTime >= PunchComboTime; };
	StateMachine.AddTransition(PunchToJab);

	FAnimTransitionDesc JabToLoco;
	JabToLoco.From = JabState;
	JabToLoco.To = LocomotionState;
	JabToLoco.Priority = 1;
	JabToLoco.CrossfadeDuration = ActionBlendOutTime;
	JabToLoco.Rule = [this](const FAnimTransitionContext&) { return JabPlayer.GetTimeRemaining() <= ActionBlendOutTime; };
	StateMachine.AddTransition(JabToLoco);

	FAnimTransitionDesc PunchToLoco;
	PunchToLoco.From = PunchState;
	PunchToLoco.To = LocomotionState;
	PunchToLoco.Priority = 1;
	PunchToLoco.CrossfadeDuration = ActionBlendOutTime;
	PunchToLoco.Rule = [this](const FAnimTransitionContext&) { return PunchPlayer.GetTimeRemaining() <= ActionBlendOutTime; };
	StateMachine.AddTransition(PunchToLoco);

	// 후딜 캔슬. 콤보 전환(Priority 0)을 먼저 보므로 다음 공격이 걸려 있으면 콤보가 이긴다
	FAnimTransitionDesc JabMoveCancel;
	JabMoveCancel.From = JabState;
	JabMoveCancel.To = LocomotionState;
	JabMoveCancel.Priority = 1;
	JabMoveCancel.CrossfadeDuration = MoveCancelBlendTime;
	JabMoveCancel.Rule = [this](const FAnimTransitionContext&) { return bHasMoveInput && JabPlayer.CurrentTime >= JabComboTime + AttackMoveCancelDelay; };
	StateMachine.AddTransition(JabMoveCancel);

	FAnimTransitionDesc PunchMoveCancel;
	PunchMoveCancel.From = PunchState;
	PunchMoveCancel.To = LocomotionState;
	PunchMoveCancel.Priority = 1;
	PunchMoveCancel.CrossfadeDuration = MoveCancelBlendTime;
	PunchMoveCancel.Rule = [this](const FAnimTransitionContext&) { return bHasMoveInput && PunchPlayer.CurrentTime >= PunchComboTime + AttackMoveCancelDelay; };
	StateMachine.AddTransition(PunchMoveCancel);

	FAnimTransitionDesc JabToRoll;
	JabToRoll.From = JabState;
	JabToRoll.To = Roll;
	JabToRoll.Priority = -1;
	JabToRoll.CrossfadeDuration = 0.1f;
	JabToRoll.Rule = [this](const FAnimTransitionContext&) { return bRollRequested; };
	StateMachine.AddTransition(JabToRoll);

	FAnimTransitionDesc PunchToRoll;
	PunchToRoll.From = PunchState;
	PunchToRoll.To = Roll;
	PunchToRoll.Priority = -1;
	PunchToRoll.CrossfadeDuration = 0.1f;
	PunchToRoll.Rule = [this](const FAnimTransitionContext&) { return bRollRequested; };
	StateMachine.AddTransition(PunchToRoll);
}

void UKurisuAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (const AKurisuCharacter* Character = Cast<AKurisuCharacter>(TryGetPawnOwner()))
	{
		// Blend Space 축은 실제 속도다. Run 속도를 넘는 만큼은 Run 클립을 빨리 돌려 발을 맞춘다
		const float GroundSpeed = Character->GetGroundSpeed();
		LocomotionPlayer.X = std::min(GroundSpeed, RunSpeed);
		LocomotionPlayer.PlayRate = (GroundSpeed > RunSpeed && RunSpeed > 0.0f) ? GroundSpeed / RunSpeed : 1.0f;
		bIsInAir = Character->IsFalling();
		VerticalSpeed = Character->GetVelocity().Z * WorldToCm;
		bHasMoveInput = Character->HasMovementInput();
	}

	// 구르기: 준비 동작과 (계속 움직이려 할 때의) 일어서기만 빠르게
	const float RollTime = RollPlayer.CurrentTime;
	if (RollTime < RollWindupEndTime)
		RollPlayer.PlayRate = RollWindupPlayRate;
	else if (RollTime >= RollRecoverStartTime && bHasMoveInput)
		RollPlayer.PlayRate = RollRecoverPlayRate;
	else
		RollPlayer.PlayRate = 1.0f;

	// 지난 프레임에 클립이 도약 시각을 넘었으면 캐릭터에 알린다
	const bool bInJumpState = StateMachine.GetCurrentStateIndex() == JumpState;
	if (bInJumpState && !bJumpTakeoffDone && JumpPlayer.CurrentTime >= JumpTakeoffTime)
	{
		bJumpTakeoffDone = true;
		bJumpTakeoffPending = true;
	}

	// 공중 구간: 수직 속도로 클립의 목표 시각을 정하고, 그 시각까지 가도록 재생 속도를 정한다 (Notify를 건너뛰지 않게 시간을 직접 바꾸지 않는다)
	JumpPlayer.PlayRate = 1.0f;
	if (bInJumpState && bJumpTakeoffDone && JumpPlayer.CurrentTime >= JumpTakeoffTime && JumpPlayer.CurrentTime < JumpLandTime)
	{
		if (bIsInAir)
			bJumpLeftGround = true;

		if (bIsInAir && DeltaSeconds > 0.0f)
		{
			float Target = JumpPlayer.CurrentTime;
			if (VerticalSpeed > 0.0f && JumpTakeoffSpeed > 0.0f)
			{
				const float Rise = 1.0f - std::clamp(VerticalSpeed / JumpTakeoffSpeed, 0.0f, 1.0f);
				Target = JumpTakeoffTime + Rise * (JumpApexTime - JumpTakeoffTime);
			}
			else if (JumpLandSpeed > 0.0f)
			{
				// 발판보다 아래로 더 떨어지면 착지 직전 포즈에서 기다린다
				const float Fall = std::clamp(-VerticalSpeed / JumpLandSpeed, 0.0f, 0.95f);
				Target = JumpApexTime + Fall * (JumpLandTime - JumpApexTime);
			}
			JumpPlayer.PlayRate = std::clamp((Target - JumpPlayer.CurrentTime) / DeltaSeconds, 0.0f, 6.0f);
		}
		else if (!bIsInAir && bJumpLeftGround)
		{
			// 낮은 점프·머리 부딪힘 등으로 일찍 내려왔다: 착지 프레임까지 빨리 돌린다
			JumpPlayer.PlayRate = JumpEarlyLandPlayRate;
		}
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
	if (!CanRoll()) return false;
	bRollRequested = true;
	return true;
}

bool UKurisuAnimInstance::RequestSlide()
{
	if (IsInAction() || bIsInAir) return false;
	bSlideRequested = true;
	return true;
}

bool UKurisuAnimInstance::RequestJab()
{
	if (bIsInAir) return false;
	// Locomotion에서 바로, 또는 펀치 도중이면 콤보로 예약
	const bool bFromPunch = StateMachine.GetCurrentStateIndex() == PunchState && !bPunchRequested;
	if (!bFromPunch && IsInAction()) return false;
	bJabRequested = true;
	return true;
}

bool UKurisuAnimInstance::RequestPunch()
{
	if (bIsInAir) return false;
	const bool bFromJab = StateMachine.GetCurrentStateIndex() == JabState && !bJabRequested;
	if (!bFromJab && IsInAction()) return false;
	bPunchRequested = true;
	return true;
}

bool UKurisuAnimInstance::IsInAction() const
{
	// 요청은 다음 Update에서 상태가 바뀌므로, 아직 Locomotion이어도 요청이 걸려 있으면 액션 중으로 본다
	return bJumpRequested || bRollRequested || bSlideRequested || bJabRequested || bPunchRequested
		|| StateMachine.GetCurrentStateIndex() != LocomotionState;
}

bool UKurisuAnimInstance::IsInDashState() const
{
	const int32 State = StateMachine.GetCurrentStateIndex();
	return State != INDEX_NONE && (State == RollState || State == SlideState);
}

float UKurisuAnimInstance::GetDashTravelSpeed() const
{
	const int32 State = StateMachine.GetCurrentStateIndex();
	const FAnimNode_MixamoSequencePlayer* Player = State == RollState ? &RollPlayer : State == SlideState ? &SlidePlayer : nullptr;
	if (!Player || !Player->Sequence)
		return 0.0f;

	// 지금 시각부터 짧은 구간 동안 클립이 간 거리 → 클립 시간 기준 속도 → 재생 속도를 곱해 실제 시간 기준으로
	constexpr float Window = 1.0f / 30.0f;
	const float Distance = MixamoRootMotion::GetTravelDelta(Player->Sequence, Player->CurrentTime, Window, false);
	return std::max(Distance / Window * Player->PlayRate, 0.0f);
}

bool UKurisuAnimInstance::IsInAttackRecovery() const
{
	if (bJumpRequested || bRollRequested || bSlideRequested || bJabRequested || bPunchRequested)
		return false;
	const int32 State = StateMachine.GetCurrentStateIndex();
	return (State == JabState && JabPlayer.CurrentTime >= JabComboTime)
		|| (State == PunchState && PunchPlayer.CurrentTime >= PunchComboTime);
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
