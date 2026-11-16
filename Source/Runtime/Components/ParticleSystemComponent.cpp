#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSpawn.h"
#include "Particles/Module/ParticleModuleLifetime.h"
#include "Particles/Module/ParticleModuleVelocity.h"
#include "Particles/Module/ParticleModuleSize.h"
#include "Particles/Module/ParticleModuleColor.h"
#include "Particles/Module/ParticleModuleSubUV.h"
#include "Particles/Module/ParticleModuleLocation.h"
#include "Particles/Module/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include <cmath>
#include "Particles/ParticleHelper.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Engine/ParticleSystemSceneProxy.h"
#include "Engine/Scene.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bCastShadow = false;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles();
	DestroyOwnedTemplate();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

	// PIE 복제본은 Template이 복사되지 않으므로 여기서 자기 것을 만든다
	if (!Template)
	{
		CreateDefaultTemplate();
	}

	// 에디터 월드는 BeginPlay가 없으므로 여기서 시작
	if (IsAutoActivate())
	{
		ActivateSystem();
	}
}

void UParticleSystemComponent::OnUnregister()
{
	ResetParticles();
	Super::OnUnregister();
}

void UParticleSystemComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	ActivateSystem(bReset);
}

void UParticleSystemComponent::Deactivate()
{
	// 부모가 Tick을 끄면 남은 파티클이 그 자리에 멈추므로 다시 켠다
	Super::Deactivate();
	SetComponentTickEnabled(true);
	DeactivateSystem();
}

void UParticleSystemComponent::InitializeSystem(UParticleSystem* InTemplate)
{
	ResetParticles();

	if (InTemplate)
	{
		Template = InTemplate;
	}
	if (!Template)
		return;

	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (!Emitter)
			continue;
		if (FParticleEmitterInstance* Instance = Emitter->CreateInstance(this))
		{
			EmitterInstances.Add(Instance);
		}
	}
	bWasCompleted = false;
}

void UParticleSystemComponent::ActivateSystem(bool bReset)
{
	if (EmitterInstances.IsEmpty() || bReset)
	{
		InitializeSystem();
	}

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = true;
	}
	bWasCompleted = false;
}

void UParticleSystemComponent::DeactivateSystem()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = false;
	}
}

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.Empty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (EmitterInstances.IsEmpty())
		return;

	bool bAllComplete = true;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->Tick(DeltaTime);
		if (!Instance->IsComplete())
		{
			bAllComplete = false;
		}
	}
	bWasCompleted = bAllComplete;

	// 바운드는 Transform이 바뀔 때만 갱신되므로 매 프레임 요청
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->GetScene()->MarkDirty(Proxy);
	}
}

void UParticleSystemComponent::CreateDefaultTemplate()
{
	UParticleSystem*   System  = NewObject<UParticleSystem>(this);
	UParticleEmitter*  Emitter = NewObject<UParticleEmitter>(System);
	UParticleLODLevel* LOD     = NewObject<UParticleLODLevel>(Emitter);

	UParticleModuleRequired* Required = NewObject<UParticleModuleRequired>(LOD);
	UParticleModuleSpawn*    Spawn    = NewObject<UParticleModuleSpawn>(LOD);
	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(LOD);
	UParticleModuleLocation* Location = NewObject<UParticleModuleLocation>(LOD);
	UParticleModuleVelocity* Velocity = NewObject<UParticleModuleVelocity>(LOD);
	UParticleModuleSize*     Size     = NewObject<UParticleModuleSize>(LOD);
	UParticleModuleColor*    Color    = NewObject<UParticleModuleColor>(LOD);
	UParticleModuleSubUV*    SubUV    = NewObject<UParticleModuleSubUV>(LOD);

	// StarParticle.png는 8x8 아틀라스
	Required->SetSubUV(EParticleSubUVInterpMethod::Random, 8, 8, 1.0f);

	LOD->SetRequiredModule(Required);
	LOD->SetSpawnModule(Spawn);
	LOD->AddModule(Lifetime);
	// Velocity의 Radial이 이동된 위치를 기준으로 방향을 잡도록 먼저
	LOD->AddModule(Location);
	LOD->AddModule(Velocity);
	LOD->AddModule(Size);
	LOD->AddModule(Color);
	LOD->AddModule(SubUV);
	Emitter->AddLODLevel(LOD);
	System->AddEmitter(Emitter);

	// 메시 에미터: TypeDataMesh가 있으면 에미터가 메시 인스턴스를 만든다
	UParticleEmitter*  MeshEmitter = NewObject<UParticleEmitter>(System);
	UParticleLODLevel* MeshLOD     = NewObject<UParticleLODLevel>(MeshEmitter);

	UParticleModuleRequired*     MeshRequired = NewObject<UParticleModuleRequired>(MeshLOD);
	UParticleModuleSpawn*        MeshSpawn    = NewObject<UParticleModuleSpawn>(MeshLOD);
	UParticleModuleLifetime*     MeshLifetime = NewObject<UParticleModuleLifetime>(MeshLOD);
	UParticleModuleLocation*     MeshLocation = NewObject<UParticleModuleLocation>(MeshLOD);
	UParticleModuleVelocity*     MeshVelocity = NewObject<UParticleModuleVelocity>(MeshLOD);
	UParticleModuleSize*         MeshSize     = NewObject<UParticleModuleSize>(MeshLOD);
	UParticleModuleColor*        MeshColor    = NewObject<UParticleModuleColor>(MeshLOD);
	UParticleModuleTypeDataMesh* MeshTypeData = NewObject<UParticleModuleTypeDataMesh>(MeshLOD);

	MeshTypeData->SetMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cube"));
	MeshSpawn->GetRate().Constant = 5.0f;
	MeshLifetime->GetLifetime().Constant = 2.0f;
	MeshSize->GetStartSize().Constant = FVector(3.0f, 3.0f, 3.0f);

	MeshLOD->SetRequiredModule(MeshRequired);
	MeshLOD->SetSpawnModule(MeshSpawn);
	MeshLOD->SetTypeDataModule(MeshTypeData);
	MeshLOD->AddModule(MeshLifetime);
	MeshLOD->AddModule(MeshLocation);
	MeshLOD->AddModule(MeshVelocity);
	MeshLOD->AddModule(MeshSize);
	MeshLOD->AddModule(MeshColor);
	MeshEmitter->AddLODLevel(MeshLOD);
	System->AddEmitter(MeshEmitter);

	// 인스턴스 Init 전에 ParticleSize 계산이 끝나 있어야 한다 (생성 후 바꾼 분포도 여기서 다시 굽는다)
	System->BuildEmitters();

	OwnedObjects = { Required, Spawn, Lifetime, Location, Velocity, Size, Color, SubUV, LOD, Emitter,
		MeshRequired, MeshSpawn, MeshLifetime, MeshLocation, MeshVelocity, MeshSize, MeshColor, MeshTypeData, MeshLOD, MeshEmitter,
		System };
	Template = System;
	bOwnsTemplate = true;
}

