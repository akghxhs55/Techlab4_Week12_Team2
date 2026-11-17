#pragma once

#include "Shader.h"
#include "PipelineState.h"
#include "VertexFactory.h"

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
	// 기존 씬의 PSOType 번호가 밀리지 않도록 끝에 추가
	Particle_AlphaBlend,
	Particle_Additive,
	StaticMesh_Additive,     // 메시 파티클 가산 블렌드
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
	// 변형이 있는지 확인할 때 쓴다. 없으면 로그와 기본 셰이더 대체 없이 nullptr
	static FShaderProgram* FindShaderProgram(const FString& InPath);

	static FPipelineState* GetPSO(const EPSOType& InType);
	static FShaderProgram* GetForwardShaderProgram(ELightingModel Model);
	// Local 변형인 Base를 정점 타입에 맞는 변형으로 바꾼다. 변형이 없으면 nullptr
	static FShaderProgram* ResolveShader(FShaderProgram* Base, EVertexFactoryType VF);
private:

	static FString MakeVFKey(const FString& BaseKey, EVertexFactoryType VF);
	void LoadOrCompileShader(const FString& Path);
	void CompileVertexFactoryVariants(const FString& Path, const FString& BaseKey, const char* ExtraMacro);
	void InitPipelineStates();

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>>  PixelShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;

	TMap<EPSOType, TUniquePtr<FPipelineState>> PipelineStateMap;
};
