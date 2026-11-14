#pragma once

#include "Shader.h"
#include "PipelineState.h"

// 렌더링에 사용할 파이프라인 종류 정의
enum class EPSOType : uint8
{
	StaticMesh_Opaque,       // 기본 불투명
	StaticMesh_Translucent,  // 반투명
	StaticMesh_Wireframe,    
	Reserved_5,
	Grid,                    
	Outline_Mask,            
	Outline_Draw,
	Reserved_9,
	Reserved_10,
	Reserved_11,
	ToneMap,
	Reserved_13,                // 삭제된 FireBall 자리. 씬 파일의 PSOType 숫자가 밀리지 않게 남긴다
	Billboard,
	StaticMesh_Forward,
	StaticMesh_WorldNormal,
	StaticMesh_DepthOnly,
	StaticMesh_LightShadow,
	LightCullingDebug,
	ShadowTileClear,         // 그림자 캐시: 다시 그릴 아틀라스 타일만 깊이 1로 지운다
	ShadowDepth,
	ShadowProjection,        // 그림자 한 장을 화면 마스크(ScreenShadowMask)에 그린다
	Reserved_23,
	Decal, 
	DecalAdditive,
	Count
};

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders("EngineResources/Shaders");
		Get().InitPipelineStates(); // PSO 초기화 추가
	}

	void ScanShaders(const fs::path& ShaderRoot);
	static void Shutdown();
	static FShaderProgram* GetShaderProgram(const FString& InPath);

	static FPipelineState* GetPSO(const EPSOType& InType);
	static FShaderProgram* GetForwardShaderProgram(ELightingModel Model);
private:
	
	void LoadOrCompileShader(const FString& Path);
	void CreateSkinnedVariant(const FString& Path, const char* LightingMacro, const FShaderProgram& Base);
	void InitPipelineStates();

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>>  PixelShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;

	TMap<EPSOType, TUniquePtr<FPipelineState>> PipelineStateMap;
};
