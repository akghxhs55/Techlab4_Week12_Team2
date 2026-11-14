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

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	// Unregister 없이 소멸하는 경우 대비
	ResetParticles();
	DestroyOwnedTemplate();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

	// 에셋이 없으면 테스트 템플릿. PIE 복제본도 Template이 비어 있으므로 여기서 자기 것을 만든다
	if (!Template)
	{
		CreateDefaultTemplate();
	}

	// 에디터 월드는 BeginPlay가 없으므로 여기서 시작한다 (PIE는 BeginPlay → Activate가 한 번 더 오지만 인스턴스가 있으면 재생성하지 않음)
	if (IsAutoActivate())
	{
		ActivateSystem();
	}
}

void UParticleSystemComponent::OnUnregister()
{
	// 생성의 역순: 내 것을 먼저 정리하고 부모
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
	// 부모는 bIsActive = false + Tick 끔. Tick이 꺼지면 남은 파티클이 그 자리에 멈추므로 다시 켠다
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
		// 팩토리를 거쳐야 나중에 Mesh 에미터면 Mesh 인스턴스로 분기할 수 있다
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
	// 스프라이트는 동적 VB를 매 프레임 갱신하므로 렌더 상태 재생성(MarkRenderStateDirty)은 하지 않는다
}

void UParticleSystemComponent::CreateDefaultTemplate()
{
	UParticleSystem*   System  = NewObject<UParticleSystem>(this);
	UParticleEmitter*  Emitter = NewObject<UParticleEmitter>(System);
	UParticleLODLevel* LOD     = NewObject<UParticleLODLevel>(Emitter);

	UParticleModuleRequired* Required = NewObject<UParticleModuleRequired>(LOD);
	UParticleModuleSpawn*    Spawn    = NewObject<UParticleModuleSpawn>(LOD);
	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(LOD);
	UParticleModuleVelocity* Velocity = NewObject<UParticleModuleVelocity>(LOD);

	LOD->SetRequiredModule(Required);
	LOD->SetSpawnModule(Spawn);
	LOD->AddModule(Lifetime);
	LOD->AddModule(Velocity);
	Emitter->AddLODLevel(LOD);
	System->AddEmitter(Emitter);

	// 인스턴스 Init 전에 Bake + 레이아웃(ParticleSize) 계산이 끝나 있어야 한다
	System->BuildEmitters();

	// 자식 → 부모 순서로 기록 (해제도 이 순서)
	OwnedObjects = { Required, Spawn, Lifetime, Velocity, LOD, Emitter, System };
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
