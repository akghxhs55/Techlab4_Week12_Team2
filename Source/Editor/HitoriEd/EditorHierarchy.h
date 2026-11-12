#pragma once

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace EditorHierarchy
{
	inline bool CanAttach(USceneComponent* Child, USceneComponent* Parent)
	{
		if (!Child || !Parent || Child == Parent || Child->GetAttachParent() == Parent) return false;
		AActor* ChildOwner = Child->GetOwner();
		AActor* ParentOwner = Parent->GetOwner();
		if (!ChildOwner || !ParentOwner || ChildOwner->GetWorld() != ParentOwner->GetWorld() ||
			ChildOwner->GetLevel() != ParentOwner->GetLevel()) return false;
		// Only actor roots can cross actor boundaries. Ordinary components keep their owner.
		if (ChildOwner != ParentOwner && Child != ChildOwner->GetRootComponent()) return false;
		if (ChildOwner == ParentOwner && Child == ChildOwner->GetRootComponent()) return false;
		if (fabsf(Parent->GetWorldMatrix().Determinant()) < 1e-12f) return false;
		for (USceneComponent* Ancestor = Parent; Ancestor; Ancestor = Ancestor->GetAttachParent())
			if (Ancestor == Child) return false;
		return true;
	}

	inline bool Attach(USceneComponent* Child, USceneComponent* Parent)
	{
		if (!CanAttach(Child, Parent)) return false;
		Child->SetupAttachment(Parent, EAttachmentRule::KeepWorld);
		return Child->GetAttachParent() == Parent;
	}

	inline AActor* GetParentActor(AActor* Actor)
	{
		USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
		USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
		return Parent && Parent->GetOwner() != Actor ? Parent->GetOwner() : nullptr;
	}
}
