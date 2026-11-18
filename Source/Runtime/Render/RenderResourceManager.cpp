#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"
#include "Core/Misc/Paths.h"
#include <cstring>
#include <fstream>
#include <iterator>



struct FPipelineTableEntry
{
	EPSOType Type;
	const char* ShaderPath;
	EPrimitiveTopology Topology;
	ERasterizerState RasterizerState;
	EBlendState BlendState;
	EDepthStencilState DepthStencilState;
};

constexpr FPipelineTableEntry PipelineTable[] =
{
	{ EPSOType::StaticMesh_Opaque,       nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::StaticMesh_Translucent,  "EngineResources/Shaders/StaticMeshTranslucentShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Wireframe,    nullptr,                                    EPrimitiveTopology::TriangleList, ERasterizerState::Wireframe, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Reserved_5, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::Grid,                    "EngineResources/Shaders/GridShader.hlsl",          EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::Outline_Mask,            "EngineResources/Shaders/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::StencilMask },
	{ EPSOType::Outline_Draw,            "EngineResources/Shaders/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::StencilOutline },
	{ EPSOType::Reserved_9, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::Reserved_10, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::Reserved_11, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::ToneMap,                "EngineResources/Shaders/ToneMapShader.hlsl",              EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Disabled },
	{ EPSOType::Reserved_13,            nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Billboard,              "EngineResources/Shaders/BillboardShader.hlsl",            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Forward,		"EngineResources/Shaders/UberLit.hlsl",			EPrimitiveTopology::TriangleList,			ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_WorldNormal, "EngineResources/Shaders/UberLit.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_DepthOnly, "EngineResources/Shaders/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_LightShadow, "EngineResources/Shaders/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::LightCullingDebug, "EngineResources/Shaders/LightCullingDebug.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::Disabled },
	{ EPSOType::ShadowTileClear, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::AlwaysWrite },
	{ EPSOType::ShadowDepth, "EngineResources/Shaders/ShadowDepth.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::ShadowProjection, "EngineResources/Shaders/ShadowProjection.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::ShadowMaskR, EDepthStencilState::Disabled },
	{ EPSOType::Reserved_23, nullptr, EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::Decal,			"EngineResources/Shaders/DecalShader.hlsl",			EPrimitiveTopology::TriangleList,		ERasterizerState::SolidBack, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::DecalAdditive,	"EngineResources/Shaders/DecalShader.hlsl",			EPrimitiveTopology::TriangleList,		ERasterizerState::SolidBack, EBlendState::Additive, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_AlphaBlend,     "EngineResources/Shaders/ParticleSpriteShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_Additive,       "EngineResources/Shaders/ParticleSpriteShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::ParticleAdditive, EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Additive,     "EngineResources/Shaders/StaticMeshTranslucentShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::ParticleAdditive, EDepthStencilState::ReadOnly },
};

static_assert(sizeof(PipelineTable) / sizeof(PipelineTable[0]) == static_cast<size_t>(EPSOType::Count));

void FRenderResourceManager::InitPipelineStates()
{
	for (size_t i = 0; i < static_cast<size_t>(EPSOType::Count); ++i)
	{
		const auto& Entry = PipelineTable[i];
		const EPSOType Type = static_cast<EPSOType>(i);
		PipelineStateMap[Type] = MakeUnique<FPipelineState>();
		FPipelineState* State = PipelineStateMap[Type].get();
		// World Normal은 전용 매크로 변형이 성공했을 때만 연결한다.
		// 셰이더가 없는 항목은 번호만 남긴 예약 자리다.
		State->Shader = Type == EPSOType::StaticMesh_WorldNormal || !Entry.ShaderPath ? nullptr : GetShaderProgram(Entry.ShaderPath);
		State->Topology = Entry.Topology;
		State->RasterizerState = Entry.RasterizerState;
		State->BlendState = Entry.BlendState;
		State->DepthStencilState = Entry.DepthStencilState;
	}
	const char* Names[] = { "LIGHTING_MODEL_GOURAUD", "LIGHTING_MODEL_LAMBERT", "LIGHTING_MODEL_PHONG", "LIGHTING_MODEL_UNLIT", "VIEW_MODE_WORLD_NORMAL" };
	for (const char* Name : Names)
	{
		const FString Path = "EngineResources/Shaders/UberLit.hlsl";
		const FString Key = Path + "#" + Name;
		CompileVertexFactoryVariants(Path, Key, Name);
		if (std::strcmp(Name, "VIEW_MODE_WORLD_NORMAL") == 0)
			PipelineStateMap[EPSOType::StaticMesh_WorldNormal]->Shader = FindShaderProgram(Key);
	}
}

FShaderProgram* FRenderResourceManager::GetForwardShaderProgram(ELightingModel Model)
{
	const char* Name = "LIGHTING_MODEL_LAMBERT";
	switch (Model)
	{
	case ELightingModel::Gouraud: Name = "LIGHTING_MODEL_GOURAUD"; break;
	case ELightingModel::Phong: Name = "LIGHTING_MODEL_PHONG"; break;
	case ELightingModel::Unlit: Name = "LIGHTING_MODEL_UNLIT"; break;
	default: break;
	}
	const FString Key = FString("EngineResources/Shaders/UberLit.hlsl#") + Name;
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FPipelineState* FRenderResourceManager::GetPSO(const EPSOType& InType)
{
	if (auto* Found = Get().PipelineStateMap.Find(InType))
	{
		return Found->get();
	}
	return nullptr;
}

void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	// 셰이더는 엔진 폴더에 있다. 작업 디렉터리는 프로젝트 폴더이므로 엔진 폴더 기준으로 찾는다.
	const fs::path AbsoluteRoot = FPaths::EngineFile(ShaderRoot);
	if (!fs::exists(AbsoluteRoot, ErrorCode))
	{
		KRS_LOG(Error, "[Shader] Scan Root Not Found : {}", FPaths::ToUtf8(AbsoluteRoot));
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(AbsoluteRoot))
	{
		if (!Entry.is_regular_file()) continue;
		if (Entry.path().extension() != ".hlsl") continue;
		// 컴퓨트 전용 .hlsl에는 mainVS/mainPS가 없다. VS/PS로 컴파일하지 않는다.
		std::ifstream SourceFile(Entry.path(), std::ios::binary);
		if (!SourceFile) continue;
		const std::string Source(std::istreambuf_iterator<char>{ SourceFile }, std::istreambuf_iterator<char>{});
		if (Source.find("mainVS(") == std::string::npos ||
			Source.find("mainPS(") == std::string::npos) continue;

		// 키는 지금까지처럼 엔진 상대 경로로 둔다.
		FString Path = FPaths::ToUtf8(Entry.path().lexically_relative(FPaths::EngineDir()));
		// 메시를 그리는 셰이더는 VertexFactory.hlsli를 통해 정점을 받으므로 정점 타입마다 변형을 만든다
		if (Source.find("VertexFactory.hlsli") != std::string::npos)
			CompileVertexFactoryVariants(Path, Path, nullptr);
		else
			LoadOrCompileShader(Path);
	}
}

void FRenderResourceManager::Shutdown()
{
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath)
{
	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(InPath))
		return Found->get();

	KRS_LOG(Error, "[Shader] not found: {}", InPath);

	if (TUniquePtr<FShaderProgram>* Fallback =
		Get().ShaderProgramMap.Find("EngineResources/Shaders/DefaultShader.hlsl"))
		return Fallback->get();

	return nullptr;
}

FShaderProgram* FRenderResourceManager::FindShaderProgram(const FString& InPath)
{
	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(InPath))
		return Found->get();
	return nullptr;
}



