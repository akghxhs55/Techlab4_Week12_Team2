#include "EnginePCH.h"
#include "ParticleModuleVelocity.h"
#include "Particles/ParticleEmitterInstance.h"

UParticleModuleVelocity::UParticleModuleVelocity()
{
	bSpawnModule = true;

	// 기본값: 위로 퍼지는 원뿔 느낌의 Uniform
	StartVelocity.Mode = EDistributionMode::Uniform;
	StartVelocity.MinValue = FVector(-20.0f, -20.0f, 80.0f);
	StartVelocity.MaxValue = FVector(20.0f, 20.0f, 120.0f);

	StartVelocityRadial.Mode = EDistributionMode::Constant;
	StartVelocityRadial.Constant = 0.0f;

	// Build() 전에도 기본값이 동작하도록 한 번 굽는다
	BakeDistributions();
}

void UParticleModuleVelocity::BakeDistributions()
{
	StartVelocity.Bake();
	StartVelocityRadial.Bake();
}

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	FVector Vel = StartVelocity.GetVectorValue(Owner.EmitterTime, Owner.RandomStream);

	// TODO: PSC 단계에서 원점을 컴포넌트 월드 위치로 교체 (지금은 PreSpawn 위치가 원점)
	const FVector EmitterOrigin = FVector::ZeroVector;
	const FVector FromOrigin = (Particle.Location - EmitterOrigin).GetSafeNormal();
	Vel += FromOrigin * StartVelocityRadial.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);

	// Reset이 매 프레임 Base → 현재값으로 복원하므로 둘 다 더한다
	Particle.Velocity += Vel;
	Particle.BaseVelocity += Vel;
}
