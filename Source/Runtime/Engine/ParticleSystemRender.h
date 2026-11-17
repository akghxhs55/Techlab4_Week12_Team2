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

	// 시뮬레이션의 ParticleHelper::ForEachParticle과 같은 순서로 Freeze를 건너뛴다
	template <typename FuncType>
	void ForEachParticle(FuncType&& Func) const;
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

struct FParticleSpriteConstants
{
	FVector4 CameraRight;
	FVector4 CameraUp;
	FVector4 SubImageSize;
};
static_assert(sizeof(FParticleSpriteConstants) % 16 == 0, "cbuffer 16바이트 정렬");

struct FParticleSortEntry
{
	float Key;
	const FBaseParticle* Particle;
	const uint8* ParticleBase;
};

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
	int32 BuildInstances();

	TUniquePtr<FVertexBuffer> InstanceBuffer;
	int32 Capacity = 0;
	FMeshParticleVertexFactory VertexFactory;
	TArray<FParticleMeshInstance> InstanceScratch;
};
