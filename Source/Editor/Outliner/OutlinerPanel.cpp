#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/HitoriEd/EditorHierarchy.h"

bool FOutlinerPanel::Init()
{
    return true;
}

void FOutlinerPanel::Tick(float DeltaTime)
{
}

void FOutlinerPanel::OnRender()
{
    // World 참조 확인
    if (!World)
        return;

    // 현재 Level 가져오기
    ULevel* Level = World->GetCurrentLevel();
    if (!Level)
        return;

    // Level의 Actor 목록 Tree 출력
    ImGui::Begin("Outliner");
    bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (bFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        if (AActor* Actor = GetSelectedActor())
        {
            RenamingActor = Actor;
            strncpy_s(RenameBuffer, Actor->GetName().c_str(), _TRUNCATE);
            bFocusRename = true;
        }

    DrawActors(Level);

    const ImVec2 Space = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##DetachActors", ImVec2(FMath::Max(Space.x, 1.0f), FMath::Max(Space.y, ImGui::GetFrameHeight())));
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            AActor* Child = *static_cast<AActor* const*>(Payload->Data);
            PendingAttachChild = Child->GetRootComponent(); bPendingDetach = true;
        }
        if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
        {
            auto* Child = *static_cast<USceneComponent* const*>(Payload->Data);
            if (Child->GetOwner() && Child == Child->GetOwner()->GetRootComponent())
            { PendingAttachChild = Child; bPendingDetach = true; }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
    if (PendingAttachChild)
    {
        if (bPendingDetach) PendingAttachChild->DetachFromParent(EAttachmentRule::KeepWorld);
        else EditorHierarchy::Attach(PendingAttachChild, PendingAttachParent);
        PendingAttachChild = PendingAttachParent = nullptr; bPendingDetach = false;
    }

    // Actors 순회가 완전히 끝난 뒤 삭제
    if (PendingDeleteActor)
    {
        if (DeleteCallback)
        {
            DeleteCallback(PendingDeleteActor);
        }

        PendingDeleteActor = nullptr;
    }
}

// Level이 가지고 있는 Actor 목록 순회하며 Draw TreeNode
void FOutlinerPanel::DrawActors(ULevel* Level)
{
    if (!Level)
        return;

    const TArray<AActor*>& Actors = Level->GetActors();

    ActorChildren.Reset();
    for (AActor* Actor : Actors)
    {
        AActor* Parent = EditorHierarchy::GetParentActor(Actor);
        if (Parent && Actors.Find(Parent) != INDEX_NONE) ActorChildren[Parent].Add(Actor);
    }
    for (AActor* Actor : Actors)
    {
        AActor* Parent = EditorHierarchy::GetParentActor(Actor);
        if (Actor && (!Parent || Actors.Find(Parent) == INDEX_NONE)) DrawActorNode(Actor);
    }

}

// Actor를 ImGui TreeNode로 출력
void FOutlinerPanel::DrawActorNode(AActor* Actor)
{
    if (!Actor)
        return;

    const FString ActorName = Actor->GetName();

    const auto* Children = ActorChildren.Find(Actor);
    const bool bHasChildren = Children && !Children->IsEmpty();
    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    for (AActor* Ancestor = RenamingActor; Ancestor; Ancestor = EditorHierarchy::GetParentActor(Ancestor))
        if (Ancestor == Actor) { ImGui::SetNextItemOpen(true); break; }

    // Actor 선택 시 UI 선택 상태
    if (SelectedObject == Actor)
        Flags |= ImGuiTreeNodeFlags_Selected;

    const ImVec2 RowStart = ImGui::GetCursorScreenPos();
    const bool bOpen = ImGui::TreeNodeEx(Actor, Flags, "%s", RenamingActor == Actor ? "" : ActorName.c_str());

    // UI에서 Actor 클릭
    // Select on release so dragging an actor does not replace the destination
    // actor's component list in Details halfway through a cross-panel drag.
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload())
    {
        SelectActor(Actor);
    }
	bool bRename = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    if (RenamingActor != Actor && ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(EditorDragDrop::Actor, &Actor, sizeof(Actor));
        ImGui::TextUnformatted(ActorName.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        USceneComponent* Child = nullptr;
        if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
            Child = (*static_cast<AActor* const*>(Payload->Data))->GetRootComponent();
        if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
            Child = *static_cast<USceneComponent* const*>(Payload->Data);
        if (EditorHierarchy::CanAttach(Child, Actor->GetRootComponent()))
        { PendingAttachChild = Child; PendingAttachParent = Actor->GetRootComponent(); bPendingDetach = false; }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem())
    {
		if (ImGui::MenuItem("Rename", "F2")) bRename = true;
        if (ImGui::MenuItem("Detach from Parent", nullptr, false, EditorHierarchy::GetParentActor(Actor) != nullptr))
        { PendingAttachChild = Actor->GetRootComponent(); bPendingDetach = true; }
        if (ImGui::MenuItem("Delete"))
        {
            PendingDeleteActor = Actor; // 여기서는 바로 삭제 X
        }

        ImGui::EndPopup();
    }
	if (bRename)
	{
		SelectActor(Actor);
		RenamingActor = Actor;
		strncpy_s(RenameBuffer, ActorName.c_str(), _TRUNCATE);
		bFocusRename = true;
	}
	if (RenamingActor == Actor)
	{
		ImGui::PushID(Actor);
		ImGui::SetCursorScreenPos(RowStart);
		if (bFocusRename) { ImGui::SetKeyboardFocusHere(); bFocusRename = false; }
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
		const bool bEnter = ImGui::InputText("##RenameActor", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::PopStyleVar();
		const bool bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		if (bEnter || bCancel || ImGui::IsItemDeactivated())
		{
			if (!bCancel && RenameBuffer[0])
			{
				FName Name(RenameBuffer);
				UObject* Existing = FindObjectFast<UObject>(Actor->GetOuter(), Name);
				if (!Existing || Existing == Actor) Actor->SetName(Name);
				else HTR_LOG(Warning, "An actor with this name already exists.");
			}
			RenamingActor = nullptr;
		}
		// InputText advances to the next row itself. Restoring that cursor position
		// without submitting an item triggers ImGui's window-boundary assertion.
		ImGui::PopID();
	}
    if (bOpen && bHasChildren)
    {
        for (AActor* Child : *Children) DrawActorNode(Child);
        ImGui::TreePop();
    }
}

// 선택 Object 변경
void FOutlinerPanel::SelectActor(AActor* Actor)
{
    if (!Actor)
    {
        RenamingActor = nullptr;
        SelectedObject = nullptr;

        if (Callback)
            Callback(nullptr);

        return;
    }

    SelectedObject = Actor;

    HTR_LOG(Info, "{} UUID {} is selected", SelectedObject->GetName(), SelectedObject->GetUUID());

    USceneComponent* Scene = Cast<USceneComponent>(Actor->GetRootComponent());

    if (Callback)
        Callback(Scene);
}


AActor* FOutlinerPanel::GetSelectedActor() const
{
    return Cast<AActor>(SelectedObject);
}
