#include "EnginePCH.h"
#include "ParticleSystemRender.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSubUV.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"

#include <algorithm>

float FDynamicEmitterDataBase::GetPacketDistance(const FParticleSubmitContext& Context)
{
	const float OrderBias = 1.0f + static_cast<float>(Context.EmitterCount - Context.EmitterIndex) * 1e-4f;
	return Context.BoundsDistanceSq * OrderBias;
}

bool FDynamicSpriteEmitterData::EnsureInstanceBuffer(const FParticleEmitterInstance& Instance)
{
	if (!InstanceBuffer || Capacity < Instance.ActiveParticles)
	{
		Capacity = Instance.MaxActiveParticles;
		InstanceBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteInstance) * Capacity, sizeof(FParticleSpriteInstance));
	}
	return InstanceBuffer != nullptr;
}

int32 FDynamicSpriteEmitterData::BuildInstances(FParticleEmitterInstance& Instance, const FViewContext& View, EParticleSortMode SortMode)
{
	SortScratch.SetNum(0, false);
	const FVector CameraPos = View.CameraPosition;
	const FVector Forward = View.CameraForward.Normalized();

	ParticleHelper::ForEachParticle(Instance, [&](FBaseParticle& Particle, uint8* ParticleBase)
		{
			float Key = 0.0f;
			switch (SortMode)
			{
			case EParticleSortMode::ViewProjDepth:
				Key = (Instance.SimulationToWorld(Particle.Location) - CameraPos).Dot(Forward);
				break;
			case EParticleSortMode::DistanceToView:
			{
				const FVector D = Instance.SimulationToWorld(Particle.Location) - CameraPos;
				Key = D.Dot(D);
				break;
			}
			case EParticleSortMode::Age_OldestFirst:
			case EParticleSortMode::Age_NewestFirst:
				Key = static_cast<float>(Particle.Flags & STATE_CounterMask);
				break;
			default:
				break;
			}
			SortScratch.Add({ Key, &Particle, ParticleBase });
		});

	// 거리/깊이: 먼 것부터 (뒤에서 앞으로 그려야 AlphaBlend가 맞다)
	// Age: OldestFirst는 순번이 작은 것부터
	switch (SortMode)
	{
	case EParticleSortMode::ViewProjDepth:
	case EParticleSortMode::DistanceToView:
	case EParticleSortMode::Age_NewestFirst:
		std::sort(SortScratch.begin(), SortScratch.end(),
			[](const FParticleSortEntry& A, const FParticleSortEntry& B) { return A.Key > B.Key; });
		break;
	case EParticleSortMode::Age_OldestFirst:
		std::sort(SortScratch.begin(), SortScratch.end(),
			[](const FParticleSortEntry& A, const FParticleSortEntry& B) { return A.Key < B.Key; });
		break;
	default:
		break;
	}

	const int32 Count = SortScratch.Num();
	InstanceScratch.SetNum(Count, false);
	const int32 SubUVOffset = Instance.SubUVDataOffset;

	for (int32 i = 0; i < Count; ++i)
	{
		const FBaseParticle& Particle = *SortScratch[i].Particle;
		FParticleSpriteInstance& Out = InstanceScratch[i];
		Out.Position = Instance.SimulationToWorld(Particle.Location);
		Out.Rotation = Particle.Rotation;
		Out.Size = FVector2(Particle.Size.X, Particle.Size.Y);
		Out.SubImageIndex = SubUVOffset > 0
			? ParticleHelper::GetPayload<FFullSubUVPayload>(const_cast<uint8*>(SortScratch[i].ParticleBase), SubUVOffset).ImageIndex
			: 0.0f;
		Out.Color = Particle.Color;
	}
	return Count;
}

void FDynamicSpriteEmitterData::Submit(FRenderQueue& Queue, FParticleEmitterInstance& Instance, const FParticleSubmitContext& Context)
{
	if (!Context.View || !Context.SpriteQuadVertexBuffer || !Context.SpriteQuadIndexBuffer)
		return;
	if (!EnsureInstanceBuffer(Instance))
		return;

	const UParticleModuleRequired* Required = Instance.CurrentLODLevel ? Instance.CurrentLODLevel->GetRequiredModule() : nullptr;
	UMaterial* EmitterMaterial = (Required && Required->GetMaterial()) ? Required->GetMaterial() : Context.DefaultMaterial;
	if (!EmitterMaterial)
		return;
	const EParticleSortMode SortMode = Required ? Required->GetSortMode() : EParticleSortMode::None;

	const int32 Count = BuildInstances(Instance, *Context.View, SortMode);
	if (Count == 0)
		return;

	RenderCommand::UpdateBufferData(InstanceBuffer.get(),
		InstanceScratch.GetData(), sizeof(FParticleSpriteInstance) * Count);

	Constants.CameraRight = FVector4(Context.CameraRight.X, Context.CameraRight.Y, Context.CameraRight.Z, 0.0f);
	Constants.CameraUp = FVector4(Context.CameraUp.X, Context.CameraUp.Y, Context.CameraUp.Z, 0.0f);
	if (Required && Required->UsesSubUV() && Instance.SubUVDataOffset > 0)
	{
		const float H = static_cast<float>(Required->GetSubImagesHorizontal());
		const float V = static_cast<float>(Required->GetSubImagesVertical());
		Constants.SubImageSize = FVector4(H, V, 1.0f / H, 1.0f / V);
	}
	else
	{
		Constants.SubImageSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	FRenderPacket Packet;
	Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
	Packet.VertexBuffer = Context.SpriteQuadVertexBuffer;
	Packet.IndexBuffer = Context.SpriteQuadIndexBuffer;
	Packet.StartIndex = 0;
	Packet.IndexCount = 6;
	Packet.InstanceBuffer = InstanceBuffer.get();
	Packet.InstanceCount = static_cast<uint32>(Count);
	Packet.Material = EmitterMaterial;
	Packet.MaterialParamData = &Constants;
	Packet.MaterialParamDataSize = sizeof(FParticleSpriteConstants);
	Packet.CameraToParticleDistance = GetPacketDistance(Context);
	Queue.Add(Packet);
}
