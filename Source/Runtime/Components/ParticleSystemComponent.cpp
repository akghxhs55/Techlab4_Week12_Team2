#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/Module/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Render/Mesh.h"
#include "Particles/ParticleHelper.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Scene.h"
#include "Engine/ParticleSystemSceneProxy.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bCastShadow = false;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

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

void UParticleSystemComponent::SetTemplate(UParticleSystem* NewTemplate)
{
	if (Template == NewTemplate && !EmitterInstances.IsEmpty())
	{
		return;
	}

	ResetParticles();
	Template = NewTemplate;
	bWasCompleted = false;

	if (!Template)
		return;

	if (IsRegistered() && (IsActive() || IsAutoActivate()))
	{
		ActivateSystem(true);
	}
	else
	{
		InitializeSystem();
	}
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
	SendRenderDynamicData();
}

void UParticleSystemComponent::SendRenderDynamicData()
{
	FParticleSystemSceneProxy* Proxy = static_cast<FParticleSystemSceneProxy*>(GetSceneProxy());
	if (!Proxy)
		return;

	TArray<TUniquePtr<FDynamicEmitterDataBase>>& RenderData = Proxy->GetDynamicEmitterData();
	if (RenderData.Num() != EmitterInstances.Num())
	{
		RenderData.Empty();
		RenderData.SetNum(EmitterInstances.Num());
	}
	for (int32 Index = 0; Index < EmitterInstances.Num(); ++Index)
	{
		if (FParticleEmitterInstance* Instance = EmitterInstances[Index])
			Instance->UpdateDynamicData(RenderData[Index]);
		else
			RenderData[Index].reset();
	}
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
	SendRenderDynamicData();
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

FBox UParticleSystemComponent::CalcBounds() const
{
	// 에미터가 자기 공간(Local/World)에 맞춰 월드 박스를 만들어 두므로 합치기만 한다
	FBox Bounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAny = false;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		Bounds.Expand(Instance->BoundingBox);
		bAny = true;
	}
	if (!bAny)
	{
		const FVector Location = GetWorldLocation();
		return FBox(Location - FVector(1.0f), Location + FVector(1.0f));
	}
	return Bounds;
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	// 로컬 박스를 요구하는 곳(디버그 박스)용. 회전이 있으면 실제보다 커진다
	return CalcBounds().GetWorldAABB(GetWorldMatrix().Inverse());
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