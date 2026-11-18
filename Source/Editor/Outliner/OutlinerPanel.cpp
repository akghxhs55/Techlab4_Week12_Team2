#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/KurisuEd/EditorDragDrop.h"
#include "Editor/KurisuEd/EditorHierarchy.h"
#include "Editor/KurisuEd/EditorUndo.h"
#include "Editor/EditorUI/EditorStyle.h"

#include <cctype>

namespace
{
    bool ContainsIgnoreCase(const FString& Text, const char* Pattern)
    {
        const size_t PatternLength = std::strlen(Pattern);
        if (PatternLength == 0) return true;
        if (Text.size() < PatternLength) return false;
        for (size_t Start = 0; Start + PatternLength <= Text.size(); ++Start)
        {
            size_t i = 0;
            while (i < PatternLength &&
                std::tolower(static_cast<unsigned char>(Text[Start + i])) == std::tolower(static_cast<unsigned char>(Pattern[i])))
                ++i;
            if (i == PatternLength) return true;
        }
        return false;
    }

    // UE처럼 클래스 이름 앞의 A/U 접두사를 떼고 보여 준다 (AStaticMeshActor → StaticMeshActor).
    FString GetDisplayTypeName(const AActor* Actor)
    {
        if (!Actor || !Actor->GetClass()) return FString();
        FString Name = Actor->GetClass()->Name;
        if (Name.size() > 1 && (Name[0] == 'A' || Name[0] == 'U') && std::isupper(static_cast<unsigned char>(Name[1])))
            Name.erase(0, 1);
        return Name;
    }
}

bool FOutlinerPanel::MatchesSearch(AActor* Actor) const
{
    if (!Actor) return false;
    if (!IsFiltering() || ContainsIgnoreCase(Actor->GetName(), SearchBuffer)) return true;
    if (const auto* Children = ActorChildren.Find(Actor))
        for (AActor* Child : *Children)
            if (MatchesSearch(Child)) return true;
    return false;
}

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
    ImGui::Begin("Outliner", GetOpenFlag());
    bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (bFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        if (AActor* Actor = GetSelectedActor())
        {
            RenamingActor = Actor;
            strncpy_s(RenameBuffer, Actor->GetName().c_str(), _TRUNCATE);
            bFocusRename = true;
        }

    // UE 아웃라이너처럼 맨 위에 검색창
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##OutlinerSearch", "Search...", SearchBuffer, sizeof(SearchBuffer));

    // 아이템 라벨 / 타입 두 열. 줄마다 배경색을 번갈아 칠한다.
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, EditorColor::BgPanel);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, EditorColor::BgRaised);
    // PadOuterX + 셀 여백: 헤더 글자와 첫 열이 창 왼쪽 끝에 붙지 않게 한다.
    const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(EditorStyle::Scale(8.0f), ImGui::GetStyle().CellPadding.y));
    const bool bTableOpen = ImGui::BeginTable("##OutlinerTable", 2, TableFlags);
    ImGui::PopStyleVar();
    if (bTableOpen)
    {
        ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableHeadersRow();
        DrawActors(Level);
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);

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
        AActor* ChildActor = PendingAttachChild->GetOwner();
        USceneComponent* OldParent = PendingAttachChild->GetAttachParent();
        const FName ChildActorName = ChildActor ? ChildActor->GetFName() : FName();
        const FName ChildCompName = PendingAttachChild->GetFName();
        const FName OldParentActorName = (OldParent && OldParent->GetOwner()) ? OldParent->GetOwner()->GetFName() : FName();
        const FName OldParentCompName = OldParent ? OldParent->GetFName() : FName();

        bool bChanged = false;
        if (bPendingDetach)
        {
            if (OldParent)
            {
                PendingAttachChild->DetachFromParent(EAttachmentRule::KeepWorld);
                bChanged = PendingAttachChild->GetAttachParent() != OldParent;
            }
        }
        else
        {
            bChanged = EditorHierarchy::Attach(PendingAttachChild, PendingAttachParent);
        }

        if (bChanged && UndoHistory && UndoHistory->CanRecord() && !ChildActorName.IsNone())
        {
            USceneComponent* NewParent = PendingAttachChild->GetAttachParent();
            const FName NewParentActorName = (NewParent && NewParent->GetOwner()) ? NewParent->GetOwner()->GetFName() : FName();
            const FName NewParentCompName = NewParent ? NewParent->GetFName() : FName();
            UndoHistory->Push(EditorUndoUtils::MakeHierarchyAttachCommand(
                bPendingDetach ? "Detach Actor" : "Attach Actor",
                ChildActorName, ChildCompName,
                OldParentActorName, OldParentCompName,
                NewParentActorName, NewParentCompName));
        }

        PendingAttachChild = PendingAttachParent = nullptr; bPendingDetach = false;
    }

    // Actors 순회가 완전히 끝난 뒤 삭제
    if (!PendingDeleteActors.IsEmpty())
    {
        if (MultiDeleteCallback)
        {
            MultiDeleteCallback(PendingDeleteActors);
        }
        else if (DeleteCallback)
        {
            for (AActor* Target : PendingDeleteActors)
            {
                DeleteCallback(Target);
            }
        }
        PendingDeleteActors.Reset();
    }
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

    PruneInvalidSelections(Level);
    VisibleActorOrder.Reset();

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
        if (Actor && (!Parent || Actors.Find(Parent) == INDEX_NONE) && MatchesSearch(Actor)) DrawActorNode(Actor);
    }
}

