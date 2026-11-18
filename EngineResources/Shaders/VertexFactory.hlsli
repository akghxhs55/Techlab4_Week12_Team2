// 메시를 그리는 패스 셰이더가 정점을 받는 유일한 입구.
// 정점 타입마다 FVertexFactoryInput과 VF_GetIntermediates가 다르고, 패스는 월드 공간 결과만 쓴다.
// 입력 레이아웃은 VertexFactory.cpp가 정하므로 FVertexFactoryInput의 시맨틱은 그 표와 같아야 한다.
#ifndef VERTEX_FACTORY_HLSLI
#define VERTEX_FACTORY_HLSLI

struct FVFIntermediates
{
    float3 WorldPosition;
    float3 WorldNormal;   // 정규화하지 않은 값
    float4 WorldTangent;  // xyz 정규화하지 않은 값, w = 종법선 부호
    float2 UV;
    float4 Color;
};

FVFIntermediates VF_LocalToWorld(float3 Position, float3 Normal, float4 Tangent, float2 UV, float4 Color,
    float4x4 World, float4x4 NormalMatrix)
{
    FVFIntermediates Result;
    Result.WorldPosition = mul(float4(Position, 1.0f), World).xyz;
    Result.WorldNormal = mul(float4(Normal, 0.0f), NormalMatrix).xyz;
    // 로컬 공간 부호에 월드 변환의 거울 반전만 반영한다
    const float Handedness = Tangent.w * (determinant((float3x3)World) < 0.0f ? -1.0f : 1.0f);
    Result.WorldTangent = float4(mul(float4(Tangent.xyz, 0.0f), World).xyz, Handedness);
    Result.UV = UV;
    Result.Color = Color;
    return Result;
}

#if VF_GPU_SKIN
    #include "VF_GpuSkin.hlsli"
#elif VF_MESH_PARTICLE
    #include "VF_MeshParticle.hlsli"
#else
    #include "VF_Local.hlsli"
#endif

#endif
