#include "EnginePCH.h"
#include "Editor/KurisuEd/EditorUndo.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Editor/KurisuEd/EditorHierarchy.h"

#include "Core/EngineLog.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/LocalLightComponent.h"
#include "UObject/UObjectGlobals.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

namespace
{
	void RefreshObjectPostEdit(UObject* Object)
	{
		if (!Object)
		{
			return;
		}

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			for (const FProperty* Prop : (*It)->Properties)
			{
				if (Prop)
				{
					Object->PostEditChangeProperty(*Prop);
				}
			}
		}

		if (USceneComponent* SceneComp = Cast<USceneComponent>(Object))
		{
			SceneComp->MarkTransformDirty();
		}
		if (UActorComponent* ActorComp = Cast<UActorComponent>(Object))
		{
			ActorComp->MarkRenderStateDirty();
		}
	}

	// ---- 1. 기즈모 Transform / LocalLight 반경 변경 ----
	class FTransformUndoCommand final : public IEditorUndoCommand
	{
	public:
		FTransformUndoCommand(FName InActorName, FName InCompName,
			const FTransform& InOld, const FTransform& InNew,
			bool bInLight, float InOldRadius, float InNewRadius)
			: ActorName(InActorName)
			, ComponentName(InCompName)
			, OldTransform(InOld)
			, NewTransform(InNew)
			, bHasLightRadius(bInLight)
			, OldRadius(InOldRadius)
			, NewRadius(InNewRadius)
		{
		}

		const char* GetDescription() const override { return "Transform"; }

		void Undo(UEditorEngine& Editor) override { Apply(Editor, OldTransform, OldRadius); }
		void Redo(UEditorEngine& Editor) override { Apply(Editor, NewTransform, NewRadius); }

	private:
		void Apply(UEditorEngine& Editor, const FTransform& Transform, float Radius)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			USceneComponent* Target = Cast<USceneComponent>(
				EditorUndoUtils::FindComponentByNames(World, ActorName, ComponentName));
			if (!Actor || !Target)
			{
				return;
			}

			Target->SetTransform(Transform);
			if (bHasLightRadius)
			{
				if (ULocalLightComponent* Light = Cast<ULocalLightComponent>(Target))
				{
					Light->SetAttenuationRadius(Radius);
				}
			}
			Editor.SelectActorForUndo(Actor, Target);
		}

		FName ActorName;
		FName ComponentName;
		FTransform OldTransform;
		FTransform NewTransform;
		bool bHasLightRadius = false;
		float OldRadius = 0.0f;
		float NewRadius = 0.0f;
	};

	// ---- 2. 단일 UObject(Actor 또는 Component) 직렬화 변경 ----
	class FObjectBytesUndoCommand final : public IEditorUndoCommand
	{
	public:
		FObjectBytesUndoCommand(FString InDesc, FName InActorName, FName InCompName,
			TArray<uint8> InBefore, TArray<uint8> InAfter)
			: Description(std::move(InDesc))
			, ActorName(InActorName)
			, ComponentName(InCompName)
			, BeforeBytes(std::move(InBefore))
			, AfterBytes(std::move(InAfter))
		{
		}

		const char* GetDescription() const override { return Description.c_str(); }
		void Undo(UEditorEngine& Editor) override { Apply(Editor, BeforeBytes); }
		void Redo(UEditorEngine& Editor) override { Apply(Editor, AfterBytes); }

	private:
		void Apply(UEditorEngine& Editor, const TArray<uint8>& Bytes)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				return;
			}

			UActorComponent* Comp = ComponentName.IsNone()
				? nullptr
				: Actor->FindComponentByName(ComponentName);
			UObject* Target = Comp ? static_cast<UObject*>(Comp) : static_cast<UObject*>(Actor);
			EditorUndoUtils::ApplyObjectBytes(Target, Bytes);
			Editor.SelectActorForUndo(Actor, Comp);
		}

		FString Description;
		FName ActorName;
		FName ComponentName;
		TArray<uint8> BeforeBytes;
		TArray<uint8> AfterBytes;
	};

	// ---- 3. 액터 생성 / 삭제 / 복제 ----
	class FActorCreateDeleteUndoCommand final : public IEditorUndoCommand
	{
	public:
		FActorCreateDeleteUndoCommand(FString InDesc, FActorUndoSnapshot InSnapshot, bool bInCreate)
			: Description(std::move(InDesc))
			, Snapshot(std::move(InSnapshot))
			, bIsCreate(bInCreate)
		{
		}

		const char* GetDescription() const override { return Description.c_str(); }

		void Undo(UEditorEngine& Editor) override
		{
			if (bIsCreate) DeleteTarget(Editor);
			else           RestoreTarget(Editor);
		}

		void Redo(UEditorEngine& Editor) override
		{
			if (bIsCreate) RestoreTarget(Editor);
			else           DeleteTarget(Editor);
		}

	private:
		void RestoreTarget(UEditorEngine& Editor)
		{
			AActor* Restored = EditorUndoUtils::RestoreActorFromSnapshot(Editor.GetEditorWorld(), Snapshot);
			if (Restored)
			{
				Editor.SelectActorForUndo(Restored);
			}
		}

		void DeleteTarget(UEditorEngine& Editor)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, Snapshot.ActorName);
			if (!Actor)
			{
				return;
			}

			// 삭제 전 최신 상태로 스냅샷을 갱신해 두면 이후 복원 시 정확하다.
			EditorUndoUtils::CaptureActorSnapshot(Actor, Snapshot);
			Editor.DeleteActorForUndo(Actor);
		}

		FString Description;
		FActorUndoSnapshot Snapshot;
		bool bIsCreate = true;
	};

	// ---- 4. 액터 이름 변경 ----
	class FActorRenameUndoCommand final : public IEditorUndoCommand
	{
	public:
		FActorRenameUndoCommand(FName InOld, FName InNew)
			: OldName(InOld)
			, NewName(InNew)
		{
		}

		const char* GetDescription() const override { return "Rename Actor"; }
		void Undo(UEditorEngine& Editor) override { Rename(Editor, NewName, OldName); }
		void Redo(UEditorEngine& Editor) override { Rename(Editor, OldName, NewName); }

	private:
		void Rename(UEditorEngine& Editor, FName From, FName To)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, From);
			if (!Actor)
			{
				return;
			}
			Actor->SetName(To);
			Editor.SelectActorForUndo(Actor);
		}

		FName OldName;
		FName NewName;
	};

	// ---- 5. Outliner / Details 계층 Attach / Detach ----
	class FHierarchyAttachUndoCommand final : public IEditorUndoCommand
	{
	public:
		FHierarchyAttachUndoCommand(FString InDesc, FName InChildActor, FName InChildComp,
			FName InOldParentActor, FName InOldParentComp,
			FName InNewParentActor, FName InNewParentComp)
			: Description(std::move(InDesc))
			, ChildActorName(InChildActor)
			, ChildCompName(InChildComp)
			, OldParentActorName(InOldParentActor)
			, OldParentCompName(InOldParentComp)
			, NewParentActorName(InNewParentActor)
			, NewParentCompName(InNewParentComp)
		{
		}

		const char* GetDescription() const override { return Description.c_str(); }
		void Undo(UEditorEngine& Editor) override { Apply(Editor, OldParentActorName, OldParentCompName); }
		void Redo(UEditorEngine& Editor) override { Apply(Editor, NewParentActorName, NewParentCompName); }

	private:
		void Apply(UEditorEngine& Editor, FName ParentActorName, FName ParentCompName)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* ChildActor = EditorUndoUtils::FindActorByName(World, ChildActorName);
			USceneComponent* Child = Cast<USceneComponent>(
				EditorUndoUtils::FindComponentByNames(World, ChildActorName, ChildCompName));
			if (!ChildActor || !Child)
			{
				return;
			}

			if (ParentActorName.IsNone() || ParentCompName.IsNone())
			{
				Child->DetachFromParent(EAttachmentRule::KeepWorld);
			}
			else
			{
				USceneComponent* Parent = Cast<USceneComponent>(
					EditorUndoUtils::FindComponentByNames(World, ParentActorName, ParentCompName));
				if (Parent)
				{
					EditorHierarchy::Attach(Child, Parent);
				}
			}
			Editor.SelectActorForUndo(ChildActor, Child);
		}

		FString Description;
		FName ChildActorName;
		FName ChildCompName;
		FName OldParentActorName;
		FName OldParentCompName;
		FName NewParentActorName;
		FName NewParentCompName;
	};

	// ---- 6. 컴포넌트 추가 / 삭제 ----
	class FComponentAddRemoveUndoCommand final : public IEditorUndoCommand
	{
	public:
		FComponentAddRemoveUndoCommand(FString InDesc, FName InActorName, FName InCompName,
			FString InClassName, FName InParentCompName, TArray<FName> InChildNames,
			TArray<uint8> InCompBytes, bool bInAdd)
			: Description(std::move(InDesc))
			, ActorName(InActorName)
			, ComponentName(InCompName)
			, ClassName(std::move(InClassName))
			, ParentCompName(InParentCompName)
			, ChildCompNames(std::move(InChildNames))
			, ComponentBytes(std::move(InCompBytes))
			, bIsAdd(bInAdd)
		{
		}

		const char* GetDescription() const override { return Description.c_str(); }

		void Undo(UEditorEngine& Editor) override
		{
			if (bIsAdd) RemoveComp(Editor);
			else        AddComp(Editor);
		}

		void Redo(UEditorEngine& Editor) override
		{
			if (bIsAdd) AddComp(Editor);
			else        RemoveComp(Editor);
		}

	private:
		void AddComp(UEditorEngine& Editor)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			UClass* Class = FindClass(ClassName);
			if (!Actor || !Class)
			{
				return;
			}

			UActorComponent* Comp = Actor->AddComponent(Class, ComponentName);
			if (!Comp)
			{
				return;
			}
			if (!ComponentBytes.IsEmpty())
			{
				EditorUndoUtils::ApplyObjectBytes(Comp, ComponentBytes);
			}
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
			{
				if (!ParentCompName.IsNone())
				{
					if (USceneComponent* Parent = Cast<USceneComponent>(Actor->FindComponentByName(ParentCompName)))
					{
						SceneComp->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
					}
				}
				for (const FName& ChildName : ChildCompNames)
				{
					if (USceneComponent* Child = Cast<USceneComponent>(Actor->FindComponentByName(ChildName)))
					{
						Child->SetupAttachment(SceneComp, EAttachmentRule::KeepWorld);
					}
				}
			}
			Editor.SelectActorForUndo(Actor, Comp);
		}

		void RemoveComp(UEditorEngine& Editor)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			UActorComponent* Comp = Actor ? Actor->FindComponentByName(ComponentName) : nullptr;
			if (!Actor || !Comp || Comp == Actor->GetRootComponent())
			{
				return;
			}

			EditorUndoUtils::CaptureObjectBytes(Comp, ComponentBytes);
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
			{
				USceneComponent* Parent = SceneComp->GetAttachParent();
				ParentCompName = (Parent && Parent->GetOwner() == Actor) ? Parent->GetFName() : FName();
				ChildCompNames.Reset();
				for (USceneComponent* Child : SceneComp->GetAttachChildren())
				{
					if (Child && Child->GetOwner() == Actor)
					{
						ChildCompNames.Add(Child->GetFName());
					}
				}
			}

			Editor.DeleteComponentForUndo(Comp);
			Editor.SelectActorForUndo(Actor, Actor->GetRootComponent());
		}

		FString Description;
		FName ActorName;
		FName ComponentName;
		FString ClassName;
		FName ParentCompName;
		TArray<FName> ChildCompNames;
		TArray<uint8> ComponentBytes;
		bool bIsAdd = true;
	};

	// ---- 7. 컴포넌트 이름 변경 ----
	class FComponentRenameUndoCommand final : public IEditorUndoCommand
	{
	public:
		FComponentRenameUndoCommand(FName InActorName, FName InOldCompName, FName InNewCompName)
			: ActorName(InActorName)
			, OldCompName(InOldCompName)
			, NewCompName(InNewCompName)
		{
		}

		const char* GetDescription() const override { return "Rename Component"; }
		void Undo(UEditorEngine& Editor) override { Rename(Editor, NewCompName, OldCompName); }
		void Redo(UEditorEngine& Editor) override { Rename(Editor, OldCompName, NewCompName); }

	private:
		void Rename(UEditorEngine& Editor, FName From, FName To)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			UActorComponent* Comp = Actor ? Actor->FindComponentByName(From) : nullptr;
			if (!Actor || !Comp)
			{
				return;
			}
			Comp->SetName(To);
			Editor.SelectActorForUndo(Actor, Comp);
		}

		FName ActorName;
		FName OldCompName;
		FName NewCompName;
	};

	// ---- 8. Make Root Component ----
	class FMakeRootComponentUndoCommand final : public IEditorUndoCommand
	{
	public:
		FMakeRootComponentUndoCommand(FName InActorName, FName InOldRoot, FName InNewRoot,
			FName InExtParentActor, FName InExtParentComp)
			: ActorName(InActorName)
			, OldRootName(InOldRoot)
			, NewRootName(InNewRoot)
			, ExternalParentActorName(InExtParentActor)
			, ExternalParentCompName(InExtParentComp)
		{
		}

		const char* GetDescription() const override { return "Make Root Component"; }

		void Undo(UEditorEngine& Editor) override { SwitchRoot(Editor, OldRootName); }
		void Redo(UEditorEngine& Editor) override { SwitchRoot(Editor, NewRootName); }

	private:
		void SwitchRoot(UEditorEngine& Editor, FName TargetRootName)
		{
			UWorld* World = Editor.GetEditorWorld();
			AActor* Actor = EditorUndoUtils::FindActorByName(World, ActorName);
			USceneComponent* TargetRoot = Actor ? Cast<USceneComponent>(Actor->FindComponentByName(TargetRootName)) : nullptr;
			if (!Actor || !TargetRoot)
			{
				return;
			}

			if (Actor->MakeRootComponent(TargetRoot) && !ExternalParentActorName.IsNone())
			{
				if (USceneComponent* ExtParent = Cast<USceneComponent>(
					EditorUndoUtils::FindComponentByNames(World, ExternalParentActorName, ExternalParentCompName)))
				{
					TargetRoot->SetupAttachment(ExtParent, EAttachmentRule::KeepWorld);
				}
			}
			Editor.SelectActorForUndo(Actor, TargetRoot);
		}

		FName ActorName;
		FName OldRootName;
		FName NewRootName;
		FName ExternalParentActorName;
		FName ExternalParentCompName;
	};

	// ---- 9. 복합 커맨드 (다중 선택 변환/삭제/복제/편집) ----
	class FCompositeUndoCommand final : public IEditorUndoCommand
	{
	public:
		FCompositeUndoCommand(
			FString InDesc,
			TArray<TUniquePtr<IEditorUndoCommand>> InCommands,
			TArray<FName> InUndoSelectActors,
			TArray<FName> InRedoSelectActors)
			: Description(std::move(InDesc))
			, Commands(std::move(InCommands))
			, UndoSelectActors(std::move(InUndoSelectActors))
			, RedoSelectActors(std::move(InRedoSelectActors))
		{
		}

		const char* GetDescription() const override { return Description.c_str(); }

		void Undo(UEditorEngine& Editor) override
		{
			for (int32 i = Commands.Num() - 1; i >= 0; --i)
			{
				if (Commands[i])
				{
					Commands[i]->Undo(Editor);
				}
			}
			ApplySelection(Editor, UndoSelectActors);
		}

		void Redo(UEditorEngine& Editor) override
		{
			for (int32 i = 0; i < Commands.Num(); ++i)
			{
				if (Commands[i])
				{
					Commands[i]->Redo(Editor);
				}
			}
			ApplySelection(Editor, RedoSelectActors);
		}

	private:
		static void ApplySelection(UEditorEngine& Editor, const TArray<FName>& Names)
		{
			if (Names.IsEmpty())
			{
				return;
			}
			UWorld* World = Editor.GetEditorWorld();
			TArray<AActor*> Actors;
			for (const FName& Name : Names)
			{
				if (AActor* Actor = EditorUndoUtils::FindActorByName(World, Name))
				{
					Actors.Add(Actor);
				}
			}
			if (!Actors.IsEmpty())
			{
				Editor.SelectActorsForUndo(Actors);
			}
		}

		FString Description;
		TArray<TUniquePtr<IEditorUndoCommand>> Commands;
		TArray<FName> UndoSelectActors;
		TArray<FName> RedoSelectActors;
	};
}

