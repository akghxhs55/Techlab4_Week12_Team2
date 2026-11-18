#pragma pack_matrix(row_major)

// 언리얼 ShadowProjectionPixelShader: 그림자 한 장을 화면 마스크(ScreenShadowMask)의 한 채널에 쓴다. 1 = 빛을 받음.
// 깊이 아틀라스는 ShadowDepth.hlsl이 쓴 값이다. Directional은 Ortho 깊이, Spot·Point는 MinSubjectZ~MaxSubjectZ 선형 깊이이고 bias는 이미 들어 있다.

#define SHADOW_ATLAS_TEXEL (1.0f / 4096.0f) // ShadowRendering.h ShadowAtlasSize

cbuffer ShadowProjectionConstants : register(b0)
{
    // 위치는 모두 카메라 기준(Translated World)이다. 원점에서 먼 곳의 float 정밀도 문제를 피한다.
    matrix InvViewProjection;    // 화면 NDC + 깊이 -> 카메라 기준 위치
    matrix ShadowViewProjection; // 카메라 기준 위치 -> 그림자 클립
    float4 AtlasRect;            // xy = 타일 그리는 영역 시작 UV, zw = 크기 UV
    float4 ViewportSize;         // xy = 화면 크기, zw = 1 / 화면 크기
    float3 LightPosition;
    float  ShadowNear;
    float3 LightDirection;
    float  InvDepthRange;
    float3 ViewOrigin;
    float  FadeAlpha;
    float3 ViewForward;
    float  ShadowSharpen;        // Sharpen * 7 + 1
    float  SplitNear;
    float  SplitFar;
    float  FadePlaneOffset;
    float  InvFadePlaneLength;
    uint   AtlasIndex;
    uint   bPerspective;
    uint   bUseFadePlane;
    uint   PcfQuality;
    uint   FilterMethod;
    float  LightSourceSize;
    float  MaxSoftKernelTexels;
    float  WorldTexelScale;
    float  MaxSoftKernelWorld;   // Directional PCSS 반경 상한 (월드, 캐스케이드 공통)
    float3 Padding;
};

Texture2D<float> SceneDepthTexture : register(t0);
Texture2DArray<float> ShadowDepthAtlas : register(t1);

SamplerState PointSampler : register(s2);
SamplerComparisonState ShadowSampler : register(s3); // LESS_EQUAL: 수신점 깊이 <= 저장 깊이면 1

#define POISSON_COUNT 16
static const float2 PoissonDisk[POISSON_COUNT] =
{
    float2(0.758417f, 0.604537f),
    float2(0.666262f, -0.501792f),
    float2(0.044159f, 0.940217f),
    float2(-0.618833f, 0.442616f),
    float2(0.276293f, 0.651964f),
    float2(-0.399537f, -0.290897f),
    float2(-0.104335f, -0.698866f),
    float2(0.260315f, -0.652012f),
    float2(0.129753f, 0.215359f),
    float2(0.333606f, -0.138314f),
    float2(0.484882f, 0.283357f),
    float2(-0.074304f, -0.153791f),
    float2(-0.255648f, 0.191645f),
    float2(-0.649947f, -0.709589f),
    float2(0.838194f, 0.199567f),
    float2(-0.354359f, 0.686538f)
};

float InterleavedGradientNoise(float2 PixelPosition)
{
    return frac(52.9829189f * frac(dot(PixelPosition, float2(0.06711056f, 0.00583715f))));
}

// 커널이 타일 밖(테두리, 옆 타일)을 읽지 않게 그리는 영역 안으로 붙인다.
float2 ClampToTile(float2 UV)
{
    const float2 HalfTexel = 0.5f * SHADOW_ATLAS_TEXEL;
    return clamp(UV, AtlasRect.xy + HalfTexel, AtlasRect.xy + AtlasRect.zw - HalfTexel);
}

float SampleShadow(float2 UV, float Receiver)
{
    return ShadowDepthAtlas.SampleCmpLevelZero(ShadowSampler, float3(ClampToTile(UV), AtlasIndex), Receiver).r;
}