// Actor를 ImGui TreeNode로 출력
void FOutlinerPanel::DrawActorNode(AActor* Actor)
{
    if (!Actor)
        return;

    VisibleActorOrder.Add(Actor);

    const FString ActorName = Actor->GetName();

    const auto* Children = ActorChildren.Find(Actor);
    const bool bHasChildren = Children && !Children->IsEmpty();
    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    for (AActor* Ancestor = RenamingActor; Ancestor; Ancestor = EditorHierarchy::GetParentActor(Ancestor))
        if (Ancestor == Actor) { ImGui::SetNextItemOpen(true); break; }
    // 검색 중에는 찾은 자손이 보이도록 펼친다.
    if (IsFiltering() && bHasChildren) ImGui::SetNextItemOpen(true);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    // Actor 선택 시 UI 선택 상태
    if (IsActorSelected(Actor))
        Flags |= ImGuiTreeNodeFlags_Selected;

    const ImVec2 RowStart = ImGui::GetCursorScreenPos();
    const bool bOpen = ImGui::TreeNodeEx(Actor, Flags, "%s", RenamingActor == Actor ? "" : ActorName.c_str());

    // UI에서 Actor 클릭 (Ctrl: 개별 토글, Shift: 범위 선택)
    // Select on release so dragging an actor does not replace the destination
    // actor's component list in Details halfway through a cross-panel drag.
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload())
    {
        const ImGuiIO& IO = ImGui::GetIO();
        SelectActor(Actor, IO.KeyCtrl, IO.KeyShift);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsActorSelected(Actor))
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
		if (ImGui::MenuItem("Rename", "F2", false, SelectedActors.Num() <= 1)) bRename = true;
        if (ImGui::MenuItem("Detach from Parent", nullptr, false, EditorHierarchy::GetParentActor(Actor) != nullptr))
        { PendingAttachChild = Actor->GetRootComponent(); bPendingDetach = true; }
        if (ImGui::MenuItem("Delete"))
        {
            if (SelectedActors.Num() > 1 && IsActorSelected(Actor))
                PendingDeleteActors = SelectedActors;
            else
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
				if (!Existing || Existing == Actor)
				{
					const FName OldName = Actor->GetFName();
					if (OldName != Name)
					{
						Actor->SetName(Name);
						if (UndoHistory && UndoHistory->CanRecord())
						{
							UndoHistory->Push(EditorUndoUtils::MakeActorRenameCommand(OldName, Name));
						}
					}
				}
				else KRS_LOG(Warning, "An actor with this name already exists.");
			}
			RenamingActor = nullptr;
		}
		// InputText advances to the next row itself. Restoring that cursor position
		// without submitting an item triggers ImGui's window-boundary assertion.
		ImGui::PopID();
	}

    // 타입 열
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", GetDisplayTypeName(Actor).c_str());
    ImGui::TableSetColumnIndex(0);

    if (bOpen && bHasChildren)
    {
        for (AActor* Child : *Children)
            if (MatchesSearch(Child)) DrawActorNode(Child);
        ImGui::TreePop();
    }
}

void FOutlinerPanel::NotifySelectionChanged()
{
    AActor* Primary = GetSelectedActor();
    USceneComponent* Scene = Primary ? Cast<USceneComponent>(Primary->GetRootComponent()) : nullptr;

    if (MultiCallback)
    {
        MultiCallback(SelectedActors);
    }
    else if (Callback)
    {
        Callback(Scene);
    }
}

void FOutlinerPanel::PruneInvalidSelections(ULevel* Level)
{
    if (!Level || SelectedActors.IsEmpty())
    {
        if (Level && SelectionAnchorActor && Level->GetActors().Find(SelectionAnchorActor) == INDEX_NONE)
        {
            SelectionAnchorActor = nullptr;
        }
        return;
    }

    const TArray<AActor*>& Actors = Level->GetActors();
    bool bRemoved = false;
    for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
    {
        AActor* Sel = SelectedActors[i];
        if (!Sel || Actors.Find(Sel) == INDEX_NONE)
        {
            SelectedActors.RemoveAt(i);
            bRemoved = true;
        }
    }
    if (SelectionAnchorActor && Actors.Find(SelectionAnchorActor) == INDEX_NONE)
    {
        SelectionAnchorActor = SelectedActors.IsEmpty() ? nullptr : SelectedActors[0];
    }
    if (bRemoved)
    {
        NotifySelectionChanged();
    }
}