void FEditorUndoHistory::Push(TUniquePtr<IEditorUndoCommand> Command)
{
	if (!Command || !CanRecord())
	{
		return;
	}

	UndoStack.Add(std::move(Command));
	if (UndoStack.Num() > MaxHistorySteps)
	{
		UndoStack.RemoveAt(0);
	}
	RedoStack.Empty();
}

const char* FEditorUndoHistory::GetUndoDescription() const
{
	return UndoStack.IsEmpty() ? "" : UndoStack[UndoStack.Num() - 1]->GetDescription();
}

const char* FEditorUndoHistory::GetRedoDescription() const
{
	return RedoStack.IsEmpty() ? "" : RedoStack[RedoStack.Num() - 1]->GetDescription();
}

void FEditorUndoHistory::Undo(UEditorEngine& Editor)
{
	if (UndoStack.IsEmpty() || Editor.IsPlaying())
	{
		return;
	}

	TUniquePtr<IEditorUndoCommand> Command = std::move(UndoStack[UndoStack.Num() - 1]);
	UndoStack.RemoveAt(UndoStack.Num() - 1);

	{
		FScopedSuppress Suppress(*this);
		Command->Undo(Editor);
	}
	RedoStack.Add(std::move(Command));
}

void FEditorUndoHistory::Redo(UEditorEngine& Editor)
{
	if (RedoStack.IsEmpty() || Editor.IsPlaying())
	{
		return;
	}

	TUniquePtr<IEditorUndoCommand> Command = std::move(RedoStack[RedoStack.Num() - 1]);
	RedoStack.RemoveAt(RedoStack.Num() - 1);

	{
		FScopedSuppress Suppress(*this);
		Command->Redo(Editor);
	}
	UndoStack.Add(std::move(Command));
}