// Kernel: 단위 원 탭 -> 아틀라스 텍셀 오프셋. 등방성이면 반경 x 단위 행렬이다.
float PoissonPCF(float2 UV, float Receiver, float2x2 Kernel, float2 SvPosition, float RotationOffset, float2 SlopePerTexel)
{
    float S, C;
    sincos((InterleavedGradientNoise(SvPosition) + RotationOffset) * 6.28318530718f, S, C);
    float Sum = 0.0f;
    [loop]
    for (uint i = 0; i < POISSON_COUNT; ++i)
    {
        const float2 P = PoissonDisk[i];
        const float2 Rotated = float2(C * P.x - S * P.y, S * P.x + C * P.y);
        const float2 TexelOffset = mul(Kernel, Rotated);
        const float TapReceiver = Receiver + dot(SlopePerTexel, TexelOffset);
        Sum += SampleShadow(UV + TexelOffset * SHADOW_ATLAS_TEXEL, TapReceiver);
    }
    return Sum / POISSON_COUNT;
}

float2x2 IsotropicKernel(float RadiusTexels)
{
    return float2x2(RadiusTexels, 0.0f, 0.0f, RadiusTexels);
}

float PoissonPCF32(float2 UV, float Receiver, float2x2 Kernel, float2 SvPosition, float2 SlopePerTexel)
{
    return 0.5f * (PoissonPCF(UV, Receiver, Kernel, SvPosition, 0.0f, SlopePerTexel)
                 + PoissonPCF(UV, Receiver, Kernel, SvPosition, 0.5f, SlopePerTexel));
}

// r.ShadowQuality 단계. UE는 품질마다 셰이더 변형을 컴파일하지만 여기서는 모든 픽셀이 같은 분기를 탄다.
float FilterPCF(float2 UV, float Receiver, float2 SvPosition, float2 SlopePerTexel)
{
    if (PcfQuality <= 1)
        return SampleShadow(UV, Receiver);

    if (PcfQuality == 2)
        return PoissonPCF(UV, Receiver, IsotropicKernel(2.0f), SvPosition, 0.0f, SlopePerTexel);

    if (PcfQuality == 3)
        return PoissonPCF32(UV, Receiver, IsotropicKernel(2.5f), SvPosition, SlopePerTexel);

    // 4 이상: Poisson 16을 두 번 다른 회전으로 (32 탭)
    return PoissonPCF32(UV, Receiver, IsotropicKernel(3.25f), SvPosition, SlopePerTexel);
}

// 정규화 깊이 -> 라이트 공간 거리. Directional은 깊이 범위 안의 거리, Spot은 광원에서의 거리.
float DepthToLightDistance(float Depth)
{
    return ShadowNear * bPerspective + Depth / InvDepthRange;
}

// 텍셀 하나의 월드 크기. Spot은 광원에서 멀수록 커진다.
float TexelWorldAt(float LightDistance)
{
    return bPerspective ? WorldTexelScale * LightDistance : WorldTexelScale;
}

