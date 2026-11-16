#include "EnginePCH.h"
#include "ParticleSystemRender.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSubUV.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Particles/Module/ParticleModuleTypeDataMesh.h"
#include "Components/ParticleSystemComponent.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Math/Transform.h"

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

bool FDynamicMeshEmitterData::EnsureInstanceBuffer(const FParticleEmitterInstance& Instance)
{
	if (!InstanceBuffer || Capacity < Instance.ActiveParticles)
	{
		Capacity = Instance.MaxActiveParticles;
		InstanceBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleMeshInstance) * Capacity, sizeof(FParticleMeshInstance));
	}
	return InstanceBuffer != nullptr;
}

int32 FDynamicMeshEmitterData::BuildInstances(FParticleEmitterInstance& Instance)
{
	FParticleMeshEmitterInstance& MeshInstance = static_cast<FParticleMeshEmitterInstance&>(Instance);
	const bool bLocalSpace = Instance.IsLocalSpace() && Instance.Component;
	const FMatrix ComponentToWorld = bLocalSpace ? Instance.Component->GetWorldMatrix() : FMatrix::Identity;
	constexpr float RadToDeg = 180.0f / PI;

	InstanceScratch.SetNum(Instance.ActiveParticles, false);
	FParticleMeshInstance* Out = InstanceScratch.GetData();

	ParticleHelper::ForEachParticle(Instance, [&](FBaseParticle& Particle, uint8*)
		{
			// 오일러 각(도). Payload는 X = Roll, Y = Pitch, Z = Yaw. 스프라이트 회전(라디안)은 세 축에 공통으로 더한다
			const FMeshRotationPayloadData& Payload = MeshInstance.GetMeshRotationPayload(static_cast<const FBaseParticle&>(Particle));
			const FVector Euler = Payload.InitialOrientation + Payload.Rotation + FVector(Particle.Rotation * RadToDeg);
			const FRotator Rotation(Euler.Y, Euler.Z, Euler.X);

			// row-vector: Scale × Rotation × Translation (액터 Transform과 같은 규칙)
			FMatrix M = FTransform(Rotation, Particle.Location, Particle.Size).GetLocalMatrix();
			if (bLocalSpace)
			{
				M = M * ComponentToWorld;
			}

			// 셰이더가 열과 내적하므로 0~2열을 위에서 아래로 담는다 (행을 넣으면 전치가 된다)
			Out->Transform0 = FVector4(M.M[0][0], M.M[1][0], M.M[2][0], M.M[3][0]);
			Out->Transform1 = FVector4(M.M[0][1], M.M[1][1], M.M[2][1], M.M[3][1]);
			Out->Transform2 = FVector4(M.M[0][2], M.M[1][2], M.M[2][2], M.M[3][2]);
			Out->Color = Particle.Color;
			++Out;
		});

	// Freeze된 파티클은 건너뛰므로 실제로 채운 수
	return static_cast<int32>(Out - InstanceScratch.GetData());
}

void FDynamicMeshEmitterData::Submit(FRenderQueue& Queue, FParticleEmitterInstance& Instance, const FParticleSubmitContext& Context)
{
	if (!Context.View)
		return;

	const FParticleMeshEmitterInstance& MeshInstance = static_cast<const FParticleMeshEmitterInstance&>(Instance);
	const UParticleModuleTypeDataMesh* TypeData = MeshInstance.GetMeshTypeData();
	UStaticMesh* Mesh = TypeData ? TypeData->GetMesh() : nullptr;
	if (!Mesh || !Mesh->GetVertexBuffer() || !Mesh->GetIndexBuffer())
		return;
	if (!EnsureInstanceBuffer(Instance))
		return;

	const int32 Count = BuildInstances(Instance);
	if (Count == 0)
		return;

	RenderCommand::UpdateBufferData(InstanceBuffer.get(),
		InstanceScratch.GetData(), sizeof(FParticleMeshInstance) * Count);

	// 섹션마다 메시 머티리얼로 그린다. 머티리얼 PSO가 불투명이면 Forward 패스에서 #PARTICLE_INSTANCE 변형으로 교체된다
	for (const FStaticMeshSection& Section : Mesh->GetMeshData().Sections)
	{
		UMaterial* SectionMaterial = Mesh->GetMaterial(Section.MaterialSlotIndex);
		if (!SectionMaterial || Section.IndexCount == 0)
			continue;

		FRenderPacket Packet;
		// 셰이더 변형은 World를 쓰지 않지만 오브젝트 슬롯 상수를 채우는 데 필요하다
		Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
		Packet.VertexBuffer = Mesh->GetVertexBuffer();
		Packet.IndexBuffer = Mesh->GetIndexBuffer();
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.InstanceBuffer = InstanceBuffer.get();
		Packet.InstanceCount = static_cast<uint32>(Count);
		Packet.Material = SectionMaterial;
		Packet.CameraToParticleDistance = GetPacketDistance(Context);
		Queue.Add(Packet);
	}
}
