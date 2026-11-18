// GPU 스킨 계산 함수. VF_GpuSkin.hlsli가 포함한다.
#ifndef GPU_SKIN_COMMON_HLSLI
#define GPU_SKIN_COMMON_HLSLI

struct FGpuSkinBoneMatrix
{
    row_major float4x4 Value;
};

StructuredBuffer<FGpuSkinBoneMatrix> GpuSkinRefToLocal : register(t14);

float4x4 GpuSkinCalcBoneMatrix(uint4 BlendIndices, float4 BlendWeights)
{
    float4x4 BoneMatrix = BlendWeights.x * GpuSkinRefToLocal[BlendIndices.x].Value;
    BoneMatrix += BlendWeights.y * GpuSkinRefToLocal[BlendIndices.y].Value;
    BoneMatrix += BlendWeights.z * GpuSkinRefToLocal[BlendIndices.z].Value;
    BoneMatrix += BlendWeights.w * GpuSkinRefToLocal[BlendIndices.w].Value;
    return BoneMatrix;
}

float3 GpuSkinSafeNormalize(float3 V)
{
    const float LengthSquared = dot(V, V);
    return LengthSquared > 1.0e-12f ? V / sqrt(LengthSquared) : V;
}

// 비균등 스케일에서도 면에 수직이 되도록 3x3 부분의 여인수 행렬로 노멀을 옮긴다.
// 미러(행렬식 음수)면 여인수 결과가 뒤집히므로 부호를 되돌린다.
float3 GpuSkinTransformNormal(float4x4 M, float3 N)
{
    const float3 R0 = M[0].xyz;
    const float3 R1 = M[1].xyz;
    const float3 R2 = M[2].xyz;
    const float3 C0 = cross(R1, R2);
    const float3 Result = C0 * N.x + cross(R2, R0) * N.y + cross(R0, R1) * N.z;
    return GpuSkinSafeNormalize(dot(R0, C0) < 0.0f ? -Result : Result);
}

void GpuSkinVertex(uint4 BlendIndices, float4 BlendWeights, inout float3 Position, inout float3 Normal, inout float4 Tangent)
{
    const float4x4 M = GpuSkinCalcBoneMatrix(BlendIndices, BlendWeights);
    Position = mul(float4(Position, 1.0f), M).xyz;
    Normal = GpuSkinTransformNormal(M, Normal);
    Tangent.xyz = GpuSkinSafeNormalize(mul(float4(Tangent.xyz, 0.0f), M).xyz);
}

// 4개 영향 중 Bone에 해당하는 가중치의 합. 같은 본이 두 칸에 들어 있어도 맞게 더한다
float GpuSkinWeightForBone(uint4 BlendIndices, float4 BlendWeights, uint Bone)
{
    return dot(BlendWeights, float4(BlendIndices == Bone));
}

// 0은 어두운 회색, 0보다 크면 파랑(작음), 초록(0.5), 빨강(1).
// SkeletalMeshObject.cpp의 WeightToColor와 같은 값이므로 함께 고친다
float3 GpuSkinWeightColor(float Weight)
{
    if (Weight <= 0.0f)
        return float3(0.08f, 0.08f, 0.10f);
    const float T = min(Weight, 1.0f);
    const float3 Low = float3(0.0f, 0.2f, 1.0f);
    const float3 Mid = float3(0.0f, 1.0f, 0.2f);
    const float3 High = float3(1.0f, 0.1f, 0.0f);
    return T < 0.5f ? lerp(Low, Mid, T * 2.0f) : lerp(Mid, High, T * 2.0f - 1.0f);
}

#endif
