#pragma once

#include "PrimitiveSceneProxy.h"
#include "ParticleSystemRender.h"

class UParticleSystemComponent;
class UMaterial;
struct FParticleEmitterInstance;

class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }
	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;

private:
	bool EnsureSpriteQuad();
	static EDynamicEmitterType GetEmitterType(const FParticleEmitterInstance& Instance);
	static TUniquePtr<FDynamicEmitterDataBase> CreateEmitterData(EDynamicEmitterType Type);

	// EmitterInstances와 같은 인덱스
	TArray<TUniquePtr<FDynamicEmitterDataBase>> EmitterRenderData;

	// 모든 스프라이트 에미터가 공유하는 쿼드
	TUniquePtr<FVertexBuffer> SpriteQuadVertexBuffer;
	TUniquePtr<FIndexBuffer> SpriteQuadIndexBuffer;

	UMaterial* DefaultMaterial = nullptr;
};
