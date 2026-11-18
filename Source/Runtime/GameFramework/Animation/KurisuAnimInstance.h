#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "GameFramework/Animation/MixamoAnimNodes.h"
#include <algorithm>
#include <cmath>
#include "KurisuAnimInstance.generated.h"

// Kurisu(믹사모) 캐릭터용 AnimInstance. 지상 이동은 전부 애니메이션 루트 모션으로 한다.
// Locomotion: Idle → Walk → Slow Run → Run 1D Blend Space. 가로축은 캐릭터가 원하는 지면 속도(cm/s)이고,
//             샘플 위치는 각 클립이 실제로 움직이는 속도라 "원하는 속도 = 루트 모션 속도"가 된다.
// 액션: Jump(클립의 도약 프레임에 물리 점프) / Roll / Slide(루트 모션) / Jab·Punch(제자리, 서로 콤보로 이어짐).
UCLASS()
class UKurisuAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Kurisu_Idle/Kurisu_Idle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString WalkAnimationPath = "FBX/Kurisu_Walking/Kurisu_Walking.anim.bin";
	UPROPERTY(EditAnywhere)
	FString SlowRunAnimationPath = "FBX/Kurisu_SlowRun/Kurisu_SlowRun.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RunAnimationPath = "FBX/Kurisu_Running/Kurisu_Running.anim.bin";
	UPROPERTY(EditAnywhere)
	FString JumpAnimationPath = "FBX/Kurisu_Jump/Kurisu_Jump.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RollAnimationPath = "FBX/Kurisu_StandToRoll/Kurisu_StandToRoll.anim.bin";
	UPROPERTY(EditAnywhere)
	FString SlideAnimationPath = "FBX/Kurisu_RunningSlide/Kurisu_RunningSlide.anim.bin";
	UPROPERTY(EditAnywhere)
	FString JabAnimationPath = "FBX/Kurisu_Jab/Kurisu_Jab.anim.bin";
	UPROPERTY(EditAnywhere)
	FString PunchAnimationPath = "FBX/Kurisu_Punching/Kurisu_Punching.anim.bin";

	// Jab.anim.bin 타이밍 (초). 0~0.33 자세만 잡음, 0.37~0.55 왼손 뻗기, 0.6~0.75 회수
	UPROPERTY(EditAnywhere)
	float JabStartTime = 0.3f;      // 앞의 정지 구간을 건너뛰어 누르자마자 손이 나가게
	UPROPERTY(EditAnywhere)
	float JabComboTime = 0.62f;     // 이 시각부터 다음 공격(펀치)으로 끊고 넘어갈 수 있다
	// Punching.anim.bin 타이밍 (초). 0.2~0.37 오른손 뻗기, 0.47~0.73 회수
	UPROPERTY(EditAnywhere)
	float PunchStartTime = 0.1f;
	UPROPERTY(EditAnywhere)
	float PunchComboTime = 0.5f;
	// 공격 판정(AttackWindow) 구간 (초). 클립에 AttackWindow가 없을 때만 이 구간으로 붙인다
	UPROPERTY(EditAnywhere)
	float JabHitStartTime = 0.37f;
	UPROPERTY(EditAnywhere)
	float JabHitEndTime = 0.55f;
	UPROPERTY(EditAnywhere)
	float PunchHitStartTime = 0.2f;
	UPROPERTY(EditAnywhere)
	float PunchHitEndTime = 0.37f;

	// Jump.anim.bin 타이밍 (초). 0~0.55 웅크림, 0.58 발이 떨어짐, 0.83 최고점, 1.07 착지, 이후 일어서기
	UPROPERTY(EditAnywhere)
	float JumpStartTime = 0.3f;     // 누르면 이 시각부터 재생 (웅크림 앞부분을 건너뛰어 입력 지연을 줄인다)
	UPROPERTY(EditAnywhere)
	float JumpTakeoffTime = 0.58f;  // 이 시각에 캐릭터가 실제로 뜬다
	UPROPERTY(EditAnywhere)
	float JumpLandTime = 1.07f;     // 체공 시간 = Land - Takeoff (점프 속도를 여기에 맞춘다)
	UPROPERTY(EditAnywhere)
	float JumpExitTime = 1.2f;      // 착지 후 이 시각부터 Locomotion으로 섞어 나간다
	// 클립 그대로의 높이 대비 점프 높이 배율. 높이 = g·t²/8 이므로 체공 시간은 √배가 되고,
	// 공중 구간(Takeoff~Land)만 그만큼 느리게 재생해 착지 프레임을 실제 착지에 맞춘다
	UPROPERTY(EditAnywhere)
	float JumpHeightScale = 3.0f;

	// Run 클립보다 빠르게 달릴 때의 최대 재생 배율. 최대 속도 = Run 루트 모션 속도 × 이 값
	// (Run 속도를 넘는 구간은 Blend Space를 Run에 두고 재생 속도만 올린다. 루트 모션도 같이 빨라진다)
	UPROPERTY(EditAnywhere)
	float MaxRunPlayRate = 1.4f;

	// Stand_To_Roll.anim.bin 타이밍 (초). 0~0.47 서서 뒤로 반 발 물러나는 준비, 0.5~1.33 앞구르기,
	// 1.33~1.55 착지, 1.55~2.37 거의 제자리에서 천천히 일어남.
	// 동작은 그대로 두고, 늘어지는 준비·일어서기 구간만 빨리 재생해 앞뒤 연결의 지연을 줄인다
	UPROPERTY(EditAnywhere)
	float RollWindupEndTime = 0.47f;
	UPROPERTY(EditAnywhere)
	float RollWindupPlayRate = 2.0f;
	UPROPERTY(EditAnywhere)
	float RollRecoverStartTime = 1.55f;
	UPROPERTY(EditAnywhere)
	float RollRecoverPlayRate = 2.0f;   // 이동 입력이 있을 때만. 가만히 있으면 원래 속도로 일어선다

	// 루프가 아닌 Roll / Slide 클립을 끝나기 이만큼 전부터 Locomotion으로 섞는다
	UPROPERTY(EditAnywhere)
	float ActionBlendOutTime = 0.25f;

	// Locomotion 상태일 때만 받아들인다. 받아들였으면 true
	bool RequestJump();
	bool RequestRoll();
	bool RequestSlide();
	// 공격은 Locomotion에서, 또는 다른 공격 도중(콤보)에 받아들인다. 콤보 요청은 ComboTime까지 기다렸다가 넘어간다
	bool RequestJab();
	bool RequestPunch();

	// Jump / Roll / Slide / 공격 중인가
	bool IsInAction() const;
	// 점프를 눌렀지만 아직 클립이 도약 프레임에 닿지 않았다
	bool IsInJumpWindup() const;
	// 도약 프레임을 지났으면 한 번만 true. 캐릭터가 이때 물리 점프를 한다
	bool ConsumeJumpTakeoff();
	// 실제 체공 시간 (초). 클립의 공중 구간 길이 × √JumpHeightScale
	float GetJumpAirTime() const { return (JumpLandTime - JumpTakeoffTime) * std::sqrt(std::max(JumpHeightScale, 0.01f)); }

	// 클립 루트 모션에서 잰 실제 이동 속도 (cm/s). 초기화 전이면 측정해 둔 기본값
	float GetWalkSpeed() const { return WalkSpeed; }
	float GetSlowRunSpeed() const { return SlowRunSpeed; }
	float GetRunSpeed() const { return RunSpeed; }
	float GetMaxRunSpeed() const { return RunSpeed * std::max(MaxRunPlayRate, 1.0f); }

private:
	bool bIsInAir = false;
	bool bHasMoveInput = false;
	bool bJumpRequested = false;
	bool bJumpTakeoffDone = false;
	bool bJumpTakeoffPending = false;
	bool bRollRequested = false;
	bool bSlideRequested = false;
	bool bJabRequested = false;
	bool bPunchRequested = false;

	float WalkSpeed = 146.0f;
	float SlowRunSpeed = 273.0f;
	float RunSpeed = 328.0f;

	int32 LocomotionState = INDEX_NONE;
	int32 JumpState = INDEX_NONE;
	int32 JabState = INDEX_NONE;
	int32 PunchState = INDEX_NONE;
	bool bGraphBuilt = false;   // InitializeAnimation이 다시 불려도 상태를 중복 등록하지 않게

	FAnimNode_StateMachine             StateMachine;
	FAnimNode_MixamoBlendSpacePlayer   LocomotionPlayer;
	FAnimNode_MixamoSequencePlayer     JumpPlayer;
	FAnimNode_MixamoSequencePlayer     RollPlayer;
	FAnimNode_MixamoSequencePlayer     SlidePlayer;
	FAnimNode_MixamoSequencePlayer     JabPlayer;
	FAnimNode_MixamoSequencePlayer     PunchPlayer;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
