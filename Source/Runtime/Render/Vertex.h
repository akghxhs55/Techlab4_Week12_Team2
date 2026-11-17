#pragma once

#include <d3d11.h>
#include "Math/Color.h"

inline constexpr int32 MaxBoneInfluences = 4;


// 입력 레이아웃은 VertexFactory.cpp가 offsetof로 만든다
struct FVertex
{
	FVector Position = FVector(); // 0
	FVector Normal = FVector(); // 12
	FVector4 Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f); // 24  w = B 부호
	FVector2 UV = FVector2(); // 40
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 48
};

struct FGpuSkinVertex
{
	FVertex Vertex; // 0
	uint32 BoneIndex[MaxBoneInfluences] = {}; // 64  uint4 BlendIndices
	float Weight[MaxBoneInfluences] = {}; // 80  float4 BlendWeights
};
static_assert(sizeof(FGpuSkinVertex) == 96);

// Transform0~2 = 파티클 World 행렬(row-vector)의 열 0~2, w에 이동값
struct FParticleMeshInstance
{
	FVector4 Transform0;
	FVector4 Transform1;
	FVector4 Transform2;
	FLinearColor Color;
};
static_assert(sizeof(FParticleMeshInstance) == 64);


struct FLineBatchVertex
{
	FVector Position = FVector();
	FVector2 UV = FVector2();
	FVector4 Color = FVector4();
};

struct FTextVertex
{
	FVector Position;
	FVector2 UV;
};

// 빌보드 쿼드(BillboardQuad) 정점
struct FBillboardVertex
{
	FVector Position;
	FVector2 UV;
};
