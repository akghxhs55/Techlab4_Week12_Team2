#include "EnginePCH.h"
#include "ParticleSystemComponent.h"

#include "BaseParticle.h"
#include "ParticleEmitter.h"
#include "ParticleEmitterInstance.h"
#include "ParticleLODLevel.h"
#include "ParticleModuleRequired.h"
#include "ParticleSystem.h"
#include "Asset/AssetManager.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles();
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	Template = InTemplate;
	ResetParticles();
	InitParticles();
}

void UParticleSystemComponent::InitParticles()
{
	if (!Template)
	{
		return;
	}

	EmitterInstances.Reserve(Template->Emitters.Num());
	for (const auto& Emitter : Template->Emitters)
	{
		if (auto Instance = Emitter->CreateInstance(this))
		{
			EmitterInstances.Add(std::move(Instance));
		}
	}
}

void UParticleSystemComponent::ResetParticles()
{
	EmitterInstances.Empty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	if (EmitterInstances.IsEmpty() && Template)
	{
		InitParticles();
	}

	for (const auto& Instance : EmitterInstances)
	{
		Instance->Tick(DeltaTime, bSuppressSpawning);
	}

	FBox WorldBox{
		.Min = FVector{ FLT_MAX },
		.Max = FVector{ -FLT_MAX },
	};
	bool bHasParticle = false;
	for (const auto& Instance : EmitterInstances)
	{
		for (int32 i = 0; i < Instance->ActiveParticles; ++i)
		{
			const FBaseParticle* Particle = Instance->GetParticle(i);
			const FVector Half = Particle->Size * 0.5f;
			WorldBox.Min = FMath::Min(WorldBox.Min, Particle->Location - Half);
			WorldBox.Max = FMath::Max(WorldBox.Max, Particle->Location + Half);

			bHasParticle = true;
		}
	}

	if (bHasParticle)
	{
		CachedLocalBounds = WorldBox.GetWorldAABB(GetWorldMatrix().Inverse());
	}
	else
	{
		CachedLocalBounds = FBox{
			.Min = FVector{ -0.5f },
			.Max = FVector{ 0.5f },
		};
	}
	OnTransformDirty();
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	return CachedLocalBounds;
}

void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!QuadMesh)
	{
		return;
	}

	int32 TotalActive = 0;
	for (const auto& Instance : EmitterInstances)
	{
		TotalActive += Instance->ActiveParticles;
	}
	if (TotalActive == 0)
	{
		return;
	}

	Constants.Reset();
	Constants.Reserve(TotalActive);

	for (const auto& Instance : EmitterInstances)
	{
		UParticleLODLevel* LOD = Instance->CurrentLODLevel;
		UMaterial* Material = (LOD && LOD->RequiredModule) ? LOD->RequiredModule->GetMaterial() : nullptr;
		if (!Material)
		{
			continue;
		}

		for (int32 i = 0; i < Instance->ActiveParticles; ++i)
		{
			const FBaseParticle* Particle = Instance->GetParticle(i);

			FSubUVConstants Value{
				.CurrentFrame = 0.0f,
				.AtlasRowSize = 1.0f,
				.AtlasColSize = 1.0f,
				.Alpha = 1.0f,
				.DecodeSRGB = 0.0f,
			};
			const uint32 ConstantIndex = Constants.Add(Value);

			const FMatrix World = ViewContext.BuildBillboardMatrix(Particle->Location, Particle->Size.X, Particle->Size.Y);
			const FVector Delta = Particle->Location - ViewContext.CameraPosition;

			FRenderPacket Packet{
				.Model = RenderQueue.StoreWorldMatrix(World),
				.Mesh = QuadMesh,
				.Material = Material,
				.MaterialParamData = &Constants[ConstantIndex],
				.CameraToParticleDistance = Delta.Dot(Delta),
				.MaterialParamDataSize = sizeof(FSubUVConstants),
			};
			RenderQueue.Add(Packet);
		}
	}
}
