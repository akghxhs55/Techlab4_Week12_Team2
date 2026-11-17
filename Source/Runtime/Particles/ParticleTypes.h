#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Color.h"

struct FBaseParticle
{
	// 16 bytes
	FVector OldLocation;
	float RelativeTime;

	// 16 bytes
	FVector Location;
	float OneOverMaxLifetime;

	// 16 bytes
	FVector BaseVelocity;
	float Rotation;

	// 16 bytes
	FVector Velocity;
	float BaseRotationRate;

	// 16 bytes
	FVector BaseSize;
	float RotationRate;

	// 16 bytes
	FVector Size;
	uint32 Flags;

	// 16 bytes
	FLinearColor Color;

	// 16 bytes
	FLinearColor BaseColor;
};
static_assert(sizeof(FBaseParticle) % 16 == 0, "FBaseParticle은 16바이트 배수여야 한다 (ParticleStride 정렬)");

enum EParticleStates : uint32
{
	STATE_Particle_JustSpawned = 0x02000000,
	STATE_Particle_Freeze = 0x04000000,
	STATE_Particle_IgnoreCollisions = 0x08000000,
	STATE_Particle_FreezeTranslation = 0x10000000,
	STATE_Particle_FreezeRotation = 0x20000000,
	STATE_Particle_CollisionIgnoreCheck = STATE_Particle_Freeze | STATE_Particle_IgnoreCollisions | STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation,
	STATE_Particle_DelayCollisions = 0x40000000,
	STATE_Particle_CollisionHasOccurred = 0x80000000,
	STATE_Mask = 0xFE000000,
	STATE_CounterMask = (~STATE_Mask)
};

// 메시 에미터가 파티클마다 붙이는 Payload. 단위는 모두 도(degree)
struct FMeshRotationPayloadData
{
	FVector InitialOrientation;		// 생성될 때의 회전. X = Roll, Y = Pitch, Z = Yaw
	FVector Rotation;				// 생성 후 누적된 회전
	FVector RotationRate;			// 이번 프레임 회전 속도(도/초). 모듈이 바꿀 수 있는 현재 값
	FVector RotationRateBase;		// 회전 속도의 기준값. 매 프레임 RotationRate로 복원
};
static_assert(sizeof(FMeshRotationPayloadData) == 48, "FMeshRotationPayloadData 크기 오류, 48이어야 합니다.");

struct FBeamPayloadData
{
	FVector SourcePoint;   // 월드 좌표
	float   Pad0;
	FVector TargetPoint;   // 월드 좌표
	float   Pad1;
};
static_assert(sizeof(FBeamPayloadData) == 32, "FBeamPayloadData 크기 오류, 32여야 합니다.");

// BeamNoise 모듈 Payload의 고정 부분. 바로 뒤에 FBeamNoisePoint가 (Frequency + 1)개 붙는다
struct FBeamNoiseHeader
{
	float LockTimer;   // 0 이하가 되면 새 목표를 뽑는다
	float Pad[3];
};
static_assert(sizeof(FBeamNoiseHeader) == 16, "FBeamNoiseHeader 크기 오류, 16이어야 합니다.");

// 노이즈 점 하나. 빔 축에 수직인 평면의 (Right, Up) 오프셋
struct FBeamNoisePoint
{
	FVector2 Current;   // 지금 위치. 렌더가 읽는다
	FVector2 Target;    // 움직여 갈 목표
};
static_assert(sizeof(FBeamNoisePoint) == 16, "FBeamNoisePoint 크기 오류, 16이어야 합니다.");

struct FRibbonPayloadData
{
	float TiledU;   // 생성 시점까지 누적 이동거리 / TilingDistance
	uint32 TrailId; // 텔레포트, 재활성화로 끊길 때마다 바뀐다. 렌더는 같은 Id끼리만 잇는다
	float Pad[2];
};
static_assert(sizeof(FRibbonPayloadData) == 16, "FRibbonPayloadData 크기 오류, 16이어야 합니다.");