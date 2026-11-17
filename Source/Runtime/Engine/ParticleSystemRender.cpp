#include "EnginePCH.h"
#include "ParticleSystemRender.h"
#include "Particles/ParticleTypes.h"
#include "Particles/ParticleHelper.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSubUV.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Math/Transform.h"

#include <algorithm>

template <typename FuncType>
void FDynamicEmitterReplayDataBase::ForEachParticle(FuncType&& Func) const
{
	const uint8* const Data = ParticleData.GetData();
	for (int32 i = ActiveParticleCount - 1; i >= 0; --i)
	{
		const uint8* ParticleBase = Data + ParticleIndices[i] * ParticleStride;
		const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(ParticleBase);
		if (Particle.Flags & STATE_Particle_Freeze)
			continue;
		Func(Particle, ParticleBase);
	}
}

float FDynamicEmitterDataBase::GetPacketDistance(const FParticleSubmitContext& Context)
{
	const float OrderBias = 1.0f + static_cast<float>(Context.EmitterCount - Context.EmitterIndex) * 1e-4f;
	return Context.BoundsDistanceSq * OrderBias;
}

bool FDynamicEmitterDataBase::EnsureInstanceBuffer(TUniquePtr<FVertexBuffer>& Buffer, int32& Capacity,
	const FDynamicEmitterReplayDataBase& Source, uint32 Stride)
{
	if (!Buffer || Capacity < Source.ActiveParticleCount)
	{
		Capacity = std::max(Source.MaxActiveParticles, Source.ActiveParticleCount);
		Buffer = RenderCommand::CreateDynamicVertexBuffer(Stride * Capacity, Stride);
	}
	return Buffer != nullptr;
}

