#pragma once

#include "PrimitiveSceneProxy.h"
#include "ParticleSystemRender.h"

class UParticleSystemComponent;
class UMaterial;

class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }
	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;

	// 컴포넌트가 틱 끝에 에미터 인스턴스를 시켜 채운다. 인덱스는 EmitterInstances와 같다
	TArray<TUniquePtr<FDynamicEmitterDataBase>>& GetDynamicEmitterData() { return EmitterRenderData; }

	const TArray<TUniquePtr<FDynamicEmitterDataBase>>& GetDynamicEmitterData() const { return EmitterRenderData; }

	double GetBuildRenderDataMs() const { return BuildRenderDataMs; }

private:
	bool EnsureSpriteQuad();

	TArray<TUniquePtr<FDynamicEmitterDataBase>> EmitterRenderData;
	double BuildRenderDataMs = 0.0;

	// 모든 스프라이트 에미터가 공유하는 쿼드
	TUniquePtr<FVertexBuffer> SpriteQuadVertexBuffer;
	TUniquePtr<FIndexBuffer> SpriteQuadIndexBuffer;

	UMaterial* DefaultMaterial = nullptr;
};
