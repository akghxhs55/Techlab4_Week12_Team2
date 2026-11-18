#include "EnginePCH.h"
#include "GameFramework/Actor/GundamFunnel.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"

AGundamFunnel::AGundamFunnel()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FunnelMeshComponent");
	SetRootComponent(StaticMeshComponent);
	StaticMeshComponent->SetRelativeScale3D(FVector(1.8f, 1.8f, 1.8f));

	TrailParticleComponent = CreateDefaultSubobject<UParticleSystemComponent>("FunnelTrailComponent");
	TrailParticleComponent->SetupAttachment(StaticMeshComponent);
	TrailParticleComponent->SetRelativeLocation(FVector(-0.28f, 0.0f, 0.0f));

	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovementComponent");
	ProjectileMovementComponent->ProjectileGravityScale = 0.0f;
	ProjectileMovementComponent->bShouldBounce = false;
	ProjectileMovementComponent->bSweepCollision = false;
	ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;
	ProjectileMovementComponent->bRotationFollowsVelocity = true;
	ProjectileMovementComponent->bSimulationEnabled = false;
	ProjectileMovementComponent->InintialSpeed = 0.0f;
	ProjectileMovementComponent->MaxSpeed = 0.0f;

	if (FPaths::HasProject())
	{
		if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Meshes/Funnel/Funnel.obj"))
		{
			StaticMeshComponent->SetStaticMesh(Mesh);
		}
		if (UParticleSystem* TrailTemplate = UAssetManager::LoadParticleSystem("Particles/FunnelTrail.particle"))
		{
			TrailParticleComponent->InitializeSystem(TrailTemplate);
		}
	}
}

void AGundamFunnel::BeginPlay()
{
	Super::BeginPlay();

	if (StaticMeshComponent && !StaticMeshComponent->GetStaticMesh())
	{
		if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Meshes/Funnel/Funnel.obj"))
		{
			StaticMeshComponent->SetStaticMesh(Mesh);
		}
	}

	if (TrailParticleComponent && !TrailParticleComponent->GetTemplate())
	{
		if (UParticleSystem* TrailTemplate = UAssetManager::LoadParticleSystem("Particles/FunnelTrail.particle"))
		{
			TrailParticleComponent->SetTemplate(TrailTemplate);
		}
	}
}

void AGundamFunnel::Launch(const FVector& WorldVelocity, float InActiveDuration, float InTrailFadeOut)
{
	bLaunched = true;
	bFadingOut = false;
	ElapsedTime = 0.0f;
	ActiveDuration = InActiveDuration;
	TrailFadeOutDuration = InTrailFadeOut;

	if (ProjectileMovementComponent)
	{
		ProjectileMovementComponent->SetUpdatedComponent(StaticMeshComponent);
		ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;
		ProjectileMovementComponent->Velocity = WorldVelocity;
		ProjectileMovementComponent->bSimulationEnabled = true;
	}
}

void AGundamFunnel::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bLaunched)
	{
		return;
	}

	ElapsedTime += DeltaTime;

	if (!bFadingOut && ElapsedTime >= ActiveDuration)
	{
		bFadingOut = true;

		if (StaticMeshComponent)
		{
			StaticMeshComponent->SetVisibility(false, false);
		}
		if (ProjectileMovementComponent)
		{
			ProjectileMovementComponent->StopMovementImmediately();
			ProjectileMovementComponent->bSimulationEnabled = false;
		}
		if (TrailParticleComponent)
		{
			TrailParticleComponent->DeactivateSystem();
		}
	}

	if (bFadingOut && ElapsedTime >= ActiveDuration + TrailFadeOutDuration)
	{
		Destroy();
	}
}
