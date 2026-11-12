#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    for (const FStaticMeshSection& Section : MeshData.Sections)
    {
        UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
            ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
        if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}