// 선택 Object 변경
void FOutlinerPanel::SelectActor(AActor* Actor, bool bAdditiveToggle, bool bRangeSelect)
{
    if (bRangeSelect && Actor)
    {
        AActor* Anchor = SelectionAnchorActor ? SelectionAnchorActor : GetSelectedActor();
        const int32 AnchorIdx = Anchor ? VisibleActorOrder.Find(Anchor) : INDEX_NONE;
        const int32 TargetIdx = VisibleActorOrder.Find(Actor);
        if (AnchorIdx != INDEX_NONE && TargetIdx != INDEX_NONE)
        {
            RenamingActor = nullptr;
            if (!bAdditiveToggle)
            {
                SelectedActors.Reset();
            }
            const int32 Lo = (std::min)(AnchorIdx, TargetIdx);
            const int32 Hi = (std::max)(AnchorIdx, TargetIdx);
            for (int32 i = Lo; i <= Hi; ++i)
            {
                AActor* Candidate = VisibleActorOrder[i];
                if (Candidate && Candidate != Actor && SelectedActors.Find(Candidate) == INDEX_NONE)
                {
                    SelectedActors.Add(Candidate);
                }
            }
            // 마지막에 클릭된 액터가 주 선택(Primary)이 되도록 뒤에 둔다.
            const int32 PrevIdx = SelectedActors.Find(Actor);
            if (PrevIdx != INDEX_NONE)
            {
                SelectedActors.RemoveAt(PrevIdx);
            }
            SelectedActors.Add(Actor);
            if (!SelectionAnchorActor)
            {
                SelectionAnchorActor = Anchor ? Anchor : Actor;
            }
            NotifySelectionChanged();
            return;
        }
    }

    if (bAdditiveToggle)
    {
        if (!Actor)
        {
            return;
        }

        RenamingActor = nullptr;
        const int32 ExistingIdx = SelectedActors.Find(Actor);
        if (ExistingIdx != INDEX_NONE)
        {
            SelectedActors.RemoveAt(ExistingIdx);
        }
        else
        {
            SelectedActors.Add(Actor);
            KRS_LOG(Info, "{} UUID {} is selected (Multi: {})", Actor->GetName(), Actor->GetUUID(), SelectedActors.Num());
        }
        SelectionAnchorActor = Actor;
        NotifySelectionChanged();
        return;
    }

    if (!Actor)
    {
        RenamingActor = nullptr;
        SelectedActors.Reset();
        SelectionAnchorActor = nullptr;
        NotifySelectionChanged();
        return;
    }

    if (RenamingActor && RenamingActor != Actor)
    {
        RenamingActor = nullptr;
    }
    SelectedActors.Reset();
    SelectedActors.Add(Actor);
    SelectionAnchorActor = Actor;

    KRS_LOG(Info, "{} UUID {} is selected", Actor->GetName(), Actor->GetUUID());
    NotifySelectionChanged();
}

void FOutlinerPanel::OnActorDestroyed(AActor* Actor)
{
    if (!Actor)
        return;

    if (SelectionAnchorActor == Actor)
        SelectionAnchorActor = nullptr;
    if (RenamingActor == Actor)
        RenamingActor = nullptr;
    if (PendingDeleteActor == Actor)
        PendingDeleteActor = nullptr;
    for (int32 i = PendingDeleteActors.Num() - 1; i >= 0; --i)
    {
        if (PendingDeleteActors[i] == Actor)
            PendingDeleteActors.RemoveAt(i);
    }
    if (PendingAttachChild && PendingAttachChild->GetOwner() == Actor)
    {
        PendingAttachChild = nullptr;
        bPendingDetach = false;
    }
    if (PendingAttachParent && PendingAttachParent->GetOwner() == Actor)
    {
        PendingAttachParent = nullptr;
    }
    if (const int32 VisIdx = VisibleActorOrder.Find(Actor); VisIdx != INDEX_NONE)
    {
        VisibleActorOrder.RemoveAt(VisIdx);
    }

    bool bRemovedSelection = false;
    for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
    {
        if (SelectedActors[i] == Actor)
        {
            SelectedActors.RemoveAt(i);
            bRemovedSelection = true;
        }
    }
    if (!SelectionAnchorActor && !SelectedActors.IsEmpty())
    {
        SelectionAnchorActor = SelectedActors[0];
    }
    if (bRemovedSelection)
    {
        NotifySelectionChanged();
    }
}

void FOutlinerPanel::SelectActors(const TArray<AActor*>& Actors)
{
    RenamingActor = nullptr;
    SelectedActors.Reset();
    for (AActor* Actor : Actors)
    {
        if (Actor && SelectedActors.Find(Actor) == INDEX_NONE)
        {
            SelectedActors.Add(Actor);
        }
    }
    SelectionAnchorActor = SelectedActors.IsEmpty() ? nullptr : SelectedActors[0];
    NotifySelectionChanged();
}

bool FOutlinerPanel::IsActorSelected(const AActor* Actor) const
{
    return Actor && SelectedActors.Find(const_cast<AActor*>(Actor)) != INDEX_NONE;
}

AActor* FOutlinerPanel::GetSelectedActor() const
{
    return SelectedActors.IsEmpty() ? nullptr : SelectedActors[SelectedActors.Num() - 1];
}