void FRenderResourceManager::LoadOrCompileShader(const FString& Path)
{ 
	FString VSCSOPath;
	FString PSCSOPath;
	FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath);
	FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath);

	if (!VSCode.IsValid() || !PSCode.IsValid())
	{
		KRS_LOG(Error, "[Shader] compile failed: {}", Path);
		return;
	}

	TUniquePtr<FVertexShader> Vs = RenderCommand::CreateVertexShader(VSCode);
	TUniquePtr<FPixelShader>  Ps = RenderCommand::CreatePixelShader(PSCode);

	if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
	{
		KRS_LOG(Error, "[Shader] device create failed: {}", Path);
		return;
	}

	FVertexShader* VsRaw = Vs.get();
	FPixelShader* PsRaw = Ps.get();

	VertexShaderMap[VSCSOPath] = std::move(Vs);
	PixelShaderMap[PSCSOPath] = std::move(Ps);
	ShaderProgramMap[Path] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);
	ShaderProgramMap[Path]->Name = Path;

	KRS_LOG(Info, "[Shader] loaded: {}", Path);

}

FString FRenderResourceManager::MakeVFKey(const FString& BaseKey, EVertexFactoryType VF)
{
	// Local은 기존 키를 그대로 써서 PSO 테이블과 GetForwardShaderProgram이 Local 변형을 가리키게 한다
	if (VF == EVertexFactoryType::Local) return BaseKey;
	return BaseKey + "#" + GetVertexFactoryTypeInfo(VF).Name;
}

