#include "EnginePCH.h"
#include "ParticleModuleEventReceiverSpawn.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Components/ParticleSystemComponent.h"

#include <cmath>

UParticleModuleEventReceiverSpawn::UParticleModuleEventReceiverSpawn()
{
	SpawnCount.Mode = EDistributionMode::Constant;
	SpawnCount.Constant = 5.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleEventReceiverSpawn::BakeDistributions()
{
	Super::BakeDistributions();
	SpawnCount.Bake();
}

void UParticleModuleEventReceiverSpawn::ProcessEvent(FParticleEmitterInstance& Owner, const FParticleEventData& Event)
{
	const int32 Count = static_cast<int32>(std::lround(SpawnCount.GetFloatValue(Owner.EmitterTime, Owner.RandomStream)));
	if (Count <= 0)
		return;

	FVector Location = Event.Location;
	if (bUseEmitterLocation && Owner.Component)
	{
		Location = Owner.Component->GetWorldLocation();
	}
	const FVector Velocity = bInheritVelocity ? Event.Velocity * InheritVelocityScale : FVector::ZeroVector;

	Owner.ForceSpawn(Count, Location, Velocity);
}
