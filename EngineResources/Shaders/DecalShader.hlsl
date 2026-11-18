#pragma pack_matrix(row_major)
#include "VertexFactory.hlsli"

cbuffer PerObject : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
};

cbuffer DecalConstants : register(b1)
{
    matrix DecalVP;
    float Fade;
    float bAdditive;
    float2 Padding;
}

Texture2D DecalTexture : register(t0);
SamplerState DecalSampler : register(s0);

struct VS_OUT
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD;
};

// 노멀은 쓰지 않으므로 NormalMatrix 자리에 World를 넘긴다
VS_OUT mainVS(FVertexFactoryInput Input)
{
    VS_OUT OUTPUT;
    const FVFIntermediates VF = VF_GetIntermediates(Input, World, World);
    float4 WorldPosition = float4(VF.WorldPosition, 1.0f);
    float4 ViewPosition = mul(WorldPosition, View);
    
    OUTPUT.Position = mul(ViewPosition, Projection);
    OUTPUT.WorldPosition = WorldPosition.xyz;
    return OUTPUT;
}

float4 mainPS(VS_OUT Input) : SV_Target
{
    float4 DecalClip = mul(float4(Input.WorldPosition, 1), DecalVP);
    
    float3 NDC = DecalClip.xyz / DecalClip.w;
    if (abs(NDC.x) > 1 || abs(NDC.y) > 1 || NDC.z < 0 || NDC.z > 1)
    {
        discard;
    }
    
    float u = 0.5 * NDC.x + 0.5;
    float v = - 0.5 * NDC.y + 0.5;
    
    float4 color = DecalTexture.Sample(DecalSampler, float2(u, v));
    color.a *= Fade;
    
    if (bAdditive > 0.5)
    {
        color.rgb *= color.a;
    }
    
    return color; 
}