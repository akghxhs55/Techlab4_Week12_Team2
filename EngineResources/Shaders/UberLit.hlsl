#pragma pack_matrix(row_major)

#include "LightData.hlsli"
#include "LightEvaluation.hlsli"
#include "LightGridCommon.hlsli"
#include "VertexFactory.hlsli"

// 셰이더 스캐너는 매크로 정의 없이 이 파일을 컴파일하므로 기본 변형에는 Lambert를 쓴다.
// 명시적인 변형은 조명 모델 또는 뷰 모드 하나만 1로 설정한다.
#if !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_UNLIT) && !defined(VIEW_MODE_WORLD_NORMAL)
    #define LIGHTING_MODEL_LAMBERT 1
#endif

#if (LIGHTING_MODEL_GOURAUD + LIGHTING_MODEL_LAMBERT + LIGHTING_MODEL_PHONG + LIGHTING_MODEL_UNLIT + VIEW_MODE_WORLD_NORMAL) != 1
    #error Exactly one lighting model or view mode must be enabled.
#endif

struct FAmbientLightInfo
{
    float4 ColorIntensity; // rgb: 선형 색상, a: 광도
};

cbuffer PerObject : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix NormalMatrix; // World 행렬의 역행렬을 전치한 법선 변환 행렬
    float4 ShadowFlags;
};

cbuffer Lighting : register(b1)
{
    FAmbientLightInfo Ambient;
    float4 CameraPosition;
    uint4 LightCounts; // x: 포인트 개수, y: 스포트 개수, z: 방향광 개수
    uint4 LightOffsets; // SceneLights에서 각 종류가 시작되는 위치, 순서는 위와 같음
    // 그림자는 광원마다 Light.ShadowMapChannel로 ScreenShadowMask(t13)에서 읽는다.
};

StructuredBuffer<FLightRenderParameters> SceneLights : register(t8);
StructuredBuffer<uint4> TileLightRanges : register(t9);
StructuredBuffer<uint> TilePointIndices : register(t10);
StructuredBuffer<uint> TileSpotIndices : register(t12);
// 그림자 광원마다 배정된 채널(R~A)에 Projection 결과가 들어 있다. 1 = 빛 받음.
Texture2D<float4> ScreenShadowMask : register(t13);

cbuffer MaterialParams : register(b2)
{
    float4 BaseColor;
    float4 Emissive;
    float4 SurfaceParams; // x: 노멀맵 Y 반전, y: 예약, z: 컬러 텍스처 사용, w: 노멀맵 인코딩
    float4 UVTransform; // xy: 이동량, zw: 배율
    float4 SpecularColorShininess; // rgb: 정반사 색상, w: 광택 지수
};

Texture2D ColorTexture : register(t0);
Texture2D NormalTexture : register(t1);

SamplerState ColorSampler : register(s0);

struct VS_OUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
    float3 WorldNormal : TEXCOORD4;
    float4 WorldTangent : TEXCOORD5;
#if LIGHTING_MODEL_GOURAUD
    float3 VertexDiffuse : TEXCOORD1;
    float3 VertexSpecular : TEXCOORD2;
    float3 WorldPosition : TEXCOORD3;
#elif !VIEW_MODE_WORLD_NORMAL
    float3 WorldPosition : TEXCOORD1;
    float ViewDepth : TEXCOORD2;
#endif
    float BoneWeight : TEXCOORD6;
};

struct FUberOutput
{
    float4 LitColor : SV_Target0;
#if !VIEW_MODE_WORLD_NORMAL
    float4 NormalRoughness : SV_Target1;
#endif
};

// 광원의 그림자 (1 = 빛 받음). 채널이 없거나 이 물체의 bReceiveShadow가 꺼져 있으면 1.
// Gouraud는 채널을 받지 않으므로 정점 셰이더에서 불려도 마스크를 읽지 않는다.
float GetLightShadow(FLightRenderParameters Light, float3 PixelPosition)
{
    if (Light.ShadowMapChannel == INVALID_SHADOW_CHANNEL || ShadowFlags.x < 0.5f) return 1.0f;
    return ScreenShadowMask.Load(int3(int2(PixelPosition.xy), 0))[Light.ShadowMapChannel];
}


