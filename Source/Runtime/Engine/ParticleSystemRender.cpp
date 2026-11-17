#include "EnginePCH.h"
#include "ParticleSystemRender.h"
#include "Particles/ParticleTypes.h"
#include "Particles/ParticleHelper.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSubUV.h"
#include "Particles/Module/ParticleModuleBeamNoise.h"
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

void AppendTrailIndices(TArray<uint32>& OutIndices, uint32 BaseVertex, uint32 NumSegments)
{
	for (uint32 i = 0; i < NumSegments; ++i)
	{
		const uint32 b = BaseVertex + i * 2;   // 이번 칸의 왼쪽 아래 점
		OutIndices.Add(b);     OutIndices.Add(b + 1); OutIndices.Add(b + 2);   // 왼쪽 삼각형
		OutIndices.Add(b + 2); OutIndices.Add(b + 1); OutIndices.Add(b + 3);   // 오른쪽 삼각형
	}
}

void AppendTrailStrip(const TArray<FVector>& Points, const FTrailStripParams& Params,
	TArray<FParticleTrailVertex>& OutVertices, TArray<uint32>& OutIndices)
{
	const int32 NumPoints = Points.Num();
	if (NumPoints < 2 || Params.Sheets < 1)
		return;

	const float LastIndex = static_cast<float>(NumPoints - 1);
	for (int32 Sheet = 0; Sheet < Params.Sheets; ++Sheet)
	{
		// 판은 180° 돌리면 같은 모양이라 PI를 나눈다
		const float Angle = PI / static_cast<float>(Params.Sheets) * static_cast<float>(Sheet);
		const float CosA = std::cos(Angle);
		const float SinA = std::sin(Angle);

		const uint32 BaseVertex = static_cast<uint32>(OutVertices.Num());
		for (int32 i = 0; i < NumPoints; ++i)
		{
			// 휘어진 경로는 점마다 진행 방향이 달라서 앞뒤 점으로 접선을 구한다
			FVector Tangent = Points[std::min(i + 1, NumPoints - 1)] - Points[std::max(i - 1, 0)];
			if (Tangent.Length() < 1e-4f)
				Tangent = FVector(1.0f, 0.0f, 0.0f);
			Tangent = Tangent.Normalized();

			// 접선과 시선 둘 다에 수직이면 판이 카메라를 향한다
			FVector Right0 = FVector::Cross(Tangent, Points[i] - Params.CameraPosition);
			if (Right0.Length() < 1e-4f) Right0 = FVector::Cross(Tangent, FVector(0.0f, 0.0f, 1.0f));   // 경로를 정면으로 볼 때
			if (Right0.Length() < 1e-4f) Right0 = FVector::Cross(Tangent, FVector(0.0f, 1.0f, 0.0f));   // 경로가 Z축과 평행할 때
			Right0 = Right0.Normalized();
			const FVector Up0 = FVector::Cross(Tangent, Right0);

			const FVector Offset = (Right0 * CosA + Up0 * SinA) * Params.HalfWidth;
			const float U = static_cast<float>(i) / LastIndex * Params.TextureTile;
			OutVertices.Add({ Points[i] - Offset, FVector2(U, 1.0f), Params.Color });   // 짝수 = 아래
			OutVertices.Add({ Points[i] + Offset, FVector2(U, 0.0f), Params.Color });   // 홀수 = 위
		}
		AppendTrailIndices(OutIndices, BaseVertex, static_cast<uint32>(NumPoints - 1));
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

bool FDynamicBeamEmitterData::EnsureBuffers(int32 NumVertices, int32 NumIndices)
{
	if (!VertexBuffer || VertexCapacity < NumVertices)
	{
		VertexCapacity = std::max(NumVertices, VertexCapacity * 2);
		VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
			sizeof(FParticleTrailVertex) * VertexCapacity, sizeof(FParticleTrailVertex));
	}
	if (!IndexBuffer || IndexCapacity < NumIndices)
	{
		IndexCapacity = std::max(NumIndices, IndexCapacity * 2);
		IndexBuffer = RenderCommand::CreateDynamicIndexBuffer(IndexCapacity);
	}
	return VertexBuffer && IndexBuffer;
}

void FDynamicBeamEmitterData::BuildNoisePath(const FVector& S, const FVector& T, const uint8* ParticleBase)
{
	const int32 F = Source.NoiseFrequency;
	if (F < 1)
		return;
	const FVector Axis = T - S;
	const FVector Dir = Axis.Normalized();

	// 노이즈 오프셋을 펼칠 축. 카메라와 무관해야 카메라를 돌려도 번개 모양이 그대로다
	FVector Right = FVector::Cross(Dir, FVector(0.0f, 0.0f, 1.0f));
	if (Right.Length() < 1e-4f) Right = FVector::Cross(Dir, FVector(0.0f, 1.0f, 0.0f));   // 빔이 Z축과 평행할 때
	Right = Right.Normalized();
	const FVector Up = FVector::Cross(Dir, Right);

	const FBeamNoisePoint* Points = UParticleModuleBeamNoise::GetPoints(ParticleBase, Source.NoiseOffset);
	NoiseScratch.SetNum(0, false);
	for (int32 j = 0; j <= F; ++j)
	{
		const FVector Base = S + Axis * (static_cast<float>(j) / static_cast<float>(F));
		NoiseScratch.Add(Base + Right * Points[j].Current.X + Up * Points[j].Current.Y);
	}

	if (!Source.bNoiseSmooth)
	{
		for (const FVector& P : NoiseScratch)
			PathScratch.Add(P);
		return;
	}

	// Catmull-Rom: 이웃 점으로 접선을 만든 Hermite. 노이즈 점을 반드시 지난다
	const int32 Tess = Source.NoiseTessellation;
	for (int32 j = 0; j < F; ++j)
	{
		const FVector& P0 = NoiseScratch[std::max(j - 1, 0)];
		const FVector& P1 = NoiseScratch[j];
		const FVector& P2 = NoiseScratch[j + 1];
		const FVector& P3 = NoiseScratch[std::min(j + 2, F)];
		const FVector T1 = (P2 - P0) * 0.5f;
		const FVector T2 = (P3 - P1) * 0.5f;
		for (int32 k = 0; k < Tess; ++k)
		{
			const float Alpha = static_cast<float>(k) / static_cast<float>(Tess);
			PathScratch.Add(FMath::CubicInterp(P1, T1, P2, T2, Alpha));
		}
	}
	PathScratch.Add(NoiseScratch[F]);
}

int32 FDynamicBeamEmitterData::BuildGeometry(const FViewContext& View)
{
	VertexScratch.SetNum(0, false);
	IndexScratch.SetNum(0, false);

	// 반투명이라 먼 빔부터 쌓는다
	SortParticles(Source, View, SortScratch);

	const int32 Segments = Source.Segments;

	FTrailStripParams Params;
	Params.CameraPosition = View.CameraPosition;
	Params.Sheets = Source.Sheets;
	Params.TextureTile = Source.TextureTile;

	for (const FParticleSortEntry& Entry : SortScratch)
	{
		const FBaseParticle& Particle = *Entry.Particle;
		const FBeamPayloadData& Payload = ParticleHelper::GetPayload<FBeamPayloadData>(Entry.ParticleBase, Source.BeamPayloadOffset);

		const FVector S = Payload.SourcePoint;
		const FVector Axis = Payload.TargetPoint - S;
		if (Axis.Length() < 1e-4f)
			continue;

		PathScratch.SetNum(0, false);
		if (Source.NoiseOffset > 0)
		{
			BuildNoisePath(S, Payload.TargetPoint, Entry.ParticleBase);
		}
		else
		{
			for (int32 i = 0; i <= Segments; ++i)
			{
				const float T = static_cast<float>(i) / static_cast<float>(Segments);
				PathScratch.Add(S + Axis * T);
			}
		}

		Params.HalfWidth = std::abs(Particle.Size.X * Source.Scale.X) * 0.5f;
		Params.Color = Particle.Color;
		AppendTrailStrip(PathScratch, Params, VertexScratch, IndexScratch);
	}
	return IndexScratch.Num();
}

void FDynamicBeamEmitterData::Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context)
{
	if (!Context.View || Source.ActiveParticleCount == 0)
		return;
	UMaterial* EmitterMaterial = Source.Material ? Source.Material : Context.DefaultMaterial;
	if (!EmitterMaterial)
		return;

	const int32 IndexCount = BuildGeometry(*Context.View);
	if (IndexCount == 0)
		return;
	if (!EnsureBuffers(VertexScratch.Num(), IndexCount))
		return;

	RenderCommand::UpdateBufferData(VertexBuffer.get(), VertexScratch.GetData(),
		static_cast<uint32>(sizeof(FParticleTrailVertex) * VertexScratch.Num()));
	RenderCommand::UpdateBufferData(IndexBuffer.get(), IndexScratch.GetData(),
		static_cast<uint32>(sizeof(uint32) * IndexCount));

	VertexFactory.Init(VertexBuffer.get(), IndexBuffer.get());

	FRenderPacket Packet;
	// 정점이 월드 좌표라 Trail 셰이더는 World를 쓰지 않지만 오브젝트 슬롯 상수를 채우는 데 필요하다
	Packet.Model = Queue.StoreWorldMatrix(FMatrix::Identity);
	Packet.VertexFactory = &VertexFactory;
	Packet.StartIndex = 0;
	// 0이면 버퍼 전체를 그리므로 이번 프레임 개수를 넣는다
	Packet.IndexCount = static_cast<uint32>(IndexCount);
	Packet.InstanceCount = 0;
	Packet.Material = EmitterMaterial;
	Packet.CameraToParticleDistance = GetPacketDistance(Context);
	Queue.Add(Packet);
}