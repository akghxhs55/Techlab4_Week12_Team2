#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"

#include "Engine/World.h"

struct FTransform;
class FImGuiRenderer;
class FEditorUndoHistory;
using FShadowPreviewQuery = std::function<bool(const ULightComponent*, FTexture2DArray*&, TArray<FLightShadowRenderData>&)>;

using DetailsSelectionCallback = std::function<void(UActorComponent*)>;
using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	void SelectComponent(UActorComponent* Component);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }
	bool IsFocused() const override { return bFocused; }

	void SetTarget(AActor* InTargetActor) 
	{ 
		if (TargetActor != InTargetActor)
		{
			CommitPendingObjectEdit();
			EditingNameComponent = nullptr;
			PendingRootComponent = nullptr;
			PendingDeleteComponent = nullptr;
			PendingDragComponent = PendingDroppedComponent = nullptr;
		}
		TargetActor = InTargetActor;
		TargetComponent = InTargetActor ? InTargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent)
	{
		if (TargetComponent != InTargetComponent)
		{
			CommitPendingObjectEdit();
		}
		TargetComponent = InTargetComponent;
	}

	void SetSelectionCallback(DetailsSelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }
	void SetUndoHistory(FEditorUndoHistory* InHistory) { UndoHistory = InHistory; }

	void SetWorld(UWorld* InWorld)
	{
		if (World != InWorld)
		{
			bPendingEditCommit = false;
			PendingActorBeforeBytes.Empty();
			PendingCompBeforeBytes.Empty();
			TrackedActorName = FName();
			TrackedComponentName = FName();
		}
		World = InWorld;
	}
	const ULightComponent* GetSelectedLight() const;
	void SetShadowPreviewQuery(FShadowPreviewQuery InQuery) { QueryShadowPreview = std::move(InQuery); }
	void SetImGuiRenderer(FImGuiRenderer* InRenderer) { ImGuiRenderer = InRenderer; }

	ImFont* GetCustomFont() { return CustomFont; }

private:
	void DrawShadowPreview();
	void DrawComponentNameEditor(UActorComponent* Component, UActorComponent*& Selected, bool bRequested);
	USceneComponent* PendingRootComponent = nullptr;
	FShadowPreviewQuery QueryShadowPreview;
	FImGuiRenderer* ImGuiRenderer = nullptr;
	float ShadowPreviewContrast = 32.0f;
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent);
	void CommitPendingObjectEdit();

	UWorld* World = nullptr;
	FEditorUndoHistory* UndoHistory = nullptr;
	bool bFocused = false;
	ImFont* CustomFont = nullptr;

	bool bPendingEditCommit = false;
	FName TrackedActorName;
	FName TrackedComponentName;
	TArray<uint8> PendingActorBeforeBytes;
	TArray<uint8> PendingCompBeforeBytes;

	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	DetailsSelectionCallback Callback;
	DetailsDeleteComponentCallback DeleteCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;
	bool bIsEditingName = false;

	USceneComponent* PendingDroppedComponent = nullptr;
	USceneComponent* PendingDragComponent = nullptr;
};

