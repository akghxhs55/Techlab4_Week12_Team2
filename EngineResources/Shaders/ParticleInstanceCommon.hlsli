// 메시 파티클 인스턴스 행렬 계산 함수. VF_MeshParticle.hlsli가 포함한다.
// T0~T2 = 파티클 World 행렬(row-vector)의 열 0~2, w에 이동값
#ifndef PARTICLE_INSTANCE_COMMON_HLSLI
#define PARTICLE_INSTANCE_COMMON_HLSLI

float3 ParticleInstanceSafeNormalize(float3 V)
{
    const float LengthSquared = dot(V, V);
    return LengthSquared > 1.0e-12f ? V / sqrt(LengthSquared) : V;
}

float3 ParticleInstanceTransformPosition(float4 T0, float4 T1, float4 T2, float3 Position)
{
    const float4 P = float4(Position, 1.0f);
    return float3(dot(P, T0), dot(P, T1), dot(P, T2));
}

// 방향은 이동을 빼고 3x3 부분만 곱한다
float3 ParticleInstanceTransformVector(float4 T0, float4 T1, float4 T2, float3 Direction)
{
    return float3(dot(Direction, T0.xyz), dot(Direction, T1.xyz), dot(Direction, T2.xyz));
}

// 비균등 스케일에서도 면에 수직이 되도록 여인수 행렬로 옮긴다
// 3x3 행렬의 행 i = (T0[i], T1[i], T2[i])
float3 ParticleInstanceTransformNormal(float4 T0, float4 T1, float4 T2, float3 Normal)
{
    const float3 R0 = float3(T0.x, T1.x, T2.x);
    const float3 R1 = float3(T0.y, T1.y, T2.y);
    const float3 R2 = float3(T0.z, T1.z, T2.z);
    const float3 C0 = cross(R1, R2);
    const float3 Result = C0 * Normal.x + cross(R2, R0) * Normal.y + cross(R0, R1) * Normal.z;
    // 거울 반전(행렬식 음수)이면 여인수 결과가 뒤집히므로 되돌린다
    return ParticleInstanceSafeNormalize(dot(R0, C0) < 0.0f ? -Result : Result);
}

void ParticleInstanceVertex(float4 T0, float4 T1, float4 T2, inout float3 Position, inout float3 Normal, inout float4 Tangent)
{
    Position = ParticleInstanceTransformPosition(T0, T1, T2, Position);
    Normal = ParticleInstanceTransformNormal(T0, T1, T2, Normal);
    Tangent.xyz = ParticleInstanceSafeNormalize(ParticleInstanceTransformVector(T0, T1, T2, Tangent.xyz));
}

#endif