#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include <unordered_map>
#include <vector>
#include "Mesh.generated.h"

class FPrimitiveSceneProxy;

UCLASS()
class UStaticMesh : public URenderAsset
{
	GENERATED_BODY()
public:
    UStaticMesh() : SortID(NextSortID++) {}
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    FString SourceObjPath;
    FVertexBuffer* GetVertexBuffer() const { return VertexBuffer.get(); }
    FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
    void RebuildRenderData();
    const FBox& GetRenderBounds() const { return RenderBounds; }
    TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    const uint16 SortID;
private:
    static inline uint16 NextSortID = 0;
    struct FMaterialSlotsHash
    {
        size_t operator()(const std::vector<UMaterial*>& Slots) const;
    };
    FBox RenderBounds;
    std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};
