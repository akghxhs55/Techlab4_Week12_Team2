#include "EnginePCH.h"
#include "SkeletalMeshData.h"

#include <cmath>
#include <string>

bool FSkeletalMeshData::Validate(FString& OutError) const
{
	const int32 NumVertices = BindVertices.Num();
	const int32 NumBones = Bones.Num();

	if (NumVertices == 0 || Indices.Num() == 0)
	{
		OutError = "SkeletalMeshData: no vertices or indices";
		return false;
	}
	if (Indices.Num() % 3 != 0)
	{
		OutError = "SkeletalMeshData: index count is not a multiple of 3";
		return false;
	}
	if (SkinWeight.Num() != NumVertices)
	{
		OutError = "SkeletalMeshData: SkinWeight count (" + std::to_string(SkinWeight.Num())
			+ ") != vertex count (" + std::to_string(NumVertices) + ")";
		return false;
	}
	if (NumBones == 0 || NumBones > 256)
	{
		// FSkinWeight::BoneIndex가 uint8이라 256개까지
		OutError = "SkeletalMeshData: bone count must be 1..256, got " + std::to_string(NumBones);
		return false;
	}

	for (const uint32 Index : Indices)
	{
		if (Index >= static_cast<uint32>(NumVertices))
		{
			OutError = "SkeletalMeshData: index " + std::to_string(Index) + " out of range";
			return false;
		}
	}

	// 부모가 항상 앞 인덱스여야 RefToLocal을 한 번의 순회로 계산할 수 있다
	for (int32 I = 0; I < NumBones; ++I)
	{
		const int32 Parent = Bones[I].ParentIndex;
		if (Parent >= I || Parent < -1)
		{
			OutError = "SkeletalMeshData: bone " + std::to_string(I) + " (" + Bones[I].Name + ") has invalid parent " + std::to_string(Parent);
			return false;
		}
	}

	for (int32 V = 0; V < NumVertices; ++V)
	{
		float Sum = 0.0f;
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
		{
			const FSkinWeight& W = SkinWeight[V];
			if (W.Weight[K] > 0.0f && W.BoneIndex[K] >= NumBones)
			{
				OutError = "SkeletalMeshData: vertex " + std::to_string(V) + " references bone " + std::to_string(W.BoneIndex[K]);
				return false;
			}
			Sum += W.Weight[K];
		}
		if (std::fabs(Sum - 1.0f) > 1e-3f)
		{
			OutError = "SkeletalMeshData: vertex " + std::to_string(V) + " weights sum to " + std::to_string(Sum) + " (expected 1)";
			return false;
		}
	}

	for (const FSkeletalMeshSection& Section : Sections)
	{
		if (static_cast<uint64>(Section.StartIndex) + Section.IndexCount > static_cast<uint64>(Indices.Num()))
		{
			OutError = "SkeletalMeshData: section range exceeds index count";
			return false;
		}
		if (Section.MaterialSlotIndex >= static_cast<uint32>(MaterialSlots.Num()))
		{
			OutError = "SkeletalMeshData: section material slot " + std::to_string(Section.MaterialSlotIndex) + " out of range";
			return false;
		}
	}

	return true;
}
