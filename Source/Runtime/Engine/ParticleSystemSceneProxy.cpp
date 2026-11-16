#include "EnginePCH.h"
#include "ParticleSystemSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSubUV.h"

#include <cmath>
#include <algorithm>

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bCastsShadow = false;
}

void FParticleSystemSceneProxy::UpdateRenderState()
{
	// 부모는 StaticMesh 렌더 상태를 만들므로 부르지 않는다
	RenderState.reset();
	UpdateCommonFlags();
	bCastsShadow = false;

	// Required에 머티리얼이 없는 에미터가 쓰는 기본값
	DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
}

bool FParticleSystemSceneProxy::EnsureBuffers(int32 EmitterIndex, const FParticleEmitterInstance& Instance)
{
	if (!QuadVertexBuffer)
	{
		const FParticleSpriteCorner Corners[4] = {
			{ FVector2(0.0f, 1.0f) },{ FVector2(0.0f, 0.0f) },{ FVector2(1.0f, 0.0f) },{ FVector2(1.0f, 1.0f) } };
		const uint32 Indices[6] = { 0, 1, 2, 0, 2, 3 };
		QuadVertexBuffer = RenderCommand::CreateStaticVertexBuffer(Corners, sizeof(Corners), sizeof(FParticleSpriteCorner));
		QuadIndexBuffer = RenderCommand::CreateStaticIndexBuffer(Indices, 6);
	}

	FParticleSpriteRenderData& Data = EmitterRenderData[EmitterIndex];
	if (!Data.InstanceBuffer || Data.Capacity < Instance.ActiveParticles)
	{
		Data.Capacity = Instance.MaxActiveParticles;
		Data.InstanceBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteInstance) * Data.Capacity, sizeof(FParticleSpriteInstance));
	}
	return QuadVertexBuffer && QuadIndexBuffer && Data.InstanceBuffer;
}

int32 FParticleSystemSceneProxy::BuildSpriteInstances(FParticleEmitterInstance& Instance, const FViewContext& View, EParticleSortMode SortMode)
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

void FParticleSystemSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	// 프록시가 머티리얼 등록보다 먼저 만들어졌을 수 있다
	if (!DefaultMaterial)
	{
		DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
	}

	const UParticleSystemComponent* PSC = static_cast<const UParticleSystemComponent*>(GetComponent());
	const TArray<FParticleEmitterInstance*>& Instances = PSC->GetEmitterInstances();
	if (EmitterRenderData.Num() != Instances.Num())
	{
		EmitterRenderData.Empty();
		EmitterRenderData.SetNum(Instances.Num());
	}

	// FViewContext::BuildBillboardMatrix와 같은 축 (왼손, X 앞 / Y 오른쪽 / Z 위)
	const FVector Forward = View.CameraForward.Normalized();
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
		Right = FVector(0.0f, 1.0f, 0.0f);
	const FVector Up = FVector::Cross(Forward, Right).Normalized();

	const FVector Delta = GetBounds().Center - View.CameraPosition;
	const float DistanceSq = Delta.Dot(Delta);

	for (int32 EmitterIndex = 0; EmitterIndex < Instances.Num(); ++EmitterIndex)
	{
		FParticleEmitterInstance* Instance = Instances[EmitterIndex];
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		if (!EnsureBuffers(EmitterIndex, *Instance))
			continue;

		const UParticleModuleRequired* Required = Instance->CurrentLODLevel ? Instance->CurrentLODLevel->GetRequiredModule() : nullptr;
		UMaterial* EmitterMaterial = (Required && Required->GetMaterial()) ? Required->GetMaterial() : DefaultMaterial;
		if (!EmitterMaterial)
			continue;
		const EParticleSortMode SortMode = Required ? Required->GetSortMode() : EParticleSortMode::None;

		const int32 Count = BuildSpriteInstances(*Instance, View, SortMode);
		if (Count == 0)
			continue;

		FParticleSpriteRenderData& Data = EmitterRenderData[EmitterIndex];
		RenderCommand::UpdateBufferData(Data.InstanceBuffer.get(),
			InstanceScratch.GetData(), sizeof(FParticleSpriteInstance) * Count);

		Data.Constants.CameraRight = FVector4(Right.X, Right.Y, Right.Z, 0.0f);
		Data.Constants.CameraUp = FVector4(Up.X, Up.Y, Up.Z, 0.0f);
		if (Required && Required->UsesSubUV() && Instance->SubUVDataOffset > 0)
		{
			const float H = static_cast<float>(Required->GetSubImagesHorizontal());
			const float V = static_cast<float>(Required->GetSubImagesVertical());
			Data.Constants.SubImageSize = FVector4(H, V, 1.0f / H, 1.0f / V);
		}
		else
		{
			Data.Constants.SubImageSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		FRenderPacket Packet;
		Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
		Packet.VertexBuffer = QuadVertexBuffer.get();
		Packet.IndexBuffer = QuadIndexBuffer.get();
		Packet.StartIndex = 0;
		Packet.IndexCount = 6;
		Packet.InstanceBuffer = Data.InstanceBuffer.get();
		Packet.InstanceCount = static_cast<uint32>(Count);
		Packet.Material = EmitterMaterial;
		Packet.MaterialParamData = &Data.Constants;
		Packet.MaterialParamDataSize = sizeof(FParticleSpriteConstants);
		// 큐는 거리가 큰 패킷부터 그린다. 같은 PSC의 에미터는 배열 순서대로 그리도록 앞 인덱스를 더 멀게
		const float OrderBias = 1.0f + static_cast<float>(Instances.Num() - EmitterIndex) * 1e-4f;
		Packet.CameraToParticleDistance = DistanceSq * OrderBias;
		Queue.Add(Packet);
	}
}