void FEditorUndoHistory::Clear()
{
	UndoStack.Empty();
	RedoStack.Empty();
}

namespace EditorUndoUtils
{
	AActor* FindActorByName(UWorld* World, FName ActorName)
	{
		if (!World || ActorName.IsNone())
		{
			return nullptr;
		}
		ULevel* Level = World->GetCurrentLevel();
		if (!Level)
		{
			return nullptr;
		}
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && Actor->GetFName() == ActorName)
			{
				return Actor;
			}
		}
		return nullptr;
	}

	UActorComponent* FindComponentByNames(UWorld* World, FName ActorName, FName ComponentName)
	{
		AActor* Actor = FindActorByName(World, ActorName);
		if (!Actor)
		{
			return nullptr;
		}
		return ComponentName.IsNone() ? Actor->GetRootComponent() : Actor->FindComponentByName(ComponentName);
	}

	bool CaptureObjectBytes(UObject* Object, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		if (!Object)
		{
			return false;
		}

		FMemoryWriter Writer(OutBytes);
		Writer.SetIsPersistent(true);
		{
			FJsonArchiveOutputFormatter Formatter(Writer);
			FStructuredArchive Ar(Formatter);
			FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
			FStructuredArchive::FRecord ObjRecord = Root.EnterRecord("Object");
			if (Cast<AActor>(Object))
			{
				// 액터 자체의 UPROPERTY만 저장한다(컴포넌트 목록은 별도 커맨드가 담당).
				Object->UObject::Serialize(ObjRecord);
			}
			else
			{
				Object->Serialize(ObjRecord);
			}
			Ar.Close();
		}
		return !Writer.IsError();
	}

	bool ApplyObjectBytes(UObject* Object, const TArray<uint8>& Bytes)
	{
		if (!Object || Bytes.IsEmpty())
		{
			return false;
		}

		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
		{
			return false;
		}

		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		FStructuredArchive::FRecord ObjRecord = Root.EnterRecord("Object");
		if (Cast<AActor>(Object))
		{
			Object->UObject::Serialize(ObjRecord);
		}
		else
		{
			Object->Serialize(ObjRecord);
		}
		Ar.Close();

		RefreshObjectPostEdit(Object);
		return !Reader.IsError();
	}

	bool CaptureActorSnapshot(AActor* Actor, FActorUndoSnapshot& OutSnapshot)
	{
		OutSnapshot = {};
		if (!Actor || !Actor->GetClass())
		{
			return false;
		}

		OutSnapshot.ClassName = Actor->GetClass()->Name;
		OutSnapshot.ActorName = Actor->GetFName();

		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			if (USceneComponent* Parent = Root->GetAttachParent())
			{
				if (AActor* ParentOwner = Parent->GetOwner(); ParentOwner && ParentOwner != Actor)
				{
					OutSnapshot.ParentActorName = ParentOwner->GetFName();
					OutSnapshot.ParentComponentName = Parent->GetFName();
				}
			}
		}

		for (UActorComponent* Owned : Actor->GetComponents())
		{
			if (USceneComponent* Scene = Cast<USceneComponent>(Owned))
			{
				for (USceneComponent* Child : Scene->GetAttachChildren())
				{
					if (Child && Child->GetOwner() && Child->GetOwner() != Actor &&
						Child == Child->GetOwner()->GetRootComponent())
					{
						OutSnapshot.ChildActors.Add({ Child->GetOwner()->GetFName(), Scene->GetFName() });
					}
				}
			}
		}

		FMemoryWriter Writer(OutSnapshot.ActorBytes);
		Writer.SetIsPersistent(true);
		{
			FJsonArchiveOutputFormatter Formatter(Writer);
			FStructuredArchive Ar(Formatter);
			FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
			Actor->Serialize(Root.EnterRecord("Actor"));
			Ar.Close();
		}
		return !Writer.IsError();
	}

	AActor* RestoreActorFromSnapshot(UWorld* World, const FActorUndoSnapshot& Snapshot)
	{
		if (!World || Snapshot.ClassName.empty())
		{
			return nullptr;
		}

		UClass* Class = FindClass(Snapshot.ClassName);
		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
		{
			return nullptr;
		}

		ULevel* Level = World->GetCurrentLevel();
		FName SpawnName = Snapshot.ActorName;
		if (!SpawnName.IsNone() && Level && StaticFindObjectFast(nullptr, Level, SpawnName))
		{
			SpawnName = NAME_None;
		}

		AActor* Actor = World->SpawnActor(Class, SpawnName);
		if (!Actor)
		{
			return nullptr;
		}

		if (!Snapshot.ActorBytes.IsEmpty())
		{
			FMemoryReader Reader(Snapshot.ActorBytes, true);
			FJsonArchiveInputFormatter Formatter(Reader);
			if (!Reader.IsError())
			{
				FStructuredArchive Ar(Formatter);
				FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
				Actor->Serialize(Root.EnterRecord("Actor"));
				Ar.Close();
			}
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			RefreshObjectPostEdit(Comp);
		}

		if (!Snapshot.ParentActorName.IsNone() && Actor->GetRootComponent())
		{
			if (USceneComponent* Parent = Cast<USceneComponent>(
				FindComponentByNames(World, Snapshot.ParentActorName, Snapshot.ParentComponentName)))
			{
				Actor->GetRootComponent()->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
			}
		}

		for (const FExternalChildLink& Link : Snapshot.ChildActors)
		{
			if (AActor* ChildActor = FindActorByName(World, Link.ChildActorName))
			{
				USceneComponent* ParentComp = Cast<USceneComponent>(Actor->FindComponentByName(Link.ParentComponentName));
				if (ParentComp && ChildActor->GetRootComponent())
				{
					ChildActor->GetRootComponent()->SetupAttachment(ParentComp, EAttachmentRule::KeepWorld);
				}
			}
		}

		World->GetScene().BuildBVH();
		return Actor;
	}

	TUniquePtr<IEditorUndoCommand> MakeTransformCommand(
		USceneComponent* Target,
		const FTransform& OldTransform,
		const FTransform& NewTransform,
		bool bHasLightRadius,
		float OldRadius,
		float NewRadius)
	{
		if (!Target || !Target->GetOwner())
		{
			return nullptr;
		}
		return MakeUnique<FTransformUndoCommand>(
			Target->GetOwner()->GetFName(),
			Target->GetFName(),
			OldTransform,
			NewTransform,
			bHasLightRadius,
			OldRadius,
			NewRadius);
	}

	TUniquePtr<IEditorUndoCommand> MakeObjectBytesCommand(
		const char* Description,
		FName ActorName,
		FName ComponentName,
		TArray<uint8> BeforeBytes,
		TArray<uint8> AfterBytes)
	{
		return MakeUnique<FObjectBytesUndoCommand>(
			Description ? Description : "Edit Property",
			ActorName,
			ComponentName,
			std::move(BeforeBytes),
			std::move(AfterBytes));
	}

	TUniquePtr<IEditorUndoCommand> MakeActorCreateDeleteCommand(
		const char* Description,
		FActorUndoSnapshot Snapshot,
		bool bIsCreate)
	{
		return MakeUnique<FActorCreateDeleteUndoCommand>(
			Description ? Description : (bIsCreate ? "Create Actor" : "Delete Actor"),
			std::move(Snapshot),
			bIsCreate);
	}

	TUniquePtr<IEditorUndoCommand> MakeActorRenameCommand(FName OldName, FName NewName)
	{
		return MakeUnique<FActorRenameUndoCommand>(OldName, NewName);
	}

	TUniquePtr<IEditorUndoCommand> MakeHierarchyAttachCommand(
		const char* Description,
		FName ChildActorName,
		FName ChildCompName,
		FName OldParentActorName,
		FName OldParentCompName,
		FName NewParentActorName,
		FName NewParentCompName)
	{
		return MakeUnique<FHierarchyAttachUndoCommand>(
			Description ? Description : "Change Attachment",
			ChildActorName,
			ChildCompName,
			OldParentActorName,
			OldParentCompName,
			NewParentActorName,
			NewParentCompName);
	}

	TUniquePtr<IEditorUndoCommand> MakeComponentAddRemoveCommand(
		const char* Description,
		FName ActorName,
		FName ComponentName,
		FString ClassName,
		FName ParentCompName,
		TArray<FName> ChildCompNames,
		TArray<uint8> ComponentBytes,
		bool bIsAdd)
	{
		return MakeUnique<FComponentAddRemoveUndoCommand>(
			Description ? Description : (bIsAdd ? "Add Component" : "Delete Component"),
			ActorName,
			ComponentName,
			std::move(ClassName),
			ParentCompName,
			std::move(ChildCompNames),
			std::move(ComponentBytes),
			bIsAdd);
	}

	TUniquePtr<IEditorUndoCommand> MakeComponentRenameCommand(
		FName ActorName,
		FName OldCompName,
		FName NewCompName)
	{
		return MakeUnique<FComponentRenameUndoCommand>(ActorName, OldCompName, NewCompName);
	}

	TUniquePtr<IEditorUndoCommand> MakeRootComponentCommand(
		FName ActorName,
		FName OldRootName,
		FName NewRootName,
		FName ExternalParentActorName,
		FName ExternalParentCompName)
	{
		return MakeUnique<FMakeRootComponentUndoCommand>(
			ActorName,
			OldRootName,
			NewRootName,
			ExternalParentActorName,
			ExternalParentCompName);
	}

	TUniquePtr<IEditorUndoCommand> MakeCompositeCommand(
		const char* Description,
		TArray<TUniquePtr<IEditorUndoCommand>> Commands,
		TArray<FName> UndoSelectActorNames,
		TArray<FName> RedoSelectActorNames)
	{
		return MakeUnique<FCompositeUndoCommand>(
			Description ? Description : "Multi Edit",
			std::move(Commands),
			std::move(UndoSelectActorNames),
			std::move(RedoSelectActorNames));
	}
}
