#include "EnginePCH.h"
#include "GameFramework/Actor/GundamBeam.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"

AGundamBeam::AGundamBeam()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("BeamParticleComponent");
	SetRootComponent(ParticleSystemComponent);

	if (FPaths::HasProject())
	{
		if (UParticleSystem* BeamTemplate = UAssetManager::LoadParticleSystem("Particles/BeamRifle.particle"))
		{
			ParticleSystemComponent->InitializeSystem(BeamTemplate);
		}
	}
}

void AGundamBeam::BeginPlay()
{
	Super::BeginPlay();

	if (ParticleSystemComponent && !ParticleSystemComponent->GetTemplate())
	{
		if (UParticleSystem* BeamTemplate = UAssetManager::LoadParticleSystem("Particles/BeamRifle.particle"))
		{
			ParticleSystemComponent->SetTemplate(BeamTemplate);
		}
	}
}

void AGundamBeam::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;
	if (ElapsedTime >= LifeTime)
	{
		Destroy();
	}
}
