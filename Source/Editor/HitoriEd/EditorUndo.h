#pragma once

#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Core/Types.h"
#include "Math/Transform.h"

class UEditorEngine;
class UWorld;
class AActor;
class UActorComponent;
class USceneComponent;
class UObject;

// 단일 편집 동작의 Undo/Redo 계약. 생포인터 대신 이름으로 대상을 찾아 삭제·복원 후에도 안전하다.
class IEditorUndoCommand
{
public:
	virtual ~IEditorUndoCommand() = default;
	virtual const char* GetDescription() const = 0;
	virtual void Undo(UEditorEngine& Editor) = 0;
	virtual void Redo(UEditorEngine& Editor) = 0;
};

struct FExternalChildLink
{
	FName ChildActorName;
	FName ParentComponentName;
};

struct FActorUndoSnapshot
{
	FString ClassName;
	FName ActorName;
	FName ParentActorName;
	FName ParentComponentName;
	TArray<uint8> ActorBytes;
	TArray<FExternalChildLink> ChildActors;
};

// 에디터 월드의 커맨드 기반 Undo/Redo 히스토리.
class FEditorUndoHistory
{
public:
	static constexpr int32 MaxHistorySteps = 100;

	// Undo/Redo 실행 중 파생 호출이 새 커맨드를 쌓지 않도록 막는 RAII 가드.
	struct FScopedSuppress
	{
		explicit FScopedSuppress(FEditorUndoHistory& InHistory)
			: History(InHistory)
			, bPrev(InHistory.bSuppressRecording)
		{
			History.bSuppressRecording = true;
		}
		~FScopedSuppress()
		{
			History.bSuppressRecording = bPrev;
		}
		FEditorUndoHistory& History;
		bool bPrev;
	};

	void SetRecordingEnabled(bool bEnabled) { bRecordingEnabled = bEnabled; }
	bool CanRecord() const { return bRecordingEnabled && !bSuppressRecording; }
	bool IsRecordingSuppressed() const { return bSuppressRecording; }

	void Push(TUniquePtr<IEditorUndoCommand> Command);

	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }
	const char* GetUndoDescription() const;
	const char* GetRedoDescription() const;

	void Undo(UEditorEngine& Editor);
	void Redo(UEditorEngine& Editor);
	void Clear();

private:
	TArray<TUniquePtr<IEditorUndoCommand>> UndoStack;
	TArray<TUniquePtr<IEditorUndoCommand>> RedoStack;
	bool bRecordingEnabled = true;
	bool bSuppressRecording = false;
};

namespace EditorUndoUtils
{
	AActor* FindActorByName(UWorld* World, FName ActorName);
	UActorComponent* FindComponentByNames(UWorld* World, FName ActorName, FName ComponentName);

	// UObject 자체의 리플렉션 프로퍼티(+컴포넌트 전용 직렬화)만 바이트로 읽고 쓴다.
	bool CaptureObjectBytes(UObject* Object, TArray<uint8>& OutBytes);
	bool ApplyObjectBytes(UObject* Object, const TArray<uint8>& Bytes);

	// 단일 액터(컴포넌트·내부 계층·외부 부모/자식 링크 포함)를 직렬화하거나 복원한다.
	bool CaptureActorSnapshot(AActor* Actor, FActorUndoSnapshot& OutSnapshot);
	AActor* RestoreActorFromSnapshot(UWorld* World, const FActorUndoSnapshot& Snapshot);

	// 커맨드 생성 도우미
	TUniquePtr<IEditorUndoCommand> MakeTransformCommand(
		USceneComponent* Target,
		const FTransform& OldTransform,
		const FTransform& NewTransform,
		bool bHasLightRadius = false,
		float OldRadius = 0.0f,
		float NewRadius = 0.0f);

	TUniquePtr<IEditorUndoCommand> MakeObjectBytesCommand(
		const char* Description,
		FName ActorName,
		FName ComponentName,
		TArray<uint8> BeforeBytes,
		TArray<uint8> AfterBytes);

	TUniquePtr<IEditorUndoCommand> MakeActorCreateDeleteCommand(
		const char* Description,
		FActorUndoSnapshot Snapshot,
		bool bIsCreate);

	TUniquePtr<IEditorUndoCommand> MakeActorRenameCommand(
		FName OldName,
		FName NewName);

	TUniquePtr<IEditorUndoCommand> MakeHierarchyAttachCommand(
		const char* Description,
		FName ChildActorName,
		FName ChildCompName,
		FName OldParentActorName,
		FName OldParentCompName,
		FName NewParentActorName,
		FName NewParentCompName);

	TUniquePtr<IEditorUndoCommand> MakeComponentAddRemoveCommand(
		const char* Description,
		FName ActorName,
		FName ComponentName,
		FString ClassName,
		FName ParentCompName,
		TArray<FName> ChildCompNames,
		TArray<uint8> ComponentBytes,
		bool bIsAdd);

	TUniquePtr<IEditorUndoCommand> MakeComponentRenameCommand(
		FName ActorName,
		FName OldCompName,
		FName NewCompName);

	TUniquePtr<IEditorUndoCommand> MakeRootComponentCommand(
		FName ActorName,
		FName OldRootName,
		FName NewRootName,
		FName ExternalParentActorName,
		FName ExternalParentCompName);

	TUniquePtr<IEditorUndoCommand> MakeCompositeCommand(
		const char* Description,
		TArray<TUniquePtr<IEditorUndoCommand>> Commands,
		TArray<FName> UndoSelectActorNames = {},
		TArray<FName> RedoSelectActorNames = {});
}
