#pragma once

#include <d3d11.h>
#include <span>
#include "Core/Types.h"

class FVertexBuffer;
class FIndexBuffer;
class FStructuredBuffer;

inline constexpr uint32 BoneMatrixBufferSlot = 14; // GpuSkinCommon.hlsli의 t14

enum class EVertexFactoryType : uint8
{
	Local,
	GpuSkin,
	MeshParticle,
	SpriteParticle,
	Trail,
	Count
};

struct FVertexFactoryTypeInfo
{
	const char* Name;
	const char* Define;
	// 비어 있으면 전용 셰이더가 입력을 직접 정의한다
	std::span<const D3D11_INPUT_ELEMENT_DESC> Elements;
};

const FVertexFactoryTypeInfo& GetVertexFactoryTypeInfo(EVertexFactoryType Type);

// 정점 스트림의 레이아웃과 바인딩을 맡는다. 패스는 정점 타입을 몰라도 된다
class FVertexFactory
{
public:
	virtual ~FVertexFactory() = default;
	virtual EVertexFactoryType GetType() const = 0;
	virtual void Bind() const;

	FIndexBuffer* GetIndexBuffer() const { return IndexBuffer; }
	bool IsValid() const { return VertexBuffer && IndexBuffer; }

protected:
	FVertexBuffer* VertexBuffer = nullptr;
	FIndexBuffer* IndexBuffer = nullptr;
};

class FLocalVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB) { VertexBuffer = VB; IndexBuffer = IB; }
	EVertexFactoryType GetType() const override { return EVertexFactoryType::Local; }
};

class FGpuSkinVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB, const FStructuredBuffer* Bones)
	{
		VertexBuffer = VB;
		IndexBuffer = IB;
		BoneBuffer = Bones;
	}
	EVertexFactoryType GetType() const override { return EVertexFactoryType::GpuSkin; }
	void Bind() const override;

private:
	const FStructuredBuffer* BoneBuffer = nullptr;
};

class FInstancedVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB, FVertexBuffer* Instances)
	{
		VertexBuffer = VB;
		IndexBuffer = IB;
		InstanceBuffer = Instances;
	}
	void Bind() const override;

protected:
	FVertexBuffer* InstanceBuffer = nullptr;
};

class FMeshParticleVertexFactory : public FInstancedVertexFactory
{
public:
	EVertexFactoryType GetType() const override { return EVertexFactoryType::MeshParticle; }
};

class FSpriteParticleVertexFactory : public FInstancedVertexFactory
{
public:
	EVertexFactoryType GetType() const override { return EVertexFactoryType::SpriteParticle; }
};

// 빔, 리본 공용. 정점이 이미 월드 좌표이고 인스턴싱하지 않는다
class FTrailVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB) { VertexBuffer = VB; IndexBuffer = IB; }
	EVertexFactoryType GetType() const override { return EVertexFactoryType::Trail; }
};
