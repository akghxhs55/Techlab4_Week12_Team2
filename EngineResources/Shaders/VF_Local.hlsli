// FVertex
struct FVertexFactoryInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEXCOORD;
    float4 Color    : COLOR;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    return VF_LocalToWorld(Input.Position, Input.Normal, Input.Tangent, Input.UV, Input.Color, World, NormalMatrix);
}
