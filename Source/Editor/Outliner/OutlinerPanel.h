#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <functional>

class UWorld;
class ULevel;
class AActor;
class UActorComponent;
class UPrimitiveComponent;
class USceneComponent;

using SelectionCallback = std::function<void(USceneComponent*)>;
using DeleteActorCallback = std::function<void(AActor*)>;

class FOutlinerPanel : public IEditorPanel
{
public:
    FOutlinerPanel() = default;
    ~FOutlinerPanel() = default;

    void SetSelectionCallback(SelectionCallback InCallback) { Callback = InCallback; }
    void SetDeleteActorCallback(DeleteActorCallback InCallback) { DeleteCallback = InCallback;}

public:
    bool Init() override;
    void Tick(float DeltaTime) override;
    void OnRender() override;
    const char* GetPanelName() const override { return "Outliner"; }

    void SetWorld(UWorld* InWorld) { World = InWorld; };

    void DrawActors(ULevel* Level);
    void DrawActorNode(AActor* Actor);
    void SelectActor(AActor* Actor);

    AActor* GetSelectedActor() const;
	bool IsFocused() const { return bFocused; }


private:
    UWorld* World = nullptr;
    UObject* SelectedObject = nullptr;

    SelectionCallback Callback;
    DeleteActorCallback DeleteCallback;
    AActor* PendingDeleteActor = nullptr;
	bool bFocused = false;
	AActor* RenamingActor = nullptr;
	char RenameBuffer[256] = {};
	bool bFocusRename = false;
	TMap<AActor*, TArray<AActor*>> ActorChildren;
	USceneComponent* PendingAttachChild = nullptr;
	USceneComponent* PendingAttachParent = nullptr;
	bool bPendingDetach = false;

	// 검색어. 이름에 포함되거나(대소문자 무시) 자손 중에 포함된 액터만 보인다.
	char SearchBuffer[128] = {};
	bool IsFiltering() const { return SearchBuffer[0] != '\0'; }
	bool MatchesSearch(AActor* Actor) const;
};
