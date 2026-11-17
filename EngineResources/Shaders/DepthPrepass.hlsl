#pragma pack_matrix(row_major)
#include "VertexFactory.hlsli"

// 기존 메시 경로의 시점·오브젝트 상수 슬롯을 공유한다.
cbuffer ViewConstants : register(b0)
{
    matrix VP;
};

cbuffer WorldConstants : register(b2)
{
    matrix World;
};

// 노멀은 쓰지 않으므로 NormalMatrix 자리에 World를 넘긴다
float4 mainVS(FVertexFactoryInput Input) : SV_POSITION
{
    const FVFIntermediates VF = VF_GetIntermediates(Input, World, World);
    return mul(float4(VF.WorldPosition, 1.0f), VP);
}

void mainPS()
{
    return;
}