float3 DecodeMapNormal(float3 Sample, float Encoding)
{
    if (Encoding > 1.5f)
    {
        float2 XY = Encoding > 2.5f ? Sample.xy : Sample.xy * 2.0f - 1.0f;
        return float3(XY, sqrt(saturate(1.0f - dot(XY, XY))));
    }
    return Sample * 2.0f - 1.0f;
}

// 조명과 World Normal 보기가 같은 노멀맵 해석 및 TBN 변환을 사용한다.
float3 GetShadingNormal(VS_OUT Input)
{
    float LengthSquared = dot(Input.WorldNormal, Input.WorldNormal);
    float3 N = LengthSquared > 1.0e-8f
        ? Input.WorldNormal * rsqrt(LengthSquared)
        : float3(0.0f, 0.0f, 1.0f);
    if (SurfaceParams.w > 0.5f)
    {
        float3 MapNormal = DecodeMapNormal(NormalTexture.Sample(ColorSampler, Input.UV).xyz, SurfaceParams.w);
        if (SurfaceParams.x > 0.5f) MapNormal.y = -MapNormal.y;
        float3 T = Input.WorldTangent.xyz - N * dot(N, Input.WorldTangent.xyz);
        float TangentLengthSquared = dot(T, T);
        if (TangentLengthSquared > 1.0e-8f)
        {
            T *= rsqrt(TangentLengthSquared);
            float3 B = cross(N, T) * Input.WorldTangent.w;
            float3 MappedNormal = T * MapNormal.x + B * MapNormal.y + N * MapNormal.z;
            float MappedLengthSquared = dot(MappedNormal, MappedNormal);
            if (MappedLengthSquared > 1.0e-8f)
                N = MappedNormal * rsqrt(MappedLengthSquared);
        }
    }
    return N;
}

#if !VIEW_MODE_WORLD_NORMAL
float3 CalculateAmbientLight(FAmbientLightInfo Info)
{
    return Info.ColorIntensity.rgb * max(Info.ColorIntensity.a, 0.0f);
}

void AddLight(inout FLightResult Result, float3 N, float3 V, float3 L,
    float3 Radiance, bool bIncludeSpecular)
{
    AccumulateLight(Result, N, V, L, Radiance, SpecularColorShininess.rgb,
        SpecularColorShininess.w, bIncludeSpecular);
}