void FDynamicEmitterDataBase::SortParticles(const FDynamicEmitterReplayDataBase& Source, const FViewContext& View,
	TArray<FParticleSortEntry>& SortScratch)
{
	SortScratch.SetNum(0, false);
	const FVector CameraPos = View.CameraPosition;
	const FVector Forward = View.CameraForward.Normalized();
	const EParticleSortMode SortMode = Source.SortMode;

	Source.ForEachParticle([&](const FBaseParticle& Particle, const uint8* ParticleBase)
		{
			float Key = 0.0f;
			switch (SortMode)
			{
			case EParticleSortMode::ViewProjDepth:
				Key = (Source.SimulationToWorld(Particle.Location) - CameraPos).Dot(Forward);
				break;
			case EParticleSortMode::DistanceToView:
			{
				const FVector D = Source.SimulationToWorld(Particle.Location) - CameraPos;
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
}

int32 FDynamicSpriteEmitterData::BuildInstances(const FViewContext& View)
{
	SortParticles(Source, View, SortScratch);

	const int32 Count = SortScratch.Num();
	InstanceScratch.SetNum(Count, false);
	const int32 SubUVOffset = Source.SubUVDataOffset;
	// 쿼드는 카메라 축으로 펼쳐져 월드 행렬의 스케일을 받지 못하므로 Local/World 모두 직접 곱한다
	const FVector Scale = Source.Scale;

	for (int32 i = 0; i < Count; ++i)
	{
		const FBaseParticle& Particle = *SortScratch[i].Particle;
		FParticleSpriteInstance& Out = InstanceScratch[i];
		Out.Position = Source.SimulationToWorld(Particle.Location);
		Out.Rotation = Particle.Rotation;
		Out.Size = FVector2(std::abs(Particle.Size.X * Scale.X), std::abs(Particle.Size.Y * Scale.Y));
		Out.SubImageIndex = SubUVOffset > 0
			? ParticleHelper::GetPayload<FFullSubUVPayload>(SortScratch[i].ParticleBase, SubUVOffset).ImageIndex
			: 0.0f;
		Out.Color = Particle.Color;
	}
	return Count;
}

void FDynamicSpriteEmitterData::Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context)
{
	if (!Context.View || !Context.SpriteQuadVertexBuffer || !Context.SpriteQuadIndexBuffer)
		return;
	UMaterial* EmitterMaterial = Source.Material ? Source.Material : Context.DefaultMaterial;
	if (!EmitterMaterial)
		return;
	if (!EnsureInstanceBuffer(InstanceBuffer, Capacity, Source, sizeof(FParticleSpriteInstance)))
		return;

	const int32 Count = BuildInstances(*Context.View);
	if (Count == 0)
		return;

	RenderCommand::UpdateBufferData(InstanceBuffer.get(),
		InstanceScratch.GetData(), sizeof(FParticleSpriteInstance) * Count);

	Constants.CameraRight = FVector4(Context.CameraRight.X, Context.CameraRight.Y, Context.CameraRight.Z, 0.0f);
	Constants.CameraUp = FVector4(Context.CameraUp.X, Context.CameraUp.Y, Context.CameraUp.Z, 0.0f);
	if (Source.SubUVDataOffset > 0)
	{
		const float H = static_cast<float>(Source.SubImagesHorizontal);
		const float V = static_cast<float>(Source.SubImagesVertical);
		Constants.SubImageSize = FVector4(H, V, 1.0f / H, 1.0f / V);
	}
	else
	{
		Constants.SubImageSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	VertexFactory.Init(Context.SpriteQuadVertexBuffer, Context.SpriteQuadIndexBuffer, InstanceBuffer.get());

	FRenderPacket Packet;
	Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
	Packet.VertexFactory = &VertexFactory;
	Packet.StartIndex = 0;
	Packet.IndexCount = 6;
	Packet.InstanceCount = static_cast<uint32>(Count);
	Packet.Material = EmitterMaterial;
	Packet.MaterialParamData = &Constants;
	Packet.MaterialParamDataSize = sizeof(FParticleSpriteConstants);
	Packet.CameraToParticleDistance = GetPacketDistance(Context);
	Queue.Add(Packet);
}

namespace
{
	// Payload 오일러 각(도)은 X = Roll, Y = Pitch, Z = Yaw. FRotator 생성자는 (Pitch, Yaw, Roll) 순서
	FRotator EulerToRotator(const FVector& Euler)
	{
		return FRotator(Euler.Y, Euler.Z, Euler.X);
	}
}

int32 FDynamicMeshEmitterData::BuildInstances(const FViewContext& View)
{
	// 한 번의 인스턴스 드로우 안에서도 인스턴스 순서대로 합성되므로 버퍼 순서가 곧 그리는 순서다
	SortParticles(Source, View, SortScratch);

	const bool bLocalSpace = Source.bUseLocalSpace;
	const FMatrix& ComponentToWorld = Source.LocalToWorld;
	// Local은 ComponentToWorld에 스케일이 들어 있다. World는 위치에는 곱하지 않고 크기에만 곱한다
	const FVector WorldScale = Source.Scale;
	const int32 PayloadOffset = Source.MeshRotationPayloadOffset;
	constexpr float RadToDeg = 180.0f / PI;

	const int32 Count = SortScratch.Num();
	InstanceScratch.SetNum(Count, false);

	for (int32 i = 0; i < Count; ++i)
	{
		const FBaseParticle& Particle = *SortScratch[i].Particle;
		const FMeshRotationPayloadData& Payload = ParticleHelper::GetPayload<FMeshRotationPayloadData>(SortScratch[i].ParticleBase, PayloadOffset);
		FParticleMeshInstance* Out = &InstanceScratch[i];

		// 회전 속도로 누적된 회전 + 스프라이트 회전(라디안, 세 축 공통)은 오일러 각끼리 더한다
		const FVector SpinEuler = Payload.Rotation + FVector(Particle.Rotation * RadToDeg);

		// row-vector: Scale × Rotation × Translation (액터 Transform과 같은 규칙)
		const FVector Size = bLocalSpace ? Particle.Size
			: FVector(Particle.Size.X * WorldScale.X, Particle.Size.Y * WorldScale.Y, Particle.Size.Z * WorldScale.Z);
		FMatrix M = FTransform(EulerToRotator(SpinEuler), Particle.Location, Size).GetLocalMatrix();

		// 초기 방향은 각도에 더하지 않고 별도 회전으로 맨 앞에 곱한다: (Orient × Scale) × Rot × Trans
		// 오일러 각 덧셈은 한 축으로만 돌 때만 회전 합성과 같다
		if (Payload.InitialOrientation.X != 0.0f || Payload.InitialOrientation.Y != 0.0f || Payload.InitialOrientation.Z != 0.0f)
		{
			const FMatrix Orient = EulerToRotator(Payload.InitialOrientation).Quaternion().ToFMatrix();
			M = Orient * M;
		}
		if (bLocalSpace)
		{
			M = M * ComponentToWorld;
		}

		// 셰이더가 열과 내적하므로 0~2열을 위에서 아래로 담는다 (행을 넣으면 전치가 된다)
		Out->Transform0 = FVector4(M.M[0][0], M.M[1][0], M.M[2][0], M.M[3][0]);
		Out->Transform1 = FVector4(M.M[0][1], M.M[1][1], M.M[2][1], M.M[3][1]);
		Out->Transform2 = FVector4(M.M[0][2], M.M[1][2], M.M[2][2], M.M[3][2]);
		Out->Color = Particle.Color;
	}
	return Count;
}

namespace
{
	// 메시 파티클 정점 변형이 있는 머티리얼만 오버라이드로 쓸 수 있다
	bool IsMeshParticleMaterial(const UMaterial* Material)
	{
		return Material && (Material->PSOType == EPSOType::StaticMesh_Opaque
			|| Material->PSOType == EPSOType::StaticMesh_Translucent
			|| Material->PSOType == EPSOType::StaticMesh_Additive);
	}
}

void FDynamicMeshEmitterData::Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context)
{
	if (!Context.View)
		return;

	UStaticMesh* Mesh = Source.Mesh;
	if (!Mesh || !Mesh->GetVertexBuffer() || !Mesh->GetIndexBuffer())
		return;
	if (!EnsureInstanceBuffer(InstanceBuffer, Capacity, Source, sizeof(FParticleMeshInstance)))
		return;

	const int32 Count = BuildInstances(*Context.View);
	if (Count == 0)
		return;

	RenderCommand::UpdateBufferData(InstanceBuffer.get(),
		InstanceScratch.GetData(), sizeof(FParticleMeshInstance) * Count);

	VertexFactory.Init(Mesh->GetVertexBuffer(), Mesh->GetIndexBuffer(), InstanceBuffer.get());

	// 스프라이트용 머티리얼처럼 메시 정점을 못 받는 머티리얼이면 섹션 머티리얼로 되돌린다
	UMaterial* OverrideMaterial = Source.bOverrideMaterial && IsMeshParticleMaterial(Source.Material) ? Source.Material : nullptr;

	// 섹션마다 메시 머티리얼로 그린다. 패스 셰이더는 MeshParticle 정점 타입 변형으로 교체된다
	for (const FStaticMeshSection& Section : Mesh->GetMeshData().Sections)
	{
		UMaterial* SectionMaterial = OverrideMaterial ? OverrideMaterial : Mesh->GetMaterial(Section.MaterialSlotIndex);
		if (!SectionMaterial || Section.IndexCount == 0)
			continue;

		FRenderPacket Packet;
		// 정점 타입이 World를 쓰지 않지만 오브젝트 슬롯 상수를 채우는 데 필요하다
		Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
		Packet.VertexFactory = &VertexFactory;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.InstanceCount = static_cast<uint32>(Count);
		Packet.Material = SectionMaterial;
		Packet.CameraToParticleDistance = GetPacketDistance(Context);
		Queue.Add(Packet);
	}
}
