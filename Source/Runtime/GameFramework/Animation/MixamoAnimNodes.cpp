#include "EnginePCH.h"
#include "GameFramework/Animation/MixamoAnimNodes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationRuntime.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
	// 이동으로 보지 않는 최소 거리 (cm). Idle·제자리 점프의 첫/끝 프레임 오차를 무시한다
	constexpr float MinTravelDistance = 5.0f;

	struct FTravelInfo
	{
		const FBoneAnimationTrack* RootTrack = nullptr;
		FVector Direction = FVector::ZeroVector;   // 수평 단위 벡터, 이동 없으면 0
		float Distance = 0.0f;                     // 한 바퀴 진행 거리
		float Length = 0.0f;
		double FrameRate = 0.0;
	};

	// 시퀀스마다 한 번만 계산한다. 시퀀스는 AssetManager가 들고 있어 주소가 바뀌지 않는다
	const FTravelInfo& GetInfo(const UAnimSequence* Sequence)
	{
		static std::unordered_map<const UAnimSequence*, FTravelInfo> Cache;
		static const FTravelInfo Empty;
		if (!Sequence)
		{
			return Empty;
		}
		if (auto Found = Cache.find(Sequence); Found != Cache.end())
		{
			return Found->second;
		}

		FTravelInfo Info;
		const UAnimDataModel* Model = Sequence->GetDataModel();
		if (Model && !Model->GetBoneAnimationTracks().IsEmpty())
		{
			// 엔진 루트 모션과 같은 규칙: "root" 트랙이 없으면 첫 트랙
			const TArray<FBoneAnimationTrack>& Tracks = Model->GetBoneAnimationTracks();
			Info.RootTrack = &Tracks[0];
			for (const FBoneAnimationTrack& Track : Tracks)
			{
				if (Track.Name == "root") { Info.RootTrack = &Track; break; }
			}
			Info.Length = Model->GetPlayLength();
			Info.FrameRate = Model->GetFrameRate().AsDecimal();

			const TArray<FVector>& Keys = Info.RootTrack->InternalTrack.PosKeys;
			if (Keys.Num() >= 2)
			{
				const FVector First = Keys[0];
				const FVector Last = Keys[Keys.Num() - 1];
				FVector Travel(Last.X - First.X, Last.Y - First.Y, 0.0f);
				const float Distance = Travel.Size();
				if (Distance >= MinTravelDistance)
				{
					Info.Direction = Travel * (1.0f / Distance);
					Info.Distance = Distance;
				}
			}
		}
		return Cache.emplace(Sequence, Info).first->second;
	}

	FVector SampleKeys(const FTravelInfo& Info, float Time)
	{
		const TArray<FVector>& Keys = Info.RootTrack->InternalTrack.PosKeys;
		if (Keys.IsEmpty())
		{
			return FVector::ZeroVector;
		}
		const double Frame = std::clamp(static_cast<double>(std::clamp(Time, 0.0f, Info.Length)) * Info.FrameRate,
			0.0, static_cast<double>(Keys.Num() - 1));
		const int32 A = static_cast<int32>(Frame);
		const int32 B = std::min(A + 1, Keys.Num() - 1);
		return FMath::Lerp(Keys[A], Keys[B], static_cast<float>(Frame - A));
	}

	// 첫 프레임부터 Time까지 진행 방향으로 간 거리
	float GetProgress(const FTravelInfo& Info, float Time)
	{
		const FVector Offset = SampleKeys(Info, Time) - SampleKeys(Info, 0.0f);
		return Offset.X * Info.Direction.X + Offset.Y * Info.Direction.Y;
	}

	int32 FindRootBoneIndex(const FTravelInfo& Info, const TArray<FBoneInfo>& RefSkeleton)
	{
		for (int32 Index = 0; Index < RefSkeleton.Num(); ++Index)
		{
			if (RefSkeleton[Index].Name == Info.RootTrack->Name)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// 루트 모션은 루트 본(Armature, 부모 없음)의 부모 공간 = 컴포넌트 공간 delta로 넘긴다.
	// SkeletalMeshComponent가 메시 스케일(0.01)·회전을 거쳐 월드 이동으로 바꾼다.
	void AddTravelRootMotion(UAnimInstance* AnimInstance, const FVector& Direction, float Distance)
	{
		if (!AnimInstance || !AnimInstance->ShouldExtractRootMotion() || std::abs(Distance) <= 0.0f)
		{
			return;
		}
		FTransform RootMotion;
		RootMotion.Location = Direction * Distance;
		AnimInstance->AddRootMotion(RootMotion);
	}
}

namespace MixamoRootMotion
{
	FVector GetTravelDirection(const UAnimSequence* Sequence)
	{
		return GetInfo(Sequence).Direction;
	}

	float GetTravelSpeed(const UAnimSequence* Sequence)
	{
		const FTravelInfo& Info = GetInfo(Sequence);
		return Info.Length > 0.0f ? Info.Distance / Info.Length : 0.0f;
	}

	float GetTravelDelta(const UAnimSequence* Sequence, float StartTime, float DeltaTime, bool bLooping)
	{
		const FTravelInfo& Info = GetInfo(Sequence);
		if (!Info.RootTrack || Info.Distance <= 0.0f || DeltaTime <= 0.0f || Info.Length <= 0.0f)
		{
			return 0.0f;
		}

		const float EndTime = StartTime + DeltaTime;
		if (!bLooping || EndTime <= Info.Length)
		{
			return GetProgress(Info, std::min(EndTime, Info.Length)) - GetProgress(Info, StartTime);
		}

		// 끝을 넘으면 [StartTime, 끝] + 한 바퀴 * n + [처음, 나머지]
		const float Wrapped = std::fmod(EndTime, Info.Length);
		const float FullLoops = std::floor(EndTime / Info.Length) - 1.0f;
		return (Info.Distance - GetProgress(Info, StartTime)) + FullLoops * Info.Distance + GetProgress(Info, Wrapped);
	}

	void RemoveTravelFromPose(const UAnimSequence* Sequence, float Time, FCompactPose& Pose, const TArray<FBoneInfo>& RefSkeleton)
	{
		const FTravelInfo& Info = GetInfo(Sequence);
		if (!Info.RootTrack || Info.Distance <= 0.0f)
		{
			return;
		}
		const int32 RootIndex = FindRootBoneIndex(Info, RefSkeleton);
		if (RootIndex == INDEX_NONE || RootIndex >= Pose.GetNumBones())
		{
			return;
		}
		Pose[RootIndex].Location = Pose[RootIndex].Location - Info.Direction * GetProgress(Info, Time);
	}

	FVector SampleRootLocation(const UAnimSequence* Sequence, float Time)
	{
		const FTravelInfo& Info = GetInfo(Sequence);
		return Info.RootTrack ? SampleKeys(Info, Time) : FVector::ZeroVector;
	}
}

// ---------------- FAnimNode_MixamoSequencePlayer ----------------

void FAnimNode_MixamoSequencePlayer::Initialize(const FAnimationInitializeContext& Context)
{
	FAnimNode_SequencePlayer::Initialize(Context);
	const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
	const float Length = Model ? Model->GetPlayLength() : 0.0f;
	CurrentTime = PrevTime = FMath::Clamp(StartTime, 0.0f, Length);
}

void FAnimNode_MixamoSequencePlayer::Update(const FAnimationUpdateContext& Context)
{
	// 시퀀스의 Enable Root Motion은 꺼 둔 채로 쓴다. 기본 플레이어는 시간·Notify만 처리한다
	FAnimNode_SequencePlayer::Update(Context);

	if (!bExtractRootMotion || !Sequence)
	{
		return;
	}
	// 상태가 섞이는 동안에는 그 상태의 가중치만큼만 움직인다 (안 그러면 전환 중에 두 상태의 이동이 더해져 튄다)
	float Delta = MixamoRootMotion::GetTravelDelta(Sequence, PrevTime, CurrentTime - PrevTime, false)
		* Context.GetFinalBlendWeight();
	if (bForwardRootMotionOnly)
	{
		Delta = std::max(Delta, 0.0f);
	}
	AddTravelRootMotion(Context.GetAnimInstance<UAnimInstance>(), MixamoRootMotion::GetTravelDirection(Sequence), Delta);
}

void FAnimNode_MixamoSequencePlayer::Evaluate(FPoseContext& Output)
{
	FAnimNode_SequencePlayer::Evaluate(Output);
	if (!Sequence)
	{
		return;
	}

	const TArray<FBoneInfo>& RefSkeleton = Output.GetRefSkeleton();
	MixamoRootMotion::RemoveTravelFromPose(Sequence, CurrentTime, Output.Pose, RefSkeleton);

	// 공중 구간: 클립이 올린 높이는 캡슐이 내므로 포즈에서는 뺀다 (0에서 시작해 0으로 끝나 끊김이 없다)
	if (AirborneStartTime >= 0.0f && CurrentTime > AirborneStartTime && CurrentTime < AirborneEndTime)
	{
		const float Rise = MixamoRootMotion::SampleRootLocation(Sequence, CurrentTime).Z
			- MixamoRootMotion::SampleRootLocation(Sequence, AirborneStartTime).Z;
		const FTravelInfo& Info = GetInfo(Sequence);
		const int32 RootIndex = Info.RootTrack ? FindRootBoneIndex(Info, RefSkeleton) : INDEX_NONE;
		if (Rise > 0.0f && RootIndex != INDEX_NONE && RootIndex < Output.Pose.GetNumBones())
		{
			// 루트 본(Armature)은 부모가 없어 컴포넌트 공간 Z가 곧 위쪽이다
			Output.Pose[RootIndex].Location.Z -= Rise;
		}
	}
}

float FAnimNode_MixamoSequencePlayer::GetTimeRemaining() const
{
	const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
	return Model ? Model->GetPlayLength() - CurrentTime : 0.0f;
}

// ---------------- FAnimNode_MixamoBlendSpacePlayer ----------------

float FAnimNode_MixamoBlendSpacePlayer::GetSampleTime(const UAnimSequence* Sequence) const
{
	const float Length = Sequence->GetDataModel()->GetPlayLength();
	if (Length <= 0.0f)
	{
		return 0.0f;
	}
	// 이동 클립은 공유 Phase, 제자리 클립은 자기 시간
	return MixamoRootMotion::GetTravelSpeed(Sequence) > 0.0f ? Phase * Length : std::fmod(FreeTime, Length);
}

void FAnimNode_MixamoBlendSpacePlayer::Update(const FAnimationUpdateContext& Context)
{
	// 기본 플레이어의 Update(모든 샘플을 한 Phase로 묶음)는 쓰지 않고 시간·Notify·루트 모션을 직접 처리한다
	FBlendSample SampleA;
	FBlendSample SampleB;
	float Alpha = 0.0f;
	if (!BlendSpace || !BlendSpace->GetSamples(X, SampleA, SampleB, Alpha)
		|| !SampleA.Animation || !SampleB.Animation
		|| !SampleA.Animation->GetDataModel() || !SampleB.Animation->GetDataModel())
	{
		return;
	}

	const bool bSameSample = SampleA.Animation == SampleB.Animation;
	const float WeightA = bSameSample ? 1.0f : 1.0f - Alpha;
	const float WeightB = bSameSample ? 0.0f : Alpha;
	const float LengthA = SampleA.Animation->GetDataModel()->GetPlayLength();
	const float LengthB = SampleB.Animation->GetDataModel()->GetPlayLength();
	const bool bMovingA = MixamoRootMotion::GetTravelSpeed(SampleA.Animation) > 0.0f;
	const bool bMovingB = MixamoRootMotion::GetTravelSpeed(SampleB.Animation) > 0.0f;

	// 동기화 길이: 지금 섞이는 이동 클립들의 길이를 (이동 클립 가중치로) 평균
	float SyncWeight = 0.0f;
	float SyncLength = 0.0f;
	if (bMovingA && WeightA > 0.0f) { SyncLength += LengthA * WeightA; SyncWeight += WeightA; }
	if (bMovingB && WeightB > 0.0f) { SyncLength += LengthB * WeightB; SyncWeight += WeightB; }
	const bool bMoving = SyncWeight > 0.0f && SyncLength > 0.0f;

	// 서 있다가 출발하면 걸음을 처음(왼발)부터 시작한다
	if (bMoving && !bWasMoving)
	{
		Phase = 0.0f;
	}
	bWasMoving = bMoving;

	const float DeltaTime = Context.GetDeltaTime() * PlayRate;
	const float PrevPhase = Phase;
	const float PrevFreeTime = FreeTime;
	float DeltaPhase = 0.0f;
	if (bMoving)
	{
		DeltaPhase = DeltaTime / (SyncLength / SyncWeight);
		Phase = std::fmod(Phase + DeltaPhase, 1.0f);
		if (Phase < 0.0f) Phase += 1.0f;
	}
	FreeTime = std::fmod(FreeTime + DeltaTime, 3600.0f);

	UAnimInstance* AnimInstance = Context.GetAnimInstance<UAnimInstance>();
	FVector Move = FVector::ZeroVector;
	auto Process = [&](const UAnimSequence* Sequence, float Length, float Weight, bool bMovingSample)
	{
		if (Weight <= 0.0f || Length <= 0.0f)
		{
			return;
		}
		// 이 샘플이 이번 프레임에 재생한 구간 (자기 길이 기준)
		const float Start = bMovingSample ? PrevPhase * Length : std::fmod(PrevFreeTime, Length);
		const float Delta = bMovingSample ? DeltaPhase * Length : DeltaTime;

		// Notify는 모든 샘플에서 모으고 가중치로 거른다 (기본 플레이어와 같음)
		if (AnimInstance)
		{
			FAnimNotifyContext NotifyContext(true, Sequence);
			Sequence->GetAnimNotifies(Start, Delta, NotifyContext);
			AnimInstance->AddNotifies(NotifyContext.ActiveNotifies, Context.GetFinalBlendWeight() * Weight);
		}
		// 루트 모션은 이동 클립만. 모든 이동 클립이 앞(+X)으로 가므로 방향도 가중 합한다
		if (bMovingSample)
		{
			const float Distance = MixamoRootMotion::GetTravelDelta(Sequence, Start, Delta, true);
			Move = Move + MixamoRootMotion::GetTravelDirection(Sequence) * (Distance * Weight);
		}
	};
	Process(SampleA.Animation, LengthA, WeightA, bMovingA);
	if (!bSameSample)
	{
		Process(SampleB.Animation, LengthB, WeightB, bMovingB);
	}

	// 상태 전환으로 섞이는 동안에는 이 상태의 가중치만큼만 움직인다
	const float MoveSize = Move.Size();
	const float Distance = MoveSize * Context.GetFinalBlendWeight();
	if (bExtractRootMotion && MoveSize > 0.0f && Distance > 0.0f)
	{
		AddTravelRootMotion(AnimInstance, Move * (1.0f / MoveSize), Distance);
	}
}

void FAnimNode_MixamoBlendSpacePlayer::Evaluate(FPoseContext& Output)
{
	FBlendSample SampleA;
	FBlendSample SampleB;
	float Alpha = 0.0f;
	if (!BlendSpace || !BlendSpace->GetSamples(X, SampleA, SampleB, Alpha)
		|| !SampleA.Animation || !SampleB.Animation
		|| !SampleA.Animation->GetDataModel() || !SampleB.Animation->GetDataModel())
	{
		Output.ResetToRefPose();
		return;
	}

	// 기본 플레이어와 같지만, 섞기 전에 샘플마다 진행 거리를 포즈에서 뺀다 (클립마다 진행량이 달라서)
	FPoseContext PoseA(Output);
	FPoseContext PoseB(Output);
	const float TimeA = GetSampleTime(SampleA.Animation);
	const float TimeB = GetSampleTime(SampleB.Animation);
	const TArray<FBoneInfo>& RefSkeleton = Output.GetRefSkeleton();

	SampleA.Animation->GetAnimationPose(TimeA, PoseA.Pose, RefSkeleton, false);
	SampleB.Animation->GetAnimationPose(TimeB, PoseB.Pose, RefSkeleton, false);
	MixamoRootMotion::RemoveTravelFromPose(SampleA.Animation, TimeA, PoseA.Pose, RefSkeleton);
	MixamoRootMotion::RemoveTravelFromPose(SampleB.Animation, TimeB, PoseB.Pose, RefSkeleton);

	FAnimationRuntime::BlendTwoPosesTogether(
		PoseA.Pose, PoseB.Pose, PoseA.Curve, PoseB.Curve,
		1.0f - Alpha, Output.Pose, Output.Curve);
}
