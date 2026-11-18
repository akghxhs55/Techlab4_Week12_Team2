#include "EnginePCH.h"
#include "ParticleModuleAcceleration.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"

UParticleModuleAcceleration::UParticleModuleAcceleration()
{
	bSpawnModule = true;
	bUpdateModule = true;

	// 기본값은 중력 (cm/s², Z가 위)
	Acceleration.Mode = EDistributionMode::Constant;
	Acceleration.Constant = FVector(0.0f, 0.0f, -980.0f);

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleAcceleration::BakeDistributions()
{
	Acceleration.Bake();
}

void UParticleModuleAcceleration::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	const FVector Value = Acceleration.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);
	const FVector UsedAcceleration = bAlwaysInWorldSpace
		? Owner.WorldToSimulationVector(Value)
		: Owner.EmitterToSimulationVector(Value);

	ParticleHelper::GetPayload<FVector>(Particle, Offset) = UsedAcceleration;

	// 프레임 안에서 먼저 태어난 파티클은 PostSpawn에서 SpawnTime만큼 전진하므로 그만큼 가속도 받는다
	Particle.Velocity += UsedAcceleration * SpawnTime;
	Particle.BaseVelocity += UsedAcceleration * SpawnTime;
}

void UParticleModuleAcceleration::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	ParticleHelper::ForEachParticle(Owner, [Offset, DeltaTime](FBaseParticle& Particle, uint8* ParticleBase)
		{
			const FVector& UsedAcceleration = ParticleHelper::GetPayload<FVector>(ParticleBase, Offset);

			// 가속은 누적되는 운동 상태라 BaseVelocity에도 더한다. Reset이 매 프레임 Velocity = BaseVelocity로 되돌린다
			Particle.Velocity += UsedAcceleration * DeltaTime;
			Particle.BaseVelocity += UsedAcceleration * DeltaTime;
		});
}
