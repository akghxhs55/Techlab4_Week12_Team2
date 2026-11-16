#include "EnginePCH.h"
#include "ParticleSystemSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"

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

	// Required에 머티리얼이 없는 에미터가 쓰는 기본값
	DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
}

bool FParticleSystemSceneProxy::EnsureSpriteQuad()
{
	if (!SpriteQuadVertexBuffer)
	{
		const FParticleSpriteCorner Corners[4] = {
			{ FVector2(0.0f, 1.0f) },{ FVector2(0.0f, 0.0f) },{ FVector2(1.0f, 0.0f) },{ FVector2(1.0f, 1.0f) } };
		const uint32 Indices[6] = { 0, 1, 2, 0, 2, 3 };
		SpriteQuadVertexBuffer = RenderCommand::CreateStaticVertexBuffer(Corners, sizeof(Corners), sizeof(FParticleSpriteCorner));
		SpriteQuadIndexBuffer = RenderCommand::CreateStaticIndexBuffer(Indices, 6);
	}
	return SpriteQuadVertexBuffer && SpriteQuadIndexBuffer;
}

EDynamicEmitterType FParticleSystemSceneProxy::GetEmitterType(const FParticleEmitterInstance& Instance)
{
	// 인스턴스 클래스는 TypeData가 정하므로 클래스로 렌더 종류를 고른다
	if (dynamic_cast<const FParticleMeshEmitterInstance*>(&Instance))
		return EDynamicEmitterType::Mesh;
	return EDynamicEmitterType::Sprite;
}

TUniquePtr<FDynamicEmitterDataBase> FParticleSystemSceneProxy::CreateEmitterData(EDynamicEmitterType Type)
{
	switch (Type)
	{
	case EDynamicEmitterType::Sprite:
		return MakeUnique<FDynamicSpriteEmitterData>();
	case EDynamicEmitterType::Mesh:
		return MakeUnique<FDynamicMeshEmitterData>();
	}
	return nullptr;
}

void FParticleSystemSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	// 프록시가 머티리얼 등록보다 먼저 만들어졌을 수 있다
	if (!DefaultMaterial)
	{
		DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteAdditive");
	}
	if (!EnsureSpriteQuad())
		return;

	const UParticleSystemComponent* PSC = static_cast<const UParticleSystemComponent*>(GetComponent());
	const TArray<FParticleEmitterInstance*>& Instances = PSC->GetEmitterInstances();
	if (EmitterRenderData.Num() != Instances.Num())
	{
		EmitterRenderData.Empty();
		EmitterRenderData.SetNum(Instances.Num());
	}

	FParticleSubmitContext Context;
	Context.View = &View;

	// FViewContext::BuildBillboardMatrix와 같은 축
	const FVector Forward = View.CameraForward.Normalized();
	Context.CameraRight = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Context.CameraRight.Length() <= 1e-6f)
		Context.CameraRight = FVector(0.0f, 1.0f, 0.0f);
	Context.CameraUp = FVector::Cross(Forward, Context.CameraRight).Normalized();

	const FVector Delta = GetBounds().Center - View.CameraPosition;
	Context.BoundsDistanceSq = Delta.Dot(Delta);
	Context.EmitterCount = Instances.Num();
	Context.DefaultMaterial = DefaultMaterial;
	Context.SpriteQuadVertexBuffer = SpriteQuadVertexBuffer.get();
	Context.SpriteQuadIndexBuffer = SpriteQuadIndexBuffer.get();

	for (int32 EmitterIndex = 0; EmitterIndex < Instances.Num(); ++EmitterIndex)
	{
		FParticleEmitterInstance* Instance = Instances[EmitterIndex];
		if (!Instance || Instance->ActiveParticles == 0)
			continue;

		const EDynamicEmitterType Type = GetEmitterType(*Instance);
		TUniquePtr<FDynamicEmitterDataBase>& Data = EmitterRenderData[EmitterIndex];
		if (!Data || Data->GetType() != Type)
		{
			Data = CreateEmitterData(Type);
			if (!Data)
				continue;
		}

		Context.EmitterIndex = EmitterIndex;
		Data->Submit(Queue, *Instance, Context);
	}
}
