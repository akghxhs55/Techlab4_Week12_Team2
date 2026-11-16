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