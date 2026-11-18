cbuffer CameraOverlayConstants : register(b0)
{
    float4 FadeColor; // rgb = 색, a = FadeAmount
    float2 ViewSize;
    float VignetteIntensity; // 0 = 없음. 0.5~1.5가 보기 좋음
    float VignettePower; // 가장자리 경도
    float LetterboxAmount; // 0~1
    float LetterboxAspect; // 2.39 = 시네마스코프
    float SpeedLineIntensity; // 0~1 부스트 방사형 스피드 라인
    float SpeedLineTime;
};

struct FPSInput
{
    float4 Position : SV_POSITION;
};

float SpeedLineHash(float N)
{
    return frac(sin(N * 127.1f + 311.7f) * 43758.5453123f);
}

float EvaluateSpeedLineLayer(float2 CenteredUV, float Radius, float Sectors, float SeedOffset, float Time)
{
    const float Angle = atan2(CenteredUV.y, CenteredUV.x);
    const float NormalizedAngle = (Angle + 3.14159265f) / 6.2831853f;
    const float SectorPos = NormalizedAngle * Sectors;
    const float SectorIdx = floor(SectorPos);
    const float SectorFrac = abs(frac(SectorPos) - 0.5f);

    const float H0 = SpeedLineHash(SectorIdx + SeedOffset);
    const float H1 = SpeedLineHash(SectorIdx * 1.37f + SeedOffset + 19.3f);
    const float H2 = SpeedLineHash(SectorIdx * 2.71f + SeedOffset + 73.1f);

    // 일부 섹터만 활성화해 굵기와 간격이 자연스럽게 달라 보이게 한다.
    if (H0 < 0.34f)
        return 0.0f;

    // 바깥쪽으로 갈수록 약간 더 선명하고 중심부는 비운다.
    const float AngularWidth = lerp(0.025f, 0.075f, H1) * lerp(0.75f, 1.35f, saturate(Radius));
    const float RadialLine = 1.0f - smoothstep(AngularWidth * 0.25f, AngularWidth, SectorFrac);

    // 중심에서 바깥으로 빠르게 흐르는 가느다란 광선
    const float Travel = frac(Radius * (1.25f + H1 * 0.65f) - Time * (2.6f + H2 * 2.2f) + H0);
    const float Streak = smoothstep(0.05f, 0.22f, Travel) * (1.0f - smoothstep(0.45f, 0.92f, Travel));

    const float EdgeMask = smoothstep(0.24f, 0.62f, Radius);
    return RadialLine * Streak * EdgeMask * lerp(0.55f, 1.0f, H2);
}

// ToneMapShader.hlsl과 같은 풀스크린 삼각형 (정점 버퍼 없이 Draw(3))
FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    const float2 UV = Input.Position.xy / ViewSize;
    const float ViewAspect = ViewSize.x / ViewSize.y;

    // 1) Letterbox: 보이는 높이 비율 = ViewAspect / TargetAspect, 나머지를 위아래 반씩
    const float Bar = saturate(0.5f * (1.0f - ViewAspect / LetterboxAspect)) * LetterboxAmount;
    if (UV.y < Bar || UV.y > 1.0f - Bar)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    // 2) Vignette: 화면비 보정한 중심 거리 → 원형 유지 (보정 안 하면 가로로 긴 타원)
    float2 D = (UV - 0.5f) * float2(ViewAspect, 1.0f);
    float V = pow(saturate(1.0f - dot(D, D) * VignetteIntensity), VignettePower);

    // 3) Fade + Vignette를 블렌드 식 하나로 (위 유도)
    const float a = FadeColor.a;
    float Alpha = 1.0f - V * (1.0f - a);
    float3 Src = (Alpha > 1e-4f) ? FadeColor.rgb * (a / Alpha) : float3(0.0f, 0.0f, 0.0f);

    // 4) 부스트 스피드 라인 UI (화면 바깥에서 중심으로 수렴하는 반투명 흰선)
    if (SpeedLineIntensity > 0.001f)
    {
        const float Radius = length(D);
        const float LayerA = EvaluateSpeedLineLayer(D, Radius, 76.0f, 0.0f, SpeedLineTime);
        const float LayerB = EvaluateSpeedLineLayer(D, Radius, 44.0f, 137.0f, SpeedLineTime * 1.18f);
        const float SpeedAlpha = saturate((LayerA + LayerB * 0.8f) * SpeedLineIntensity * 0.55f);
        if (SpeedAlpha > 1e-4f)
        {
            const float3 LineColor = float3(0.95f, 0.97f, 1.0f);
            // LineColor를 기존 Overlay 위에 일반 알파 합성(SrcOver)한다.
            const float OutAlpha = SpeedAlpha + Alpha * (1.0f - SpeedAlpha);
            Src = (OutAlpha > 1e-4f)
                ? (LineColor * SpeedAlpha + Src * Alpha * (1.0f - SpeedAlpha)) / OutAlpha
                : float3(0.0f, 0.0f, 0.0f);
            Alpha = OutAlpha;
        }
    }

    return float4(Src, Alpha);
}