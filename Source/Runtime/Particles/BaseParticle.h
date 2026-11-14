#pragma once

struct FBaseParticle
{
	FVector Location;

	FVector BaseVelocity;
	FVector Velocity;

	FVector BaseSize;
	FVector Size;

	float BaseRotationRate;
	float RotationRate;

	float RelativeTime;
	float OneOverMaxLifeTime;
};
