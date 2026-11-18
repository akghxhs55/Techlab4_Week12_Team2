#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Buffer.h"
#include "Render/RenderPacket.h"

class UMaterial;
class UStaticMesh;
struct FBaseParticle;
enum class EParticleSortMode : uint8;

// 프록시가 뷰마다 한 번 계산해서 모든 에미터에 넘기는 값
struct FParticleSubmitContext
{
	const FViewContext* View = nullptr;
	FVector CameraRight;
	FVector CameraUp;
	float BoundsDistanceSq = 0.0f;   // 프록시 바운드 중심까지 거리
	int32 EmitterIndex = 0;
	int32 EmitterCount = 0;
	UMaterial* DefaultMaterial = nullptr;
	FVertexBuffer* SpriteQuadVertexBuffer = nullptr;
	FIndexBuffer* SpriteQuadIndexBuffer = nullptr;
};

enum class EDynamicEmitterType : uint8
{
	Sprite,
	Mesh,
	Beam,
	Ribbon,
};

// 틱이 끝날 때 에미터가 채우는 렌더용 사본. 렌더 쪽은 이것만 보고 FParticleEmitterInstance는 모른다
struct FDynamicEmitterReplayDataBase
{
	// 파티클 블록 사본. 슬롯 순서 그대로이고 Indices가 살아 있는 슬롯을 가리킨다
	TArray<uint8> ParticleData;
	TArray<uint16> ParticleIndices;
	int32 ActiveParticleCount = 0;
	int32 MaxActiveParticles = 0;
	int32 ParticleStride = 0;

	bool bUseLocalSpace = false;
	FMatrix LocalToWorld = FMatrix::Identity;   // bUseLocalSpace일 때만 쓴다
	FVector Scale = FVector(1.0f);               // 컴포넌트 월드 스케일

	UMaterial* Material = nullptr;
	EParticleSortMode SortMode{};

	FVector SimulationToWorld(const FVector& Position) const
	{
		return bUseLocalSpace ? LocalToWorld.TransformPosition(Position) : Position;
	}

	// 시뮬레이션의 ParticleHelper::ForEachParticle과 같은 순서로 돈다.
	// Freeze는 갱신만 멈추는 상태라 그리기에서는 건너뛰지 않는다 (멈춘 자리에 계속 보여야 한다)
	template <typename FuncType>
	void ForEachParticle(FuncType&& Func) const;
};

struct FDynamicBeamEmitterReplayData : FDynamicEmitterReplayDataBase
{
	int32 BeamPayloadOffset = 0;   // 파티클 메모리에서 FBeamPayloadData 위치
	int32 Sheets = 1;
	int32 Segments = 10;
	float TextureTile = 1.0f;

	int32 NoiseOffset = 0;          // BeamNoise Payload 위치. 0이면 노이즈 없음
	int32 NoiseFrequency = 0;
	bool  bNoiseSmooth = false;
	int32 NoiseTessellation = 1;
};

struct FDynamicRibbonEmitterReplayData : FDynamicEmitterReplayDataBase
{
	int32 RibbonPayloadOffset = 0;   // 파티클 메모리에서 FRibbonPayloadData 위치
	int32 Sheets = 1;
	bool  bTiling = false;           // TilingDistance > 0
	float TessellationStepSize = 0.0f;
	int32 MaxTessellation = 0;

	bool    bHeadValid = false;      // head를 현재 트레일에 이을지
	uint32  HeadTrailId = 0;
	FVector HeadPosition;            // 이번 프레임 소스 위치(월드)
	float   HeadTiledU = 0.0f;
};


struct FDynamicSpriteEmitterReplayData : FDynamicEmitterReplayDataBase
{
	int32 SubUVDataOffset = 0;   // 0이면 SubUV 없음
	int32 SubImagesHorizontal = 1;
	int32 SubImagesVertical = 1;
};

struct FDynamicMeshEmitterReplayData : FDynamicEmitterReplayDataBase
{
	UStaticMesh* Mesh = nullptr;
	int32 MeshRotationPayloadOffset = 0;
	bool bOverrideMaterial = false;
};

struct FParticleSortEntry
{
	float Key;
	const FBaseParticle* Particle;
	const uint8* ParticleBase;
};

// 에미터 하나의 렌더 리소스와 제출. 프록시가 소유하고 에미터 인스턴스가 Source를 갱신한다
class FDynamicEmitterDataBase
{
public:
	virtual ~FDynamicEmitterDataBase() = default;

	virtual EDynamicEmitterType GetType() const = 0;
	virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
	virtual void Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context) = 0;

protected:
	// 큐는 거리가 큰 패킷부터 그린다. 같은 PSC의 에미터는 배열 순서대로 그리도록 앞 인덱스를 더 멀게
	static float GetPacketDistance(const FParticleSubmitContext& Context);
	static bool EnsureInstanceBuffer(TUniquePtr<FVertexBuffer>& Buffer, int32& Capacity,
		const FDynamicEmitterReplayDataBase& Source, uint32 Stride);
	// Source.SortMode에 따라 살아 있는 파티클을 그릴 순서로 Out에 담는다
	static void SortParticles(const FDynamicEmitterReplayDataBase& Source, const FViewContext& View,
		TArray<FParticleSortEntry>& Out);
};