void CalculateDirectionalLight(inout FLightResult Result, FLightRenderParameters Info, float3 N, float3 V, bool bIncludeSpecular)
{
    if (Info.Intensity <= 0.0f)
    {
        return;
    }
    
    float3 L = normalize(-Info.Direction);
    float3 Radiance = Info.Color * Info.Intensity;
    
    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

void CalculatePointLight(inout FLightResult Result, FLightRenderParameters Info,
    float3 P, float3 N, float3 V, bool bIncludeSpecular)
{
    float3 ToLight = Info.Position - P;
    float DistanceSquared = dot(ToLight, ToLight);
    float RadiusSquared = Info.Radius * Info.Radius;
    if (RadiusSquared <= 0.0f || DistanceSquared >= RadiusSquared || DistanceSquared < 1.0e-8f)
        return;
    float Distance = sqrt(max(DistanceSquared, 1.0e-6f));
    // Point Light의 RadiusFallOff를 반경 감쇠 지수로 사용한다.
    float Fade = pow(saturate(1.0f - Distance / Info.Radius), max(Info.FalloffExponent, 0.0f));
    if (Fade <= 0.0f) return;
    float3 L = ToLight / Distance;
    float3 Radiance = Info.Color * max(Info.Intensity, 0.0f) * Fade;
    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

void CalculateSpotLight(inout FLightResult Result, FLightRenderParameters Info, float3 P, float3 N, float3 V, bool bIncludeSpecular)
{
    float3 ToLight = Info.Position - P;
    float DistanceSquared = dot(ToLight, ToLight);
    float RadiusFade = CalculateDistanceAttenuation(DistanceSquared, Info.Radius * Info.Radius);
    if (RadiusFade <= 0.0f) return;
    float3 L = ToLight / sqrt(max(DistanceSquared, 1.0e-6f));
	float ConeCos = dot(-L, normalize(Info.Direction));
	float ConeFade = CalculateConeAttenuation(ConeCos, Info.InnerConeCos, Info.OuterConeCos);
	ConeFade = pow(ConeFade, max(Info.FalloffExponent, 0.0001f));

	float3 Radiance = Info.Color * max(Info.Intensity, 0.0f) * ConeFade * RadiusFade;

    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

FLightResult CalculateBaseLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 N, float3 V, bool bIncludeSpecular)
{
    FLightResult Result;
    Result.Diffuse = CalculateAmbientLight(Ambient);
    Result.Specular = 0.0f;

    [loop]
    for (uint DirectionalIndex = 0; DirectionalIndex < LightCounts.z; ++DirectionalIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.z + DirectionalIndex];
        
        float Shadow = GetLightShadow(Light, PixelPosition);
            
        FLightResult DirectionalResult = (FLightResult) 0;
        CalculateDirectionalLight(DirectionalResult, Light, N, V, bIncludeSpecular);
        DirectionalResult.Diffuse *= Shadow;
        DirectionalResult.Specular *= Shadow;
         
        Result.Diffuse += DirectionalResult.Diffuse;
        Result.Specular += DirectionalResult.Specular;
    }
    
    return Result;
}

void AddAllLocalLights(inout FLightResult Result, float3 PixelPosition, float3 P, float3 N,
    float3 V, bool bIncludeSpecular)
{
    [loop]
    for (uint PointIndex = 0; PointIndex < LightCounts.x; ++PointIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.x + PointIndex];

        float Shadow = GetLightShadow(Light, PixelPosition);

        FLightResult PointResult = (FLightResult) 0;
        CalculatePointLight(PointResult, SceneLights[LightOffsets.x + PointIndex], P, N, V, bIncludeSpecular);
         
        Result.Diffuse += PointResult.Diffuse * Shadow;
        Result.Specular += PointResult.Specular * Shadow;
    }
    [loop]
    for (uint SpotIndex = 0; SpotIndex < LightCounts.y; ++SpotIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.y + SpotIndex];
        
        float Shadow = GetLightShadow(Light, PixelPosition);
            
        FLightResult SpotResult = (FLightResult) 0;
        CalculateSpotLight(SpotResult, Light, P, N, V, bIncludeSpecular);

        Result.Diffuse += SpotResult.Diffuse * Shadow;
        Result.Specular += SpotResult.Specular * Shadow;
    }
}

FLightResult CalculateLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 Normal, bool bIncludeSpecular)
{
    float3 N = normalize(Normal);
    float3 ToCamera = CameraPosition.xyz - P;
    float3 V = ToCamera * rsqrt(max(dot(ToCamera, ToCamera), 1.0e-8f));
    FLightResult Result = CalculateBaseLighting(PixelPosition, P, ViewDepth, N, V, bIncludeSpecular);
    AddAllLocalLights(Result, PixelPosition, P, N, V, bIncludeSpecular);
    return Result;
}

// 타일 목록의 인덱스는 kim의 공유 SceneLights 안에서 종류별 시작 위치에 더한다.
FLightResult CalculateTiledLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 Normal, bool bIncludeSpecular)
{
    float3 N = normalize(Normal);
    float3 ToCamera = CameraPosition.xyz - P;
    float3 V = ToCamera * rsqrt(max(dot(ToCamera, ToCamera), 1.0e-8f));
    FLightResult Result = CalculateBaseLighting(PixelPosition, P, ViewDepth, N, V, bIncludeSpecular);

    if (GridMode == LIGHT_GRID_MODE_OFF)
    {
        // Off는 업로드된 전체 로컬 광원을 평가한다.
        AddAllLocalLights(Result, PixelPosition, P, N, V, bIncludeSpecular);
    }
    else
    {
        // SV_POSITION.xy는 픽셀 위치, z는 현재 표면의 DeviceZ다.
        uint CellIndex = GetLightGridCellIndex(uint2(PixelPosition.xy), PixelPosition.z);
        uint4 Range = TileLightRanges[CellIndex];
        bool UsePointList = Range.x != 0xffffffffu;
        bool UseSpotList = Range.z != 0xffffffffu;
        uint PointCount = UsePointList ? Range.y : LightCounts.x;
        uint SpotCount = UseSpotList ? Range.w : LightCounts.y;

        // 예약 실패한 종류만 전체 배열을 평가하는 기존 fallback을 유지한다.
        [loop]
        for (uint PointListIndex = 0; PointListIndex < PointCount; ++PointListIndex)
        {
            uint LightIndex = UsePointList ? TilePointIndices[Range.x + PointListIndex] : PointListIndex;
            
            FLightRenderParameters Light = SceneLights[LightOffsets.x + LightIndex];

            float Shadow = GetLightShadow(Light, PixelPosition);
            
            FLightResult PointResult = (FLightResult) 0;
            CalculatePointLight(PointResult, Light, P, N, V, bIncludeSpecular);
         
            Result.Diffuse += PointResult.Diffuse * Shadow;
            Result.Specular += PointResult.Specular * Shadow;
        }

        [loop]
        for (uint SpotListIndex = 0; SpotListIndex < SpotCount; ++SpotListIndex)
        {
            uint LightIndex = UseSpotList ? TileSpotIndices[Range.z + SpotListIndex] : SpotListIndex;
            
            FLightRenderParameters Light = SceneLights[LightOffsets.y + LightIndex];
            
            float Shadow = GetLightShadow(Light, PixelPosition);
            
            FLightResult SpotResult = (FLightResult) 0;
            CalculateSpotLight(SpotResult, Light, P, N, V, bIncludeSpecular);
            
            Result.Diffuse += SpotResult.Diffuse * Shadow;
            Result.Specular += SpotResult.Specular * Shadow;
        }
    }
    return Result;
}

