#pragma once

#include "PrimitiveSceneProxy.h"
#include "Render/Buffer.h"

class UParticleSystemComponent;
class UMaterial;
struct FParticleEmitterInstance;

struct FParticleSpriteVertex
{
	FVector Position;
	FVector2 TexCoord;
	FLinearColor Color;
	FVector2 TexCoord2;   // Blend 상대 칸의 UV
	float BlendAlpha;     // 0이면 TexCoord만 사용
};
static_assert(sizeof(FParticleSpriteVertex) == 48, "ParticleSpriteShader VS_INPUT과 맞아야 한다");

// 에미터 하나의 렌더 리소스. 메시 에미터가 생기면 종류별로 나눈다
struct FParticleSpriteRenderData
{
	TUniquePtr<FVertexBuffer> VertexBuffer;
	int32 Capacity = 0;   // 파티클 수
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
	// 정점 수를 반환
	int32 BuildSpriteVertices(FParticleEmitterInstance& Instance, const FViewContext& View);

	TArray<FParticleSpriteRenderData> EmitterRenderData;   // EmitterInstances와 같은 인덱스
	TUniquePtr<FIndexBuffer> IndexBuffer;                  // 모든 에미터가 공유
	int32 IndexCapacity = 0;
	TArray<FParticleSpriteVertex> VertexScratch;
	UMaterial* Material = nullptr;
};