// 정점 타입마다 VS/PS를 같은 매크로로 컴파일하고, VS 입력 레이아웃은 정점 타입이 정한 것으로 만든다
void FRenderResourceManager::CompileVertexFactoryVariants(const FString& Path, const FString& BaseKey, const char* ExtraMacro)
{
	for (uint8 Index = 0; Index < static_cast<uint8>(EVertexFactoryType::Count); ++Index)
	{
		const EVertexFactoryType VF = static_cast<EVertexFactoryType>(Index);
		const FVertexFactoryTypeInfo& Info = GetVertexFactoryTypeInfo(VF);
		if (Info.Elements.empty()) continue;

		D3D_SHADER_MACRO Defines[3] = {};
		int32 Count = 0;
		if (ExtraMacro) Defines[Count++] = { ExtraMacro, "1" };
		Defines[Count++] = { Info.Define, "1" };

		// .cso 캐시 파일 이름에 매크로가 들어가므로 변형마다 따로 캐시된다
		FString VSCSOPath;
		FString PSCSOPath;
		FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath, Defines);
		FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath, Defines);
		if (!VSCode.IsValid() || !PSCode.IsValid())
		{
			KRS_LOG(Error, "[Shader] {} variant compile failed: {}", Info.Name, BaseKey);
			continue;
		}

		TUniquePtr<FVertexShader> VS = RenderCommand::CreateVertexShader(VSCode, Info.Elements);
		TUniquePtr<FPixelShader> PS = RenderCommand::CreatePixelShader(PSCode);
		if (!VS || !VS->IsValid() || !VS->GetLayout() || !PS || !PS->IsValid())
		{
			KRS_LOG(Error, "[Shader] {} variant create failed: {}", Info.Name, BaseKey);
			continue;
		}

		FVertexShader* VSRaw = VS.get();
		FPixelShader* PSRaw = PS.get();
		VertexShaderMap[VSCSOPath] = std::move(VS);
		PixelShaderMap[PSCSOPath] = std::move(PS);
		const FString Key = MakeVFKey(BaseKey, VF);
		ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(VSRaw, PSRaw);
		ShaderProgramMap[Key]->Name = Key;
		ShaderProgramMap[Key]->bUsesVertexFactory = true;
	}
	KRS_LOG(Info, "[Shader] loaded: {}", BaseKey);
}

FShaderProgram* FRenderResourceManager::ResolveShader(FShaderProgram* Base, EVertexFactoryType VF)
{
	if (!Base) return nullptr;
	// 빔, 리본: 파티클 머티리얼의 블렌드와 깊이 설정은 PSO 그대로 두고 셰이더만 정점 형식에 맞는 것으로 바꾼다
	if (VF == EVertexFactoryType::Trail)
	{
		return Base->Name == "EngineResources/Shaders/ParticleSpriteShader.hlsl"
			? FindShaderProgram("EngineResources/Shaders/ParticleTrailShader.hlsl")
			: nullptr;
	}
	// 전용 셰이더는 자기 입력 하나만 받는다
	const bool bDedicatedVF = GetVertexFactoryTypeInfo(VF).Elements.empty();
	if (!Base->bUsesVertexFactory)
		return VF == EVertexFactoryType::Local || bDedicatedVF ? Base : nullptr;
	if (bDedicatedVF) return nullptr;
	if (VF == EVertexFactoryType::Local) return Base;
	return FindShaderProgram(MakeVFKey(Base->Name, VF));
}