#endif


// Week11 Unreal 스타일 본 가중치 히트맵 그라디언트 (0: 마젠타 -> 파랑 -> 하늘 -> 초록 -> 노랑 -> 빨강)
float3 CalculateBoneWeightHeatmap(float Weight)
{
    const float t = saturate(Weight);
    const float3 MagentaColor = float3(160.0f / 255.0f, 32.0f / 255.0f, 240.0f / 255.0f);
    const float3 BlueColor    = float3(0.0f, 0.05f, 0.95f);
    const float3 CyanColor    = float3(0.0f, 0.95f, 0.95f);
    const float3 GreenColor   = float3(0.1f, 0.95f, 0.1f);
    const float3 YellowColor  = float3(1.0f, 0.90f, 0.0f);
    const float3 RedColor     = float3(1.0f, 0.05f, 0.05f);

    if (t < 0.08f)
    {
        float u = t / 0.08f;
        u = u * u * (3.0f - 2.0f * u);
        return lerp(MagentaColor, BlueColor, u);
    }
    else if (t < 0.22f)
    {
        return lerp(BlueColor, CyanColor, (t - 0.08f) / (0.22f - 0.08f));
    }
    else if (t < 0.40f)
    {
        return lerp(CyanColor, GreenColor, (t - 0.22f) / (0.40f - 0.22f));
    }
    else if (t < 0.55f)
    {
        return lerp(GreenColor, YellowColor, (t - 0.40f) / (0.55f - 0.40f));
    }
    else if (t < 0.72f)
    {
        return lerp(YellowColor, RedColor, (t - 0.55f) / (0.72f - 0.55f));
    }
    return RedColor;
}

VS_OUT Uber_VS(FVertexFactoryInput Input)
{
    VS_OUT Output;
    const FVFIntermediates VF = VF_GetIntermediates(Input, World, NormalMatrix);
    float4 WorldPosition = float4(VF.WorldPosition, 1.0f);
    float4 ViewPosition = mul(WorldPosition, View);
    Output.Position = mul(ViewPosition, Projection);
    Output.Color = VF.Color;
    Output.BoneWeight = 0.0f;
    if (ShadowFlags.y > 0.5f)
    {
#if VF_GPU_SKIN
        const uint Bone = (uint)(ShadowFlags.y - 0.5f);   // 본 번호 + 1로 실은 값을 되돌린다
        Output.BoneWeight = GpuSkinWeightForBone(Input.BlendIndices, Input.BlendWeights, Bone);
#else
        Output.BoneWeight = Input.Color.r;
#endif
    }
    Output.UV = VF.UV * UVTransform.zw + UVTransform.xy;

    float3 N = VF.WorldNormal;
    Output.WorldNormal = N;
    Output.WorldTangent = VF.WorldTangent;
#if LIGHTING_MODEL_GOURAUD
    FLightResult Lit = CalculateLighting(float3(0.0f, 0.0f, 0.0f), WorldPosition.xyz, ViewPosition.x, N, true);
    Output.VertexDiffuse = Lit.Diffuse;
    Output.VertexSpecular = Lit.Specular;
    Output.WorldPosition = WorldPosition.xyz;
#elif !VIEW_MODE_WORLD_NORMAL
    Output.WorldPosition = WorldPosition.xyz;
    Output.ViewDepth = ViewPosition.x;
#endif
    return Output;
}

