#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"
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
	JustSpawned = 0x02000000,
	Freeze = 0x04000000,
	IgnoreCollisions = 0x08000000,
	FreezeTranslation = 0x10000000,
	FreezeRotation = 0x20000000,
	CollisionIgnoreCheck = Freeze | IgnoreCollisions | FreezeTranslation | FreezeRotation,
	DelayCollisions = 0x40000000,
	CollisionHasOccurred = 0x80000000,
	STATE_Mask = 0xFE000000,
	STATE_CounterMask = (~STATE_Mask)
};