void UParticleSystemComponent::DestroyOwnedTemplate()
{
	if (!bOwnsTemplate)
		return;

	for (UObject* Object : OwnedObjects)
	{
		delete Object;
	}
	OwnedObjects.Empty();
	Template = nullptr;
	bOwnsTemplate = false;
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	FBox SimBounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAny = false;
	bool bLocalSpace = false;
	// 위치만 담긴 박스라 스프라이트 크기만큼 기본 여유를 준다
	float PadSize = 50.0f;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		SimBounds.Expand(Instance->BoundingBox);
		bLocalSpace = Instance->IsLocalSpace();
		bAny = true;

		// 메시 파티클은 메시 반경 × 최대 크기만큼 튀어나온다
		const FParticleMeshEmitterInstance* MeshInstance = dynamic_cast<const FParticleMeshEmitterInstance*>(Instance);
		const UParticleModuleTypeDataMesh* TypeData = MeshInstance ? MeshInstance->GetMeshTypeData() : nullptr;
		const UStaticMesh* Mesh = TypeData ? TypeData->GetMesh() : nullptr;
		if (Mesh)
		{
			const FBox& MeshBounds = Mesh->GetRenderBounds();
			const FVector MaxCorner(
				std::max(std::abs(MeshBounds.Min.X), std::abs(MeshBounds.Max.X)),
				std::max(std::abs(MeshBounds.Min.Y), std::abs(MeshBounds.Max.Y)),
				std::max(std::abs(MeshBounds.Min.Z), std::abs(MeshBounds.Max.Z)));
			const float MeshRadius = MaxCorner.Length();

			float MaxParticleSize = 0.0f;
			ParticleHelper::ForEachParticle(*Instance, [&](FBaseParticle& Particle, uint8*)
				{
					MaxParticleSize = std::max(MaxParticleSize,
						std::max(std::abs(Particle.Size.X), std::max(std::abs(Particle.Size.Y), std::abs(Particle.Size.Z))));
				});
			PadSize = std::max(PadSize, MeshRadius * MaxParticleSize);
		}
	}
	if (!bAny)
		return FBox(FVector(-1.0f), FVector(1.0f));

	const FVector Pad(PadSize);
	SimBounds = FBox(SimBounds.Min - Pad, SimBounds.Max + Pad);

	// 프록시가 LocalToWorld를 다시 곱하므로 World Space면 로컬로 되돌린다
	return bLocalSpace ? SimBounds : SimBounds.GetWorldAABB(GetWorldMatrix().Inverse());
}



void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher)
		return;

	constexpr float HalfLength = 3.0f;
	const FVector4 Color(1.0f, 1.0f, 0.0f, 1.0f);

	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* PSC = *It;
		if (!PSC || !PSC->IsRegistered() || PSC->GetWorld() != World)
			continue;
		if (Selected && PSC->GetOwner() != Selected)
			continue;

		for (FParticleEmitterInstance* Instance : PSC->GetEmitterInstances())
		{
			if (!Instance || Instance->ActiveParticles == 0)
				continue;

			ParticleHelper::ForEachParticle(*Instance, [&](FBaseParticle& Particle, uint8* /*ParticleBase*/)
				{
						const FVector P = Instance->SimulationToWorld(Particle.Location);

					LineBatcher->AddLine(P - FVector(HalfLength, 0, 0), P + FVector(HalfLength, 0, 0), Color);
					LineBatcher->AddLine(P - FVector(0, HalfLength, 0), P + FVector(0, HalfLength, 0), Color);
					LineBatcher->AddLine(P - FVector(0, 0, HalfLength), P + FVector(0, 0, HalfLength), Color);
				});
		}
	}
}