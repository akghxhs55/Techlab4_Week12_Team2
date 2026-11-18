// FVertex + FParticleMeshInstance. 인스턴스 행렬이 정점을 월드까지 옮기므로 World/NormalMatrix는 쓰지 않는다
#include "ParticleInstanceCommon.hlsli"

struct FVertexFactoryInput
{
    float3 Position           : POSITION;
    float3 Normal             : NORMAL;
    float4 Tangent            : TANGENT;
    float2 UV                 : TEXCOORD;
    float4 Color              : COLOR;
    float4 InstanceTransform0 : INSTANCE_TRANSFORM0;
    float4 InstanceTransform1 : INSTANCE_TRANSFORM1;
    float4 InstanceTransform2 : INSTANCE_TRANSFORM2;
    float4 InstanceColor      : INSTANCE_COLOR;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    float3 Position = Input.Position;
    float3 Normal = Input.Normal;
    float4 Tangent = Input.Tangent;
    ParticleInstanceVertex(Input.InstanceTransform0, Input.InstanceTransform1, Input.InstanceTransform2,
        Position, Normal, Tangent);

    FVFIntermediates Result;
    Result.WorldPosition = Position;
    Result.WorldNormal = Normal;
    Result.WorldTangent = Tangent;
    Result.UV = Input.UV;
    Result.Color = Input.Color * Input.InstanceColor;
    return Result;
}
