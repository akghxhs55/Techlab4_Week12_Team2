#include "EnginePCH.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Components/SkinnedMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Render/SceneView.h"

// 스텐실 마스크·외곽선 두 패스의 Shader와 상수 버퍼를 준비한다.
// 두 패스 모두 깊이 테스트를 끄므로 다른 물체에 가려져도 외곽선이 보인다.
void FOutlineRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/OutlineShader.hlsl");

	// 1패스: 원본 크기 메시 영역을 색 없이 스텐실에만 기록한다.
	MaskPipelineState.Shader = Shader;
	MaskPipelineState.Topology = EPrimitiveTopology::TriangleList;
	MaskPipelineState.RasterizerState = ERasterizerState::SolidNone;
	MaskPipelineState.BlendState = EBlendState::NoColorWrite;
	MaskPipelineState.DepthStencilState = EDepthStencilState::StencilMask;

	// 2패스: 확장 메시를 스텐실이 비어 있는 곳에만 그려 테두리 띠만 남긴다.
	OutlinePipelineState.Shader = Shader;
	OutlinePipelineState.Topology = EPrimitiveTopology::TriangleList;
	OutlinePipelineState.RasterizerState = ERasterizerState::SolidNone;
	OutlinePipelineState.BlendState = EBlendState::Opaque;
	OutlinePipelineState.DepthStencilState = EDepthStencilState::StencilOutline;

	static_assert(sizeof(FOutlineData) == 208, "Outline constant buffer layout mismatch");
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FOutlineData));
}

// 외부에서 지정한 Mesh 참조를 저장한다.
void FOutlineRenderer::SetMesh(UStaticMesh* InMesh)
{
	Mesh = InMesh;
}

// 카메라 거리 대신 View별 픽셀 크기와 고정 확장량으로 선택 Outline을 그린다.
void FOutlineRenderer::OnRender(const FOutline& InOutline, const FMatrix& InViewProj, const FViewportSettings& Viewport)
{
	const TArray<UPrimitiveComponent*>& RawTargets = InOutline.GetTargets();
	if (RawTargets.IsEmpty() || Viewport.Width == 0 || Viewport.Height == 0)
	{
		return;
	}

	struct FRenderableOutlineTarget
	{
		const FVertexFactory* VertexFactory = nullptr;
		FMatrix World;
		FMatrix NormalMatrix;
	};

	TArray<FRenderableOutlineTarget> RenderTargets;
	for (UPrimitiveComponent* Target : RawTargets)
	{
		if (!Target)
		{
			continue;
		}

		const FVertexFactory* VertexFactory = nullptr;
		if (UStaticMesh* StaticMesh = Target->GetRenderMesh())
		{
			VertexFactory = StaticMesh->GetVertexFactory();
		}
		else if (USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Target))
		{
			if (FSkeletalMeshObject* MeshObject = Skinned->GetMeshObject())
				VertexFactory = MeshObject->GetVertexFactory(0);
		}

		if (VertexFactory && VertexFactory->IsValid())
		{
			const FMatrix OriginalWorld = Target->GetWorldMatrix();
			RenderTargets.Add({ VertexFactory, OriginalWorld, OriginalWorld.Inverse().GetTransposed() });
		}
	}

	// 메시 없는 컴포넌트(텍스트 등)는 아웃라인을 그리지 않는다
	if (RenderTargets.IsEmpty())
	{
		return;
	}

	const FMatrix& ViewProj = InViewProj;
	const float ViewportWidth = static_cast<float>(Viewport.Width);
	const float ViewportHeight = static_cast<float>(Viewport.Height);
	auto DrawPass = [&](const FPipelineState& BasePipeline, float ExpandPixels)
	{
		for (const FRenderableOutlineTarget& Item : RenderTargets)
		{
			FPipelineState Pipeline = BasePipeline;
			Pipeline.Shader = FRenderResourceManager::ResolveShader(BasePipeline.Shader, Item.VertexFactory->GetType());
			if (!Pipeline.Shader) continue;

			Item.VertexFactory->Bind();
			RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex);

			FOutlineData ConstData = { Item.World, Item.NormalMatrix, ViewProj, FVector4(ViewportWidth, ViewportHeight, ExpandPixels, 0.0f) };
			RenderCommand::BindPipelineState(Pipeline);
			RenderCommand::UpdateBufferData(ConstantBuffer.get(), &ConstData, sizeof(ConstData));
			RenderCommand::DrawIndexed(Item.VertexFactory->GetIndexBuffer()->GetIndexCount());
		}
	};

	// 1패스: 모든 선택 메시 영역을 스텐실에 먼저 찍어 겹치는 선택 외곽선이 내부를 가리지 않게 한다.
	DrawPass(MaskPipelineState, 0.0f);
	// 2패스: 스텐실 바깥 부분만 노란색 외곽선 띠로 그린다.
	DrawPass(OutlinePipelineState, 2.0f);

	// 이후 패스(Gizmo 등)에 스텐실·블렌드 상태가 새지 않도록 기본값으로 되돌린다.
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
}

// 그리는 도구를 만들고 셰이더와 상수 버퍼를 준비한다. FOutlineRenderer::Init의 렌더러 인자는 쓰이지 않는다.
FOutline::FOutline()
{
	Renderer = MakeUnique<FOutlineRenderer>();
	Renderer->Init(nullptr);
}

// FOutlineRenderer가 완전한 타입인 이 파일에서 소멸자를 정의해야 TUniquePtr가 지울 수 있다.
FOutline::~FOutline() = default;

void FOutline::Render(const FSceneView* View, const FViewportSettings& Viewport) const
{
	Renderer->OnRender(*this, View->ViewProjectionMatrix, Viewport);
}
