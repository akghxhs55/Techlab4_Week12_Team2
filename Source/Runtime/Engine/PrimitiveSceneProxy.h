#pragma once

#include "Math/Frustum.h"
#include "Render/MeshRenderData.h"
#include "Render/RenderPacket.h"

class FScene;
class UPrimitiveComponent;
class UStaticMesh;
class UMaterial;
class FRenderQueue;
struct FViewContext;

class FPrimitiveSceneProxy
{
public:
	explicit FPrimitiveSceneProxy(UPrimitiveComponent* InComponent);
	
	virtual ~FPrimitiveSceneProxy();
	virtual void UpdateRenderState();

	virtual bool HasDynamicMeshElements() const { return false; }
	virtual void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View);

	bool IsSkinned() const { return bSkinned; }
	
	void UpdateTransform();
	void OnMeshRenderDataChanged();
	void OnMeshDestroyed();

	UPrimitiveComponent* GetComponent() const { return Component; }
	const FAABB& GetBounds() const { return Bounds; }
	const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
	const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
	UStaticMesh* GetMesh() const { return Mesh; }
	bool IsVisible() const { return bVisible; }
	bool IsShown(bool bGameView) const { return bVisible && !(bGameView && bHiddenInGame); }
	const FMeshRenderState* GetRenderState() const { return RenderState.get(); }
	const FCachedMeshSection& GetSection(uint32 Index) const { return RenderState->Sections[Index]; }
	FScene* GetScene() const { return Scene; }

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceivesDecals = true;
protected:
	bool bSkinned = false;
	TSharedPtr<const FMeshRenderState> RenderState;
	void UpdateCommonFlags();
private:
	friend class FScene;
	FScene* Scene = nullptr;
	UPrimitiveComponent* Component = nullptr;
	UStaticMesh* Mesh = nullptr;
	FMatrix LocalToWorld;
	FMatrix WorldToLocal;
	FAABB Bounds;
	int32 PackedIndex = INDEX_NONE;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
	bool bVisible = true;
	bool bHiddenInGame = false;
};
