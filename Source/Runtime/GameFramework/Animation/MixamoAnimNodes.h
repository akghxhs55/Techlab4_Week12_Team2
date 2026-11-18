#pragma once

#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/BonePose.h"

// Kurisu 리그(믹사모 다운로드)의 루트 모션.
// 이 리그는 "root" 본이 없고 첫 트랙인 Armature(0번 본)가 Hips 역할을 한다: 이동·높이·몸 회전이 전부 이 트랙에 있다.
// 엔진 기본 루트 모션은 root 트랙의 위치·회전을 통째로 고정(Root Lock)하므로 여기서 쓰면 구르기 회전, 걸음 흔들림이 사라진다.
// 그래서 클립의 "진행 방향"(첫 프레임 → 마지막 프레임 수평 방향) 성분만 루트 모션으로 넘기고 포즈에서는 그 성분만 뺀다.
// 좌우 흔들림, 위아래 움직임, 회전은 포즈에 그대로 남는다. 첫 프레임과 마지막 프레임이 같은 클립(Idle, 제자리 Jump)은 손대지 않는다.
namespace MixamoRootMotion
{
	// 루트 트랙의 진행 방향 단위 벡터 (컴포넌트 공간, 수평). 이동이 없는 클립이면 0 벡터
	FVector GetTravelDirection(const UAnimSequence* Sequence);
	// 클립 한 바퀴의 평균 속도 (컴포넌트 공간 단위/초. Kurisu는 cm/s)
	float GetTravelSpeed(const UAnimSequence* Sequence);
	// [StartTime, StartTime + DeltaTime] 동안 진행 방향으로 간 거리. bLooping이면 클립 끝을 넘는 구간을 이어 붙인다
	float GetTravelDelta(const UAnimSequence* Sequence, float StartTime, float DeltaTime, bool bLooping);
	// 포즈의 루트 본에서 Time까지의 진행 거리를 뺀다 (제자리로 만든다)
	void RemoveTravelFromPose(const UAnimSequence* Sequence, float Time, FCompactPose& Pose, const TArray<FBoneInfo>& RefSkeleton);
	// 루트 트랙의 위치 (컴포넌트 공간)
	FVector SampleRootLocation(const UAnimSequence* Sequence, float Time);
}

// 시퀀스 플레이어 + 진행 방향 루트 모션.
struct FAnimNode_MixamoSequencePlayer : public FAnimNode_SequencePlayer
{
	bool bExtractRootMotion = true;
	// 상태에 들어올 때 이 시각부터 재생한다 (예: 점프의 준비 동작 일부를 건너뛴다)
	float StartTime = 0.0f;
	// 이 구간 동안 루트가 AirborneStartTime 높이보다 올라간 만큼 포즈에서 뺀다. 그 높이는 캡슐(물리 점프)이 대신 낸다
	float AirborneStartTime = -1.0f;
	float AirborneEndTime = -1.0f;
	// 뒤로 가는 루트 모션은 버린다 (준비 동작에서 뒤로 무르는 걸음 때문에 달리다 뒤로 끌려가지 않게)
	bool bForwardRootMotionOnly = false;

	void Initialize(const FAnimationInitializeContext& Context) override;
	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;

	float GetTimeRemaining() const;
};

// 1D Blend Space 플레이어 + 진행 방향 루트 모션. 샘플마다 루트 모션을 뽑아 가중치로 섞는다.
// 이동 클립(Walk/Run 등)끼리만 Phase로 동기화한다. 제자리 클립(Idle)은 길이가 전혀 달라서(16초 vs 1초)
// 같이 묶으면 섞이는 동안 걷기가 몇 배 느려지므로, 자기 시간(FreeTime)으로 따로 돈다 (UE Sync Group과 같은 생각).
struct FAnimNode_MixamoBlendSpacePlayer : public FAnimNode_BlendSpacePlayer
{
	// false면 포즈만 제자리로 만들고 루트 모션은 넘기지 않는다 (이동을 코드가 할 때)
	bool bExtractRootMotion = true;

	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;

private:
	float GetSampleTime(const UAnimSequence* Sequence) const;

	float FreeTime = 0.0f;          // 제자리 클립용 시간
	bool bWasMoving = false;        // 지난 프레임에 이동 클립이 섞여 있었나 (출발할 때 Phase를 0부터)
};