FUberOutput Uber_PS(VS_OUT Input)
{
    FUberOutput Output;
    float3 ShadingNormal = GetShadingNormal(Input);
#if VIEW_MODE_WORLD_NORMAL
    Output.LitColor = float4(ShadingNormal * 0.5f + 0.5f, 1.0f);
#else
    if (ShadowFlags.y > 0.5f)
    {
        const float3 HeatColor = CalculateBoneWeightHeatmap(Input.BoneWeight);
        if (ShadowFlags.z > 0.5f)
        {
            const float3 WireColor = (ShadowFlags.z > 1.5f)
                ? float3(1.0f, 1.0f, 1.0f)
                : saturate(HeatColor * 1.35f + float3(0.08f, 0.08f, 0.08f));
            Output.LitColor = float4(WireColor, 1.0f);
            Output.NormalRoughness = float4(ShadingNormal, 0.8f); // SV_Target1도 반드시 채운다
            return Output;
        }

        const float3 HeatmapL = normalize(CameraPosition.xyz - Input.WorldPosition);
        const float HalfLambert = dot(ShadingNormal, HeatmapL) * 0.5f + 0.5f;
        const float LightingFactor = HalfLambert * 0.7f + 0.3f;
        Output.LitColor = float4(HeatColor * LightingFactor, 1.0f);
        Output.NormalRoughness = float4(ShadingNormal, 0.8f); // SV_Target1도 반드시 채운다
        return Output;
    }
    float4 SurfaceColor;
    if (SurfaceParams.z > 0.5f)
    {
        float4 TextureColor = ColorTexture.Sample(ColorSampler, Input.UV);
        SurfaceColor = float4(pow(max(TextureColor.rgb, 0.0f), 2.2f), TextureColor.a);
#if VF_MESH_PARTICLE
        // 텍스처가 있으면 정점 색을 쓰지 않으므로 파티클 색(Color/ColorOverLife 모듈)을 여기서 곱한다
        SurfaceColor *= Input.Color;
#endif
    }
    else
    {
        SurfaceColor = Input.Color;
    }
    float3 Albedo = SurfaceColor.rgb * BaseColor.rgb;
    float3 Diffuse;
    float3 Specular = 0.0f;

#if LIGHTING_MODEL_GOURAUD
    Diffuse = Input.VertexDiffuse;
    Specular = Input.VertexSpecular;
#elif LIGHTING_MODEL_LAMBERT
    // 픽셀 위치와 깊이를 함께 전달한다.
    Diffuse = CalculateTiledLighting(Input.Position.xyz, Input.WorldPosition, Input.ViewDepth, ShadingNormal, false).Diffuse;
#elif LIGHTING_MODEL_PHONG
    // 같은 목록 선택 경로에서 기존 Blinn-Phong 조명을 계산한다.
    FLightResult Lit = CalculateTiledLighting(Input.Position.xyz, Input.WorldPosition, Input.ViewDepth, ShadingNormal, true);
    Diffuse = Lit.Diffuse;
    Specular = Lit.Specular;
#elif LIGHTING_MODEL_UNLIT
    // 조명·그림자를 계산하지 않고 표면 색을 그대로 낸다.
    Diffuse = 1.0f;
#endif

    Output.LitColor = float4(Albedo * Diffuse + Specular + Emissive.rgb,
        SurfaceColor.a * BaseColor.a);
    Output.NormalRoughness = float4(ShadingNormal, 0.8f);
#endif
    return Output;
}

// 현재 리소스 매니저는 .hlsl 파일을 스캔할 때 mainVS/mainPS 진입점을 찾는다.
VS_OUT mainVS(FVertexFactoryInput Input) { return Uber_VS(Input); }
FUberOutput mainPS(VS_OUT Input) { return Uber_PS(Input); }
