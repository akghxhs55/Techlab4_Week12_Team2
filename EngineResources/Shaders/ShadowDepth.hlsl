// Resources/Shader/ShadowDepth.hlsl
#pragma pack_matrix(row_major)
#include "VertexFactory.hlsli"

cbuffer ViewConstants : register(b0)
{
    matrix VP;
};                       // Renderer->UploadViewMatrix(ShadowViewProjection)
cbuffer WorldConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
}; // 그림자 Build가 b2(Vertex)에 오브젝트 상수를 묶음

cbuffer ShadowDepthPass : register(b3)
{
    float4 ShadowParams;
    float3 LightDirection;
    float ShadowNear;
    uint bPerspective;
    float3 ShadowPadding;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float ShadowDepth : TEXCOORD0;
    float DepthBias : TEXCOORD1;
};

float ComputeDepthBias(float3 WorldNormal)
{
    const float NoL = abs(dot(LightDirection, WorldNormal));
    const float MaxSlope = ShadowParams.z;
    const float Slope = clamp(NoL > 0 ? sqrt(saturate(1 - NoL * NoL)) / NoL : MaxSlope, 0, MaxSlope);
    return ShadowParams.x + ShadowParams.y * Slope;
}

VS_OUTPUT mainVS(FVertexFactoryInput Input)
{
    VS_OUTPUT Output;
    const FVFIntermediates VF = VF_GetIntermediates(Input, World, NormalMatrix);
    const float4 WorldPosition = float4(VF.WorldPosition, 1.0f);
    const float3 WorldNormal = normalize(VF.WorldNormal);
    Output.Position = mul(WorldPosition, VP);

    if (bPerspective)
    {
        // Perspective는 w = 라이트 공간 X. near~far를 0~1 선형으로
        Output.ShadowDepth = (Output.Position.w - ShadowNear) * ShadowParams.w;
    }
    else
    {
        // 근평면보다 광원 쪽에 있는 Caster를 0에 붙여서 잘리지 않게 함
        Output.Position.z = max(Output.Position.z, 0.0f);
        Output.ShadowDepth = Output.Position.z; // Ortho는 이미 선형
    }
    Output.DepthBias = ComputeDepthBias(WorldNormal);
    return Output;
}

// Week9 깊이는 0 = 가까움. bias를 더해 Caster를 광원에서 멀리 민다.
float mainPS(VS_OUTPUT Input) : SV_Depth
{
    return saturate(Input.ShadowDepth + Input.DepthBias);
}