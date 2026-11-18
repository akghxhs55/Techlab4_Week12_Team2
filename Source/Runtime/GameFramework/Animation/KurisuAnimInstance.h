#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "GameFramework/Animation/MixamoAnimNodes.h"
#include "KurisuAnimInstance.generated.h"

// Kurisu(믹사모) 캐릭터용 AnimInstance. 지상 이동은 전부 애니메이션 루트 모션으로 한다.
// Locomotion: Idle → Walk → Slow Run → Run 1D Blend Space. 가로축은 캐릭터가 원하는 지면 속도(cm/s)이고,
//             샘플 위치는 각 클립이 실제로 움직이는 속도라 "원하는 속도 = 루트 모션 속도"가 된다.
// 액션: Jump(클립의 도약 프레임에 물리 점프) / Roll / Slide(루트 모션).
UCLASS()
class UKurisuAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Idle/Idle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString WalkAnimationPath = "FBX/Walking/Walking.anim.bin";
	UPROPERTY(EditAnywhere)
	FString SlowRunAnimationPath = "FBX/Slow_Run/Slow_Run.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RunAnimationPath = "FBX/Running/Running.anim.bin";
	UPROPERTY(EditAnywhere)
	FString JumpAnimationPath = "FBX/Jump/Jump.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RollAnimationPath = "FBX/Stand_To_Roll/Stand_To_Roll.anim.bin";
	UPROPERTY(EditAnywhere)
	FString SlideAnimationPath = "FBX/Running_Slide/Running_Slide.anim.bin";

	// Jump.anim.bin 타이밍 (초). 0~0.55 웅크림, 0.58 발이 떨어짐, 0.83 최고점, 1.07 착지, 이후 일어서기
	UPROPERTY(EditAnywhere)
	float JumpStartTime = 0.3f;     // 누르면 이 시각부터 재생 (웅크림 앞부분을 건너뛰어 입력 지연을 줄인다)
	UPROPERTY(EditAnywhere)
	float JumpTakeoffTime = 0.58f;  // 이 시각에 캐릭터가 실제로 뜬다
	UPROPERTY(EditAnywhere)
	float JumpLandTime = 1.07f;     // 체공 시간 = Land - Takeoff (점프 속도를 여기에 맞춘다)
	UPROPERTY(EditAnywhere)
	float JumpExitTime = 1.2f;      // 착지 후 이 시각부터 Locomotion으로 섞어 나간다

	// 루프가 아닌 Roll / Slide 클립을 끝나기 이만큼 전부터 Locomotion으로 섞는다
	UPROPERTY(EditAnywhere)
	float ActionBlendOutTime = 0.25f;

	// Locomotion 상태일 때만 받아들인다. 받아들였으면 true
	bool RequestJump();
	bool RequestRoll();
	bool RequestSlide();

	// Jump / Roll / Slide 중인가
	bool IsInAction() const;
	// 점프를 눌렀지만 아직 클립이 도약 프레임에 닿지 않았다
	bool IsInJumpWindup() const;
	// 도약 프레임을 지났으면 한 번만 true. 캐릭터가 이때 물리 점프를 한다
	bool ConsumeJumpTakeoff();
	float GetJumpAirTime() const { return JumpLandTime - JumpTakeoffTime; }

	// 클립 루트 모션에서 잰 실제 이동 속도 (cm/s). 초기화 전이면 측정해 둔 기본값
	float GetWalkSpeed() const { return WalkSpeed; }
	float GetSlowRunSpeed() const { return SlowRunSpeed; }
	float GetRunSpeed() const { return RunSpeed; }

private:
	bool bIsInAir = false;
	bool bJumpRequested = false;
	bool bJumpTakeoffDone = false;
	bool bJumpTakeoffPending = false;
	bool bRollRequested = false;
	bool bSlideRequested = false;

	float WalkSpeed = 146.0f;
	float SlowRunSpeed = 273.0f;
	float RunSpeed = 328.0f;

	int32 LocomotionState = INDEX_NONE;
	int32 JumpState = INDEX_NONE;
	bool bGraphBuilt = false;   // InitializeAnimation이 다시 불려도 상태를 중복 등록하지 않게

	FAnimNode_StateMachine             StateMachine;
	FAnimNode_MixamoBlendSpacePlayer   LocomotionPlayer;
	FAnimNode_MixamoSequencePlayer     JumpPlayer;
	FAnimNode_MixamoSequencePlayer     RollPlayer;
	FAnimNode_MixamoSequencePlayer     SlidePlayer;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
