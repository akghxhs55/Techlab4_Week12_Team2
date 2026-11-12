#pragma once
#include "Containers/Array.h"
class UMaterial;
struct FCachedMeshSection
{
    UMaterial* Material;
    uint32 StartIndex;
    uint32 IndexCount;
};
struct FMeshRenderState
{
    TArray<FCachedMeshSection> Sections;
};
