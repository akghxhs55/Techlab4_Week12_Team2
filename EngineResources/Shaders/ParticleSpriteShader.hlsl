#pragma pack_matrix(row_major)

cbuffer constants : register(b0)
{
    matrix VP;
};

Texture2D SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
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
    output.position = mul(float4(input.position, 1.0f), VP);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    return SpriteTexture.Sample(SpriteSampler, input.uv) * input.color;
}