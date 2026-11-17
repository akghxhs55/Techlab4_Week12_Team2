#pragma once

#include "Render/SkeletalMesh.h"
#include "Render/VertexFactory.h"

class FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObject(USkeletalMesh* InMesh);
	virtual ~FSkeletalMeshObject() = default;

	virtual void Update(const TArray<FMatrix>& ComponentSpaceTransforms) = 0;
	// CPU 스킨은 이미 스키닝된 FVertex라 Local, GPU 스킨은 본 버퍼를 함께 묶는 GpuSkin
	virtual const FVertexFactory* GetVertexFactory(uint32 LOD) const = 0;

	FIndexBuffer* GetIndexBuffer(uint32 LOD) const
	{
		return SkeletalMesh->GetRenderData().LODRenderData[LOD].IndexBuffer.get();
	}

	const FBox& GetBounds() const { return Bounds; }
	virtual const TArray<FVertex>* GetCPUSkinnedVertices() const { return nullptr; }
	// GPU 스킨 경로만 돌려준다. CPU 경로는 정점이 이미 스키닝되어 있어 nullptr
	virtual const FStructuredBuffer* GetBoneMatrixBuffer() const { return nullptr; }
	// 이번 포즈가 Bounds 밖으로 나가면 여유를 두고 넓히고 true를 돌려준다
	bool UpdateBounds();
	void SetDebugWeightBone(int32 BoneIndex) { DebugWeightBone = BoneIndex; }

protected:
	void UpdateRefToLocalMatrices(const TArray<FMatrix>& ComponentSpaceTransforms);
	void SkinVerticesCPU(TArray<FVertex>& OutVertices, FBox& OutBounds) const;

	int32 DebugWeightBone = -1;

	USkeletalMesh* SkeletalMesh;
	TArray<FMatrix> RefToLocal;
	FBox Bounds;
	FBox SkinnedBounds;
};

class FSkeletalMeshObjectCPUSkin : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectCPUSkin(USkeletalMesh* InMesh);

	void Update(const TArray<FMatrix>& ComponentSpaceTransforms) override;
	const FVertexFactory* GetVertexFactory(uint32 LOD) const override { return &VertexFactory; }
	const TArray<FVertex>* GetCPUSkinnedVertices() const override { return &SkinnedVertices; }

private:
	void UploadVertices();

	TUniquePtr<FVertexBuffer> FinalVertexBuffer;
	FLocalVertexFactory VertexFactory;
	TArray<FVertex> SkinnedVertices;
};

// GPU 스킨 경로: 정점은 움직이지 않고, 패스의 버텍스 셰이더가 본 행렬을 섞어 스키닝한다.
class FSkeletalMeshObjectGPUSkin : public FSkeletalMeshObject
{
public:
	explicit FSkeletalMeshObjectGPUSkin(USkeletalMesh* InMesh);

	bool IsValid() const;
	void Update(const TArray<FMatrix>& ComponentSpaceTransforms) override;
	const FVertexFactory* GetVertexFactory(uint32 LOD) const override { return &VertexFactory; }
	const FStructuredBuffer* GetBoneMatrixBuffer() const override { return BoneMatrixBuffer.get(); }
	// 피킹처럼 CPU에서 정점이 필요할 때만 현재 포즈로 계산한다
	const TArray<FVertex>* GetCPUSkinnedVertices() const override;

private:
	void UpdateSkinnedBoundsFromBones();
	void UploadBoneMatrices();

	TUniquePtr<FStructuredBuffer> BoneMatrixBuffer;
	FGpuSkinVertexFactory VertexFactory;
	mutable TArray<FVertex> CPUVertices;
	mutable bool bCPUVerticesDirty = false;
};