// PCSS (Directional, Spot). 1) Blocker 평균 깊이 2) 반그림자 폭 3) 그 폭으로 PCF
// SurfaceKernel: 텍셀 단위 원을 수신면 위의 원이 되도록 누르는 행렬 (특이값 <= 1)
float FilterPCSS(float2 UV, float Receiver, float2 SvPosition, float2 SlopePerTexel, float2x2 SurfaceKernel)
{
    const float ReceiverDistance = DepthToLightDistance(Receiver);

    // Blocker 탐색 반경: Directional은 반그림자 상한의 1.5배 (상한보다 넓어야 반그림자 바깥 끝이 끊기지 않는다),
    // Spot은 광원 크기를 수신점에서 본 크기
    const float SearchScale = 1.5f;
    const float SearchWorld = bPerspective
        ? LightSourceSize * (ReceiverDistance - ShadowNear) / max(ReceiverDistance, 1.0e-4f)
        : min(LightSourceSize * ReceiverDistance, MaxSoftKernelWorld * SearchScale);
    const float SearchTexels = clamp(SearchWorld / TexelWorldAt(bPerspective ? ShadowNear : ReceiverDistance), 1.0f, MaxSoftKernelTexels * SearchScale);

    float S, C;
    sincos(InterleavedGradientNoise(SvPosition) * 6.28318530718f, S, C);
    float BlockerSum = 0.0f;
    float BlockerCount = 0.0f;
    [loop]
    for (uint i = 0; i < POISSON_COUNT; ++i)
    {
        const float2 P = PoissonDisk[i];
        const float2 Rotated = float2(C * P.x - S * P.y, S * P.x + C * P.y);
        const float2 TexelOffset = Rotated * SearchTexels;
        const float2 TapUV = ClampToTile(UV + TexelOffset * SHADOW_ATLAS_TEXEL);
        const float TapReceiver = Receiver + dot(SlopePerTexel, TexelOffset);
        const float Depth = ShadowDepthAtlas.SampleLevel(PointSampler, float3(TapUV, AtlasIndex), 0).r;
        if (Depth < TapReceiver)
        {
            BlockerSum += Depth;
            BlockerCount += 1.0f;
        }
    }
    if (BlockerCount <= 0.0f)
        return 1.0f;

    // 반그림자 폭: Directional은 tan(각반지름) x 차폐체-수신점 거리, Spot은 닮은 삼각형
    const float BlockerDistance = DepthToLightDistance(BlockerSum / BlockerCount);
    const float BlockerToReceiver = max(ReceiverDistance - BlockerDistance, 0.0f);
    const float PenumbraWorld = bPerspective
        ? LightSourceSize * BlockerToReceiver / max(BlockerDistance, 1.0e-4f)
        : min(LightSourceSize * BlockerToReceiver, MaxSoftKernelWorld);
    const float FilterTexels = clamp(PenumbraWorld / TexelWorldAt(ReceiverDistance), 1.0f, MaxSoftKernelTexels);
    // 낮은 각도에서 텍셀이 수신면에 길게 늘어나 반그림자가 빛 방향으로만 번지는 것을 막는다. 1텍셀은 계단 방지용으로 남긴다.
    const float2x2 Kernel = SurfaceKernel * max(FilterTexels - 1.0f, 0.0f) + IsotropicKernel(1.0f);
    return PoissonPCF32(UV, Receiver, Kernel, SvPosition, SlopePerTexel);
}

struct FVSOutput
{
    float4 Position : SV_POSITION;
};

