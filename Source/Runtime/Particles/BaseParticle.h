#pragma once

struct FBaseParticle
{
	FVector Location;

	FVector4 BaseColor;
	FVector4 Color;

	FVector BaseVelocity;
	FVector Velocity;

	FVector BaseSize;
	FVector Size;

	float BaseRotationRate;
	float RotationRate;

	float RelativeTime;
	float OneOverMaxLifeTime;
};
