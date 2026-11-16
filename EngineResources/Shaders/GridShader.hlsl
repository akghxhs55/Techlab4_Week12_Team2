#pragma pack_matrix(row_major)
// 원근 뷰포트 전용 XY 평면 Grid. 화면 전체를 덮는 삼각형에서 시선 광선과 Z=0 평면의
// 교점을 구해 선·축을 절차적으로 그리므로 수평선까지 끊김 없이 이어진다.
cbuffer GridCB : register(b0)
{
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float3 CameraPos;
    float GridSpacing; // 주선(Major) 간격. 보조선은 그 1/10이다.
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

// 정점 버퍼 없이 화면 전체를 덮는 삼각형 하나를 만든다.
PS_INPUT mainVS(uint vid : SV_VertexID)
{
    PS_INPUT o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    o.pos = float4(o.ndc, 0.0f, 1.0f);
    return o;
}

static const float MinorAlpha = 0.25f;
static const float MajorAlpha = 0.60f;
static const float MinorHalfWidth = 0.5f;  // 픽셀
static const float MajorHalfWidth = 0.75f; // 픽셀
static const float AxisHalfWidth = 1.5f;   // 픽셀
static const float MinCellPixels = 8.0f;   // 보조선 간격이 이보다 좁아지면 한 단계 위 LOD로 넘어간다.

// 가장 가까운 격자선까지의 화면 픽셀 거리로 반두께 선의 커버리지를 구한다.
float LineCoverage(float2 coords, float step, float2 derivative, float halfWidth)
{
    float2 pixelDist = abs(frac(coords / step - 0.5f) - 0.5f) * step / derivative;
    float d = min(pixelDist.x, pixelDist.y);
    return 1.0f - smoothstep(halfWidth - 0.5f, halfWidth + 0.5f, d);
}

// 한 좌표축(거리 = |coord|)의 픽셀 커버리지를 구한다.
float AxisCoverage(float coord, float derivative)
{
    float d = abs(coord) / derivative;
    return 1.0f - smoothstep(AxisHalfWidth - 0.5f, AxisHalfWidth + 0.5f, d);
}

// src를 dst 위에 덮는 일반 알파 합성(비-프리멀티플라이)이다.
float4 Over(float4 src, float4 dst)
{
    float a = src.a + dst.a * (1.0f - src.a);
    float3 rgb = a > 1.0e-5f ? (src.rgb * src.a + dst.rgb * dst.a * (1.0f - src.a)) / a : 0.0f;
    return float4(rgb, a);
}

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT o;

    // 화면 픽셀을 근·원평면으로 역투영해 월드 시선 광선을 만든다.
    float4 nearH = mul(float4(input.ndc, 0.0f, 1.0f), InvViewProj);
    float4 farH = mul(float4(input.ndc, 1.0f, 1.0f), InvViewProj);
    float3 nearP = nearH.xyz / nearH.w;
    float3 farP = farH.xyz / farH.w;
    float3 ray = farP - nearP;

    // Z=0 평면과의 교점. 평면을 등지거나 평행하면 버린다.
    float t = -nearP.z / ray.z;
    if (!isfinite(t) || t <= 0.0f)
        discard;
    float3 worldPos = nearP + ray * t;
    float2 coords = worldPos.xy;

    // 화면 1픽셀당 월드 좌표 변화량. 수평선 쪽에서 커지며 LOD 선택의 기준이 된다.
    float2 derivative = max(fwidth(coords), float2(1.0e-6f, 1.0e-6f));

    // 보조선 간격이 MinCellPixels 이상이 되도록 10배 단위 LOD를 고르고 소수부로 보간한다.
    float baseStep = max(GridSpacing, 1.0e-3f) * 0.1f;
    float footprint = max(derivative.x, derivative.y);
    float lod = max(log10(footprint * MinCellPixels / baseStep), 0.0f);
    float blend = frac(lod);
    float step0 = baseStep * pow(10.0f, floor(lod));
    float step1 = step0 * 10.0f;
    float step2 = step1 * 10.0f;

    // 가장 촘촘한 단계는 사라지고, 중간 단계는 주선→보조선으로 옅어지며, 가장 성긴 단계는 주선을 유지한다.
    float a0 = LineCoverage(coords, step0, derivative, MinorHalfWidth) * MinorAlpha * (1.0f - blend);
    float a1 = LineCoverage(coords, step1, derivative, lerp(MajorHalfWidth, MinorHalfWidth, blend))
             * lerp(MajorAlpha, MinorAlpha, blend);
    float a2 = LineCoverage(coords, step2, derivative, MajorHalfWidth) * MajorAlpha;
    float4 color = float4(1.0f, 1.0f, 1.0f, max(a0, max(a1, a2)));

    // 월드 X축(빨강, y=0)·Y축(초록, x=0). 음수 방향은 옅게 그린다.
    float xAxis = AxisCoverage(coords.y, derivative.y) * (coords.x >= 0.0f ? 1.0f : 0.3f);
    float yAxis = AxisCoverage(coords.x, derivative.x) * (coords.y >= 0.0f ? 1.0f : 0.3f);
    color = Over(float4(1.0f, 0.0f, 0.0f, xAxis), color);
    color = Over(float4(0.0f, 1.0f, 0.0f, yAxis), color);

    // 시선이 평면을 스칠수록(수평선 쪽) 부드럽게 사라지게 해 거리와 무관하게 자연스러운 경계를 만든다.
    float grazing = abs(normalize(worldPos - CameraPos).z);
    color.a *= smoothstep(0.0f, 0.05f, grazing);

    if (color.a < 0.003f)
        discard;

    // 교점의 실제 깊이를 기록해 씬 물체에 가려지게 한다. 원평면 너머는 원평면 직전으로 고정한다.
    float4 clip = mul(float4(worldPos, 1.0f), ViewProj);
    o.depth = min(clip.z / clip.w, 0.999999f);
    o.color = color;
    return o;
}
