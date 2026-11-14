#include "EnginePCH.h"
#include "ParticleSystemSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include <cmath>

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

	// TODO: Required 모듈에 Material이 생기면 에미터별로 읽는다
	Material = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
}

bool FParticleSystemSceneProxy::EnsureBuffers(int32 EmitterIndex, const FParticleEmitterInstance& Instance)
{
	FParticleSpriteRenderData& Data = EmitterRenderData[EmitterIndex];
	const int32 Capacity = Instance.MaxActiveParticles;

	if (!Data.VertexBuffer || Data.Capacity < Instance.ActiveParticles)
	{
		Data.VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleSpriteVertex) * 4 * Capacity, sizeof(FParticleSpriteVertex));
		Data.Capacity = Capacity;
	}

	if (!IndexBuffer || IndexCapacity < Instance.ActiveParticles)
	{
		TArray<uint32> Indices;
		Indices.Reserve(Capacity * 6);
		for (uint32 Quad = 0; Quad < static_cast<uint32>(Capacity); ++Quad)
		{
			const uint32 V = Quad * 4;
			Indices.Add(V + 0); Indices.Add(V + 1); Indices.Add(V + 2);
			Indices.Add(V + 0); Indices.Add(V + 2); Indices.Add(V + 3);
		}
		IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Indices.GetData(), Indices.Num());
		IndexCapacity = Capacity;
	}
	return Data.VertexBuffer && IndexBuffer;
}

int32 FParticleSystemSceneProxy::BuildSpriteVertices(FParticleEmitterInstance& Instance, const FViewContext& View)
{
	const FVector Forward = View.CameraForward.Normalized();
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
		Right = FVector(0.0f, 1.0f, 0.0f);
	const FVector Up = FVector::Cross(Forward, Right).Normalized();

	VertexScratch.SetNum(Instance.ActiveParticles * 4, false);
	FParticleSpriteVertex* Out = VertexScratch.GetData();

	ParticleHelper::ForEachParticle(Instance, [&](FBaseParticle& Particle, uint8*)
		{
			const FVector P = Instance.SimulationToWorld(Particle.Location);
			const float C = std::cos(Particle.Rotation);
			const float S = std::sin(Particle.Rotation);
			const FVector R = (Right * C + Up * S) * (Particle.Size.X * 0.5f);
			const FVector U = (Up * C - Right * S) * (Particle.Size.Y * 0.5f);

			Out[0] = { P - R - U, FVector2(0.0f, 1.0f), Particle.Color };
			Out[1] = { P - R + U, FVector2(0.0f, 0.0f), Particle.Color };
			Out[2] = { P + R + U, FVector2(1.0f, 0.0f), Particle.Color };
			Out[3] = { P + R - U, FVector2(1.0f, 1.0f), Particle.Color };
			Out += 4;
		});

	// Freeze된 파티클은 건너뛰므로 실제로 채운 수
	return static_cast<int32>(Out - VertexScratch.GetData());
}

void FParticleSystemSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	// 프록시가 머티리얼 등록보다 먼저 만들어졌을 수 있다
	if (!Material)
	{
		Material = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
		if (!Material)
			return;
	}

	const UParticleSystemComponent* PSC = static_cast<const UParticleSystemComponent*>(GetComponent());
	const TArray<FParticleEmitterInstance*>& Instances = PSC->GetEmitterInstances();

	// 인스턴스가 다시 만들어지면 개수가 바뀐다
	if (EmitterRenderData.Num() != Instances.Num())
	{
		EmitterRenderData.Empty();
		EmitterRenderData.SetNum(Instances.Num());
	}

	const FVector Delta = GetBounds().Center - View.CameraPosition;
	const float DistanceSq = Delta.Dot(Delta);

	for (int32 EmitterIndex = 0; EmitterIndex < Instances.Num(); ++EmitterIndex)
	{
		FParticleEmitterInstance* Instance = Instances[EmitterIndex];
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		if (!EnsureBuffers(EmitterIndex, *Instance))
			continue;

		const int32 VertexCount = BuildSpriteVertices(*Instance, View);
		if (VertexCount == 0)
			continue;

		FParticleSpriteRenderData& Data = EmitterRenderData[EmitterIndex];
		RenderCommand::UpdateBufferData(Data.VertexBuffer.get(),
			VertexScratch.GetData(), sizeof(FParticleSpriteVertex) * VertexCount);

		FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
		// Proxy를 넣으면 LocalToWorld가 World 행렬로 쓰인다. 정점이 이미 월드 좌표라 비워둔다
		Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
		Packet.VertexBuffer = Data.VertexBuffer.get();
		Packet.IndexBuffer = IndexBuffer.get();
		Packet.StartIndex = 0;
		Packet.IndexCount = static_cast<uint32>(VertexCount / 4 * 6);
		Packet.Material = Material;
		Packet.CameraToParticleDistance = DistanceSq;
	}
}