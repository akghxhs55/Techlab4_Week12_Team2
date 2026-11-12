#include "EnginePCH.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "Render/PostProcess/HeightFogRenderer.h"
#include "Render/PostProcess/FXAARenderer.h"
#include "Render/PostProcess/ToneMapData.h"
#include "Render/DecalData.h"

#include "Render/PostProcess/CameraOverlayRenderer.h"
#include "Text/TextRenderer.h"
#include "Render/SkyboxRenderer.h"
#include "Core/Misc/Paths.h"

namespace
{
	constexpr uint32 PerObjectSlotConstants = 16;
	constexpr uint32 MinPerObjectSlots = 1024;
}

FRenderer::FRenderer() = default;
FRenderer::~FRenderer() = default;

bool FRenderer::Init()
{
	bUsePerObjectSlots = RenderCommand::SupportsConstantBufferOffsets();
	ObjectCB = RenderCommand::CreateConstantBuffer(sizeof(FForwardObjectConstants));
	ViewCB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
	MaterialCB = RenderCommand::CreateConstantBuffer(sizeof(FForwardMaterialConstants));
	if (!ObjectCB || !ViewCB || !MaterialCB) return false;
	return true;
}

void FRenderer::UploadViewMatrix(const FMatrix& ViewProjection)
{
	RenderCommand::UpdateBufferData(ViewCB.get(), &ViewProjection, sizeof(ViewProjection));
}

void FRenderer::BindBatch(const FMeshDrawBatch& Batch)
{
	RenderCommand::BindPipelineState(Batch.Pipeline);
	if (Batch.bForceWireframe)
		RenderCommand::SetRasterizerState(ERasterizerState::Wireframe);
	
	RenderCommand::BindVertexBuffer(Batch.VertexBuffer);
	RenderCommand::BindIndexBuffer(Batch.IndexBuffer);
	if (Batch.BoneBuffer)
		RenderCommand::BindShaderResource(BoneMatrixBufferSlot, Batch.BoneBuffer, EShaderBindFlagBits::Vertex);

	if (Batch.bBindViewConstants)
		RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex);
	for (uint32 Index = 0; Index < Batch.Textures.Num(); ++Index)
		RenderCommand::BindShaderResource(Index, Batch.Textures[Index].Texture,
			Batch.Textures[Index].Stages);
	
	RenderCommand::BindSamplerState(0, Batch.Sampler, Batch.SamplerStages);
	
	if (Batch.MaterialDataSize)
	{
		RenderCommand::UpdateBufferData(MaterialCB.get(), Batch.MaterialData.data(), Batch.MaterialDataSize);
		RenderCommand::BindConstantBuffer(Batch.MaterialSlot, MaterialCB.get(), Batch.MaterialStages);
	}
}

void FRenderer::BindObject(const FMeshDrawBatch& Batch, const FMeshDrawBatch::FItem& Item)
{
	if (Batch.bUseObjectSlots && bUsePerObjectSlots && PerObjectSlotCB &&
		Item.ObjectSlot != InvalidObjectSlot)
	{
		RenderCommand::BindConstantBufferRange(Batch.ObjectBindingSlot, PerObjectSlotCB.get(),
			Item.ObjectSlot * PerObjectSlotConstants, PerObjectSlotConstants, Batch.ObjectStages);
	}
	else
	{
		RenderCommand::UpdateBufferData(ObjectCB.get(), Item.ObjectData.data(), Item.ObjectDataSize);
		RenderCommand::BindConstantBuffer(Batch.ObjectBindingSlot, ObjectCB.get(), Batch.ObjectStages);
	}
}

void FRenderer::DrawMeshes(std::span<const FMeshDrawBatch> Batches)
{
	uint32 PreviousTextureCount = 0;
	for (const FMeshDrawBatch& Batch : Batches)
	{
		BindBatch(Batch);
		if (PreviousTextureCount > Batch.Textures.Num())
			RenderCommand::UnbindShaderResources(static_cast<uint32>(Batch.Textures.Num()),
				PreviousTextureCount - static_cast<uint32>(Batch.Textures.Num()),
				EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
		
		PreviousTextureCount = static_cast<uint32>(Batch.Textures.Num());
		for (const FMeshDrawBatch::FItem& Item : Batch.Items)
		{
			BindObject(Batch, Item);
			RenderCommand::DrawIndexed(Item.IndexCount, Item.StartIndex);
		}
	}
}

void FRenderer::EnsurePerObjectSlotCapacity(uint32 SlotCount)
{
	if (SlotCount <= PerObjectSlotCapacity) return;
	uint32 NewCapacity = std::max(PerObjectSlotCapacity * 2, MinPerObjectSlots);
	while (NewCapacity < SlotCount) NewCapacity *= 2;
	PerObjectSlotCB = RenderCommand::CreateConstantBuffer(NewCapacity * ObjectSlotBytes);
	if (!PerObjectSlotCB || !PerObjectSlotCB->GetBuffer())
	{
		HTR_LOG(Warning, "Per-object constant buffer creation failed; using per-draw updates.");
		PerObjectSlotCB.reset();
		PerObjectSlotCapacity = 0;
		return;
	}
	PerObjectSlotCapacity = NewCapacity;
}

uint8* FRenderer::BeginObjectConstants(uint32 MaxSlots)
{
	if (!bUsePerObjectSlots || MaxSlots == 0) return nullptr;
	EnsurePerObjectSlotCapacity(MaxSlots);
	if (!PerObjectSlotCB) { bUsePerObjectSlots = false; return nullptr; }
	uint8* Destination = static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
	if (!Destination) bUsePerObjectSlots = false;
	return Destination;
}

void FRenderer::EndObjectConstants()
{
	if (PerObjectSlotCB) RenderCommand::Unmap(PerObjectSlotCB.get());
}

void FRenderer::InitScenePasses()
{
	// 스카이 배경 파노라마(엔진 콘텐츠). 로드에 실패하면 배경은 검정으로 남는다.
	Skybox = MakeUnique<FSkyboxRenderer>();
	if (!Skybox->Init(FPaths::EngineFileUtf8("EngineResources/SkySphere/Sky.hdr")))
	{
		HTR_LOG(Warning, "Skybox panorama not loaded");
		Skybox.reset();
	}
	HeightFog = MakeUnique<FHeightFogRenderer>();
	HeightFog->Init(this);
	FXAA = MakeUnique<FFXAARenderer>();
	FXAA->Init(this);
	CameraOverlay = MakeUnique<FCameraOverlayRenderer>();   // FXAA->Init(this); 아래
	CameraOverlay->Init(this);
	ToneMapConstants = RenderCommand::CreateConstantBuffer(sizeof(FToneMapData));
	DecalConstants = RenderCommand::CreateConstantBuffer(sizeof(FDecalData));
	SceneText = MakeUnique<FTextRenderer>();
	SceneText->Init();
}

