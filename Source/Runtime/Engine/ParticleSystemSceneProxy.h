#pragma once

#include "PrimitiveSceneProxy.h"
#include "Render/Buffer.h"

class UParticleSystemComponent;
class UMaterial;
struct FParticleEmitterInstance;
struct FBaseParticle;
enum class EParticleSortMode : uint8;

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

struct FParticleSpriteRenderData
{
	TUniquePtr<FVertexBuffer> InstanceBuffer;
	int32 Capacity = 0;
	// 패킷이 이 주소를 가리키므로 배치가 복사할 때까지 살아 있어야 한다
	FParticleSpriteConstants Constants;
};

struct FParticleSortEntry
{
	float Key;
	const FBaseParticle* Particle;
	const uint8* ParticleBase;
};

class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }
	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;

private:
	bool EnsureBuffers(int32 EmitterIndex, const FParticleEmitterInstance& Instance);
	int32 BuildSpriteInstances(FParticleEmitterInstance& Instance, const FViewContext& View, EParticleSortMode SortMode);

	TArray<FParticleSortEntry> SortScratch;
	UMaterial* DefaultMaterial = nullptr;

	TArray<FParticleSpriteRenderData> EmitterRenderData;
	TUniquePtr<FVertexBuffer> QuadVertexBuffer;
	TUniquePtr<FIndexBuffer> QuadIndexBuffer;
	TArray<FParticleSpriteInstance> InstanceScratch;
};