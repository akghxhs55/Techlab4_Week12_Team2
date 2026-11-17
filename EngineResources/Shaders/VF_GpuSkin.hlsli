// FGpuSkinVertex. 본 행렬로 스키닝한 결과를 로컬 정점처럼 월드로 옮긴다
#include "GpuSkinCommon.hlsli"

struct FVertexFactoryInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float4 Tangent      : TANGENT;
    float2 UV           : TEXCOORD;
    float4 Color        : COLOR;
    uint4  BlendIndices : BLENDINDICES;
    float4 BlendWeights : BLENDWEIGHT;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    float3 Position = Input.Position;
    float3 Normal = Input.Normal;
    float4 Tangent = Input.Tangent;
    GpuSkinVertex(Input.BlendIndices, Input.BlendWeights, Position, Normal, Tangent);
    return VF_LocalToWorld(Position, Normal, Tangent, Input.UV, Input.Color, World, NormalMatrix);
}
