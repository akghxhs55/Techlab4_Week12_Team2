#pragma pack_matrix(row_major)

cbuffer constants : register(b0)
{
    matrix VP;
};

Texture2D TrailTexture : register(t0);
SamplerState TrailSampler : register(s0);

// FParticleTrailVertex와 순서가 같아야 한다 (입력 레이아웃이 선언 순서대로 offset을 쌓는다)
struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    // CPU에서 월드 좌표로 만들었으므로 World를 곱하지 않는다
    output.position = mul(float4(input.position, 1.0f), VP);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    return TrailTexture.Sample(TrailSampler, input.uv) * input.color;
}
