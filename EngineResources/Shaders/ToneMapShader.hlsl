Texture2D<float4> LightingHDR : register(t0);

cbuffer ToneMapconstants : register(b0)
{
    float InvGamma;
    float bGammaCorrection;
    float2 Padding;
}


struct FPSInput
{
    float4 Position : SV_POSITION;
};

FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    int2 Pixel = int2(Input.Position.xy);
    float3 HDR = max(LightingHDR.Load(int3(Pixel, 0)).rgb, 0.0f);
    float3 Ldr = saturate(HDR);
    // SceneColor는 UNORM이므로 화면에 표시할 sRGB 근사값으로 변환한다.
    float3 DisplayColor = (bGammaCorrection > 0.5f) ? pow(Ldr, InvGamma) : Ldr;
    return float4(DisplayColor, 1.0f);
}