FVSOutput mainVS(uint VertexID : SV_VertexID)
{
    FVSOutput Output;
    const float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FVSOutput Input) : SV_Target
{
    const int2 Pixel = int2(Input.Position.xy);
    const float SceneDepth = SceneDepthTexture.Load(int3(Pixel, 0));
    if (SceneDepth >= 1.0f)
        discard; // 하늘

    const float2 ScreenUV = Input.Position.xy * ViewportSize.zw;
    const float2 NDC = float2(ScreenUV.x * 2.0f - 1.0f, 1.0f - ScreenUV.y * 2.0f);
    const float4 WorldH = mul(float4(NDC, SceneDepth, 1.0f), InvViewProjection);
    const float3 World = WorldH.xyz / WorldH.w;

    const float4 ShadowClip = mul(float4(World, 1.0f), ShadowViewProjection);
    const float3 ShadowNDC = ShadowClip.xyz / max(ShadowClip.w, 1.0e-6f);
    const float2 TileUV = ShadowNDC.xy * float2(0.5f, -0.5f) + 0.5f;
    const float Receiver = bPerspective
        ? (dot(World - LightPosition, LightDirection) - ShadowNear) * InvDepthRange
        : ShadowNDC.z;

    // 화면 미분(ddx/ddy)으로 아틀라스 1텍셀당 수신면 깊이 기울기를 구해 낮은 각도에서도 계단/자기그림자를 막는다.
    const float2 AtlasUV = AtlasRect.xy + TileUV * AtlasRect.zw;
    const float2 dTexelDx = ddx(AtlasUV) / SHADOW_ATLAS_TEXEL;
    const float2 dTexelDy = ddy(AtlasUV) / SHADOW_ATLAS_TEXEL;
    const float2 dRdxdy = float2(ddx(Receiver), ddy(Receiver));
    const float Det = dTexelDx.x * dTexelDy.y - dTexelDx.y * dTexelDy.x;
    float2 SlopePerTexel = abs(Det) > 1.0e-8f
        ? float2( dTexelDy.y * dRdxdy.x - dTexelDx.y * dRdxdy.y,
                 -dTexelDy.x * dRdxdy.x + dTexelDx.x * dRdxdy.y) / Det
        : 0.0f;
    // 수평면은 빛 고도 θ에서 텍셀당 cot(θ) 텍셀만큼 깊어진다. 16이면 고도 약 3.6도까지 커버하고, 실루엣의 극단적인 미분만 자른다.
    const float MaxSlopePerTexel = 16.0f * WorldTexelScale * InvDepthRange;
    SlopePerTexel = clamp(SlopePerTexel, -MaxSlopePerTexel, MaxSlopePerTexel);

    // Directional은 이 캐스케이드가 맡은 화면 깊이 구간만
    const float SceneW = dot(World - ViewOrigin, ViewForward);
    if (!bPerspective && (SceneW < SplitNear || SceneW > SplitFar))
        discard;

    if (ShadowClip.w <= 0.0f)
        discard;
    // 이 그림자 프러스텀 밖. Point 6면은 서로 겹치지 않으므로 각 면이 자기 영역만 쓴다.
    if (any(TileUV < 0.0f) || any(TileUV > 1.0f) || Receiver < 0.0f || Receiver > 1.0f)
        discard;

    float Shadow = 1.0f;
    if (FilterMethod == 1)
    {
        // 아틀라스 1텍셀당 수신면 월드 이동량(Eu, Ev)을 QR 분해해 수신면 위 원 -> 텍셀 오프셋 행렬을 만든다.
        const float TexelWorld = TexelWorldAt(DepthToLightDistance(Receiver));
        float2x2 SurfaceKernel = IsotropicKernel(1.0f);
        if (abs(Det) > 1.0e-8f)
        {
            const float3 Wx = ddx(World);
            const float3 Wy = ddy(World);
            const float3 Eu = (dTexelDy.y * Wx - dTexelDx.y * Wy) / (Det * TexelWorld);
            const float3 Ev = (-dTexelDy.x * Wx + dTexelDx.x * Wy) / (Det * TexelWorld);
            const float R11 = length(Eu);
            const float3 Q1 = Eu / max(R11, 1.0e-6f);
            const float R12 = dot(Q1, Ev);
            const float R22 = length(Ev - R12 * Q1);
            // 실루엣·먼 곳의 미분은 믿을 수 없으므로 늘어남 비율이 정상 범위일 때만 쓴다.
            if (R11 > 0.5f && R22 > 0.5f && R11 < 16.0f && R22 < 16.0f)
            {
                SurfaceKernel = float2x2(1.0f / R11, -R12 / (R11 * R22),
                                         0.0f,       1.0f / R22);
                const float Norm = sqrt(dot(SurfaceKernel[0], SurfaceKernel[0]) + dot(SurfaceKernel[1], SurfaceKernel[1]));
                if (Norm > 1.41421356f)
                    SurfaceKernel *= 1.41421356f / Norm;
            }
        }
        Shadow = FilterPCSS(AtlasUV, Receiver, Input.Position.xy, SlopePerTexel, SurfaceKernel);
    }
    else
    {
        Shadow = FilterPCF(AtlasUV, Receiver, Input.Position.xy, SlopePerTexel);
    }

    // UE ShadowProjectionPixelShader.usf: Sharpen 후 FadeAlpha로 그림자 세기를 줄인다.
    Shadow = saturate((Shadow - 0.5f) * ShadowSharpen + 0.5f);
    Shadow = lerp(1.0f, Shadow, FadeAlpha);

    // 캐스케이드 끝부분(FadePlane ~ SplitFar)에서 알파를 0으로 낮춰 먼저 그린 먼 캐스케이드와 섞는다.
    // smoothstep으로 구간 양 끝의 기울기를 0으로 만들어 경계선이 띠처럼 보이지 않게 한다.
    const float BlendFactor = bUseFadePlane ? 1.0f - smoothstep(0.0f, 1.0f, (SceneW - FadePlaneOffset) * InvFadePlaneLength) : 1.0f;
    return float4(Shadow, Shadow, Shadow, BlendFactor);
}