struct FParticleSpriteCorner
{
	FVector2 UV;
};
static_assert(sizeof(FParticleSpriteCorner) == 8, "ParticleSpriteShader 슬롯 0과 맞아야 한다");

struct FParticleSpriteInstance
{
	FVector Position;
	float Rotation;
	FVector2 Size;
	float SubImageIndex;
	FLinearColor Color;
};
static_assert(sizeof(FParticleSpriteInstance) == 44, "ParticleSpriteShader INSTANCE_* 와 맞아야 한다");

struct FParticleTrailVertex
{
	FVector      Position;   // 월드 좌표
	FVector2     UV;
	FLinearColor Color;
};
static_assert(sizeof(FParticleTrailVertex) == 36, "ParticleTrailShader VS_INPUT과 맞아야 한다");

// 위아래 두 줄로 놓인 정점들을 삼각형 인덱스로 만든다
void AppendTrailIndices(TArray<uint32>& OutIndices, uint32 BaseVertex, uint32 NumSegments);

struct FTrailPoint
{
	FVector Position;
	float HalfWidth = 1.0f;
	FLinearColor Color;
	float U = 0.0f;
};

struct FTrailStripParams
{
	FVector CameraPosition;
	FVector CameraUp;
	int32 Sheets = 1;
	float MinPinch = 0.1f;   // 완전히 되돌아가는 꼭짓점의 폭 배율
};

// Points를 따라 시트 수만큼 띠를 만들어 정점과 인덱스를 덧붙인다. 빔, 리본 공용
void AppendTrailStrip(const TArray<FTrailPoint>& Points, const FTrailStripParams& Params,
	TArray<FParticleTrailVertex>& OutVertices, TArray<uint32>& OutIndices);

struct FParticleSpriteConstants
{
	FVector4 CameraRight;
	FVector4 CameraUp;
	FVector4 SubImageSize;
};
static_assert(sizeof(FParticleSpriteConstants) % 16 == 0, "cbuffer 16바이트 정렬");

class FDynamicSpriteEmitterData final : public FDynamicEmitterDataBase
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Sprite; }
	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	void Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context) override;

	FDynamicSpriteEmitterReplayData Source;

private:
	int32 BuildInstances(const FViewContext& View);

	TUniquePtr<FVertexBuffer> InstanceBuffer;
	int32 Capacity = 0;
	// 패킷이 이 주소들을 가리키므로 배치가 복사할 때까지 살아 있어야 한다
	FParticleSpriteConstants Constants;
	FSpriteParticleVertexFactory VertexFactory;

	TArray<FParticleSortEntry> SortScratch;
	TArray<FParticleSpriteInstance> InstanceScratch;
};

class FDynamicMeshEmitterData final : public FDynamicEmitterDataBase
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Mesh; }
	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	void Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context) override;

	FDynamicMeshEmitterReplayData Source;

private:
	int32 BuildInstances(const FViewContext& View);

	TUniquePtr<FVertexBuffer> InstanceBuffer;
	int32 Capacity = 0;
	FMeshParticleVertexFactory VertexFactory;
	TArray<FParticleSortEntry> SortScratch;
	TArray<FParticleMeshInstance> InstanceScratch;
};

class FDynamicBeamEmitterData final : public FDynamicEmitterDataBase
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Beam; }
	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	void Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context) override;

	FDynamicBeamEmitterReplayData Source;

private:
	bool EnsureBuffers(int32 NumVertices, int32 NumIndices);
	// 정렬된 빔마다 시트 수만큼 띠를 만든다. 반환값은 인덱스 수
	int32 BuildGeometry(const FParticleSubmitContext& Context);
	// 노이즈 점을 월드로 펼치고 bSmooth면 곡선으로 채워 PathScratch에 넣는다
	void BuildNoisePath(const FVector& S, const FVector& T, const uint8* ParticleBase);

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer>  IndexBuffer;
	int32 VertexCapacity = 0;
	int32 IndexCapacity = 0;
	FTrailVertexFactory VertexFactory;

	TArray<FParticleSortEntry> SortScratch;
	TArray<FVector> PathScratch;
	TArray<FVector> NoiseScratch;
	TArray<FTrailPoint> PointScratch;
	TArray<FParticleTrailVertex> VertexScratch;
	TArray<uint32> IndexScratch;
};

class FDynamicRibbonEmitterData final : public FDynamicEmitterDataBase
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Ribbon; }
	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	void Submit(FRenderQueue& Queue, const FParticleSubmitContext& Context) override;

	FDynamicRibbonEmitterReplayData Source;

private:
	bool EnsureBuffers(int32 NumVertices, int32 NumIndices);
	int32 BuildGeometry(const FParticleSubmitContext& Context);
	// PointScratch의 점 사이를 곡선으로 채워 SmoothScratch에 넣는다
	void TessellatePoints();

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer>  IndexBuffer;
	int32 VertexCapacity = 0;
	int32 IndexCapacity = 0;
	FTrailVertexFactory VertexFactory;

	struct FRibbonSortEntry
	{
		uint32 TrailId;
		uint32 Counter;
		const uint8* ParticleBase;
	};
	TArray<FRibbonSortEntry> SortScratch;
	TArray<FTrailPoint> PointScratch;
	TArray<FTrailPoint> SmoothScratch;
	TArray<FParticleTrailVertex> VertexScratch;
	TArray<uint32> IndexScratch;
};