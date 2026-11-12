#include "EnginePCH.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/HitoriEd/EditorHierarchy.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/MyPlayerController.h"
#include "Input/GameKeyboardInput.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/AssetEditor/FPreviewScene.h"
#include "Math/BoxSphereBounds.h"
#include "Render/RenderDevice.h"
#include "Render/RenderCommand.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Core/Misc/Paths.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>

UClass* GetEngineClass() { return nullptr; }
std::filesystem::path ResolveProjectFile(HINSTANCE, const std::filesystem::path& Path) { return Path; }
UWorld* FPreviewScene::GetWorld() const { return nullptr; }
FString FEditorFileUtils::CurrentScenePath;
bool FImGuiRenderer::DrawShadowMap(ID3D11ShaderResourceView*, unsigned int, ImVec2, ImVec2, ImVec2, float) { return false; }
static int Checks = 0;
static void Check(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
    ++Checks;
}
static bool Near(FVector A, FVector B) { return (A-B).Size() < 0.001f; }
static bool SameMatrix(const FMatrix& A, const FMatrix& B)
{
    for (int Row=0; Row<4; ++Row) for (int Column=0; Column<4; ++Column)
        if (fabsf(A.M[Row][Column]-B.M[Row][Column]) > 0.003f) return false;
    return true;
}
static AActor* Clone(AActor* Actor, UObject* Outer)
{
    FObjectDuplicationParameters Params(Actor, Outer);
    Params.DestName = MakeUniqueObjectName(Outer, Actor->GetClass());
    return Cast<AActor>(StaticDuplicateObjectEx(Params));
}
int main()
{
    try
    {
        FPaths::InitEngineDir();
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& IO = ImGui::GetIO();
            IO.IniFilename = nullptr; IO.LogFilename = nullptr;
            IO.ConfigInputTrickleEventQueue = false;
            IO.DisplaySize = ImVec2(800,600); IO.DeltaTime = 1.0f/60.0f;
            IO.Fonts->AddFontDefaultBitmap();
            unsigned char* Pixels; int Width, Height;
            IO.Fonts->GetTexDataAsRGBA32(&Pixels,&Width,&Height);
            auto* World = NewObject<UWorld>();
            auto* Level = NewObject<ULevel>(World);
            World->SetCurrentLevel(Level);
            AActor* Last = nullptr;
            for (int Index = 0; Index < 4; ++Index) { Last = NewObject<AActor>(Level); Level->AddActor(Last); }
            {
                FOutlinerPanel Panel;
                Panel.SetWorld(World); Panel.SelectActor(Last);
                const auto Frame = [&]() {
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({400,300});
                    ImGui::SetNextWindowFocus();
                    Panel.OnRender();
                    ImGui::Render();
                };
                Frame(); Frame();
                IO.AddKeyEvent(ImGuiKey_F2,true); Frame();
                IO.AddKeyEvent(ImGuiKey_F2,false); Frame();
                Check(ImGui::GetActiveID() != 0, "F2 did not activate the last outliner row's name editor");
                IO.AddInputCharactersUTF8("RenamedLastActor"); Frame();
                IO.AddKeyEvent(ImGuiKey_Enter,true); Frame();
                IO.AddKeyEvent(ImGuiKey_Enter,false); Frame();
                Check(Last->GetName() == "RenamedLastActor", "F2 rename was not committed");
                IO.AddKeyEvent(ImGuiKey_F2,true); Frame();
                IO.AddKeyEvent(ImGuiKey_F2,false); Frame();
                IO.AddInputCharactersUTF8("CancelledName"); Frame();
                IO.AddKeyEvent(ImGuiKey_Escape,true); Frame();
                IO.AddKeyEvent(ImGuiKey_Escape,false); Frame();
                Check(Last->GetName() == "RenamedLastActor", "Escape did not cancel F2 rename");
                Frame();
                const ImVec2 Start = ImGui::FindWindowByName("Outliner")->DC.CursorStartPos;
                const float RowHeight = ImGui::GetTextLineHeightWithSpacing();
                const auto Drag = [&](ImVec2 Source, ImVec2 Target) {
                    IO.AddMousePosEvent(Source.x, Source.y); Frame();
                    IO.AddMouseButtonEvent(0,true); Frame();
                    IO.AddMousePosEvent(Target.x, Target.y); Frame(); Frame();
                    IO.AddMouseButtonEvent(0,false); Frame(); Frame();
                };
                AActor* First = Level->GetActors()[0];
                Last->GetRootComponent()->SetRelativeLocation({7,8,9});
                First->GetRootComponent()->SetRelativeLocation({10,20,30});
                const FMatrix BeforeDrag = Last->GetRootComponent()->GetWorldMatrix();
                Drag({Start.x+60, Start.y+3*RowHeight+6}, {Start.x+60, Start.y+6});
                Check(EditorHierarchy::GetParentActor(Last) == First, "outliner drag did not attach actor");
                Check(SameMatrix(BeforeDrag, Last->GetRootComponent()->GetWorldMatrix()), "outliner drag changed world transform");
                Drag({Start.x+60, Start.y+RowHeight+6}, {Start.x+60, 260});
                Check(EditorHierarchy::GetParentActor(Last) == nullptr, "dropping into blank outliner did not detach actor");
                Check(SameMatrix(BeforeDrag, Last->GetRootComponent()->GetWorldMatrix()), "outliner detach changed world transform");
            }
            {
                auto* Actor = NewObject<AActor>();
                auto* A = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(),FName("ChildA")));
                auto* B = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(),FName("ChildB")));
                FDetailsPanel Details;
                UActorComponent* Selected = Actor->GetRootComponent();
                const auto Frame = [&]() {
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({400,300});
                    ImGui::Begin("ComponentListSmoke");
                    const float Start = ImGui::GetCursorPosY();
                    Details.DrawCompoenetList(Actor,Selected);
                    const float Height = ImGui::GetCursorPosY()-Start;
                    ImGui::End(); ImGui::Render();
                    return Height;
                };
                Frame(); const float NoExtra = Frame();
                const float Expected = ImGui::GetFrameHeightWithSpacing()+3*ImGui::GetTextLineHeightWithSpacing();
                Check(fabsf(NoExtra-Expected)<0.1f, "component list drew an extra separator without non-scene components");
                auto* Extra = Actor->AddComponent(UActorComponent::StaticClass(),FName("Logic"));
                Extra->SetHiddenInDetails(true);
                Check(fabsf(Frame()-NoExtra)<0.1f, "hidden non-scene component created a separator");
                Extra->SetHiddenInDetails(false);
                Check(Frame()>NoExtra, "non-scene component row missing");
                const ImVec2 Start = ImGui::FindWindowByName("ComponentListSmoke")->DC.CursorStartPos;
                const float Rows = ImGui::GetTextLineHeightWithSpacing(), Header = ImGui::GetFrameHeightWithSpacing();
                IO.AddMousePosEvent(Start.x+60,Start.y+Header+Rows+6); Frame();
                IO.AddMouseButtonEvent(0,true); Frame();
                IO.AddMousePosEvent(Start.x+60,Start.y+Header+2*Rows+6); Frame(); Frame();
                IO.AddMouseButtonEvent(0,false); Frame(); Frame();
                Check(A->GetAttachParent()==B, "component list drag did not change parent");
                delete Actor;
            }
            {
                AActor* Parent = Level->GetActors()[0];
                auto* Anchor = Cast<USceneComponent>(Parent->AddComponent(USceneComponent::StaticClass(),FName("CrossPanelAnchor")));
                Anchor->SetRelativeLocation({3,4,5});
                FOutlinerPanel Panel;
                Panel.SetWorld(World); Panel.SelectActor(Parent);
                FDetailsPanel Details;
                UActorComponent* Selected = Parent->GetRootComponent();
                const auto Frame = [&]() {
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({380,300}); Panel.OnRender();
                    ImGui::SetNextWindowPos({400,0}); ImGui::SetNextWindowSize({380,300});
                    ImGui::Begin("CrossPanelComponents");
                    Details.DrawCompoenetList(Panel.GetSelectedActor(),Selected);
                    ImGui::End(); ImGui::Render();
                };
                Frame(); Frame();
                const ImVec2 Source = ImGui::FindWindowByName("Outliner")->DC.CursorStartPos;
                const ImVec2 Target = ImGui::FindWindowByName("CrossPanelComponents")->DC.CursorStartPos;
                const float Row = ImGui::GetTextLineHeightWithSpacing(), Header = ImGui::GetFrameHeightWithSpacing();
                const FMatrix Before = Last->GetRootComponent()->GetWorldMatrix();
                IO.AddMousePosEvent(Source.x+60,Source.y+3*Row+6); Frame();
                IO.AddMouseButtonEvent(0,true); Frame();
                IO.AddMousePosEvent(Target.x+60,Target.y+Header+Row+6); Frame(); Frame();
                IO.AddMouseButtonEvent(0,false); Frame(); Frame();
                Check(Panel.GetSelectedActor()==Parent, "dragging actor replaced the destination component list");
                Check(Last->GetRootComponent()->GetAttachParent()==Anchor, "actor-to-component cross-panel drop failed");
                Check(SameMatrix(Before,Last->GetRootComponent()->GetWorldMatrix()), "cross-panel drop changed world transform");
            }
            Level->ClearActors(); delete Level; delete World;
            ImGui::DestroyContext();
        }
        {
            const auto CreateLevel = [](UWorld*& World) {
                World = NewObject<UWorld>();
                auto* Level = NewObject<ULevel>(World);
                Level->SetWorld(World); World->SetPersistentLevel(Level); World->SetCurrentLevel(Level);
                return Level;
            };
            const auto Cleanup = [](UWorld* World, ULevel* Level) {
                for (AActor* Actor : Level->GetActors()) Actor->UnregisterAllComponents();
                Level->ClearActors(); delete Level; delete World;
            };
            UWorld* World; auto* Level = CreateLevel(World);
            auto* Child = World->SpawnActor<AActor>(FName("ChildActor"));
            auto* Parent = World->SpawnActor<AActor>(FName("ParentActor"));
            auto* Anchor = Cast<USceneComponent>(Parent->AddComponent(USceneComponent::StaticClass(), FName("Anchor")));
            Parent->GetRootComponent()->SetTransform(FTransform(FRotator(23,47,16), {12,25,-9}, {2,3,-4}));
            Anchor->SetTransform(FTransform(FRotator(7,13,-19), {2,-3,4}, {0.5f,2,-1}));
            Child->GetRootComponent()->SetTransform(FTransform(FRotator(15,-25,35), {-10,5,42}, {0.5f,-1,2}));
            const FMatrix Original = Child->GetRootComponent()->GetWorldMatrix();
            Check(EditorHierarchy::Attach(Child->GetRootComponent(), Anchor), "actor-to-component attachment rejected");
            Check(SameMatrix(Original, Child->GetRootComponent()->GetWorldMatrix()), "attachment to rotated nonuniform parent changed world TRS");
            Check(!Near(Child->GetRootComponent()->GetRelativeLocation(), Child->GetActorLocation()), "relative location was not recalculated");
            Check(!EditorHierarchy::CanAttach(Parent->GetRootComponent(), Child->GetRootComponent()), "cyclic actor attachment allowed");
            Parent->GetRootComponent()->SetRelativeLocation({13,25,-9});
            Check(Near(Child->GetActorLocation(), {-9,5,42}), "child did not follow its moved parent");
            Parent->GetRootComponent()->SetRelativeLocation({12,25,-9});
            auto* A = Cast<USceneComponent>(Child->AddComponent(USceneComponent::StaticClass(), FName("A")));
            auto* B = Cast<USceneComponent>(Child->AddComponent(USceneComponent::StaticClass(), FName("B")));
            const FMatrix OriginalA = A->GetWorldMatrix();
            Check(EditorHierarchy::Attach(A,B) && SameMatrix(OriginalA,A->GetWorldMatrix()), "component reparent changed world TRS");
            Check(!EditorHierarchy::CanAttach(B,A), "cyclic component attachment allowed");
            Check(!EditorHierarchy::CanAttach(A,Anchor), "ordinary component moved across actor ownership");
            auto* ParentCopy = Clone(Parent,nullptr);
            Check(Cast<USceneComponent>(ParentCopy->FindComponentByName(FName("Anchor")))->GetAttachChildren().IsEmpty(), "parent copy retained original child actor");
            delete ParentCopy;
            Check(Child->GetRootComponent()->GetAttachParent() == Anchor, "deleting parent copy detached original child");
            auto* ChildCopy = Clone(Child,nullptr);
            Check(!ChildCopy->GetRootComponent()->GetAttachParent() && SameMatrix(Original,ChildCopy->GetRootComponent()->GetWorldMatrix()), "actor copy did not detach with world transform intact");
            delete ChildCopy;
            TArray<uint8> Bytes;
            { FMemoryWriter Writer(Bytes); FJsonArchiveOutputFormatter Formatter(Writer); FStructuredArchive Ar(Formatter); Level->Serialize(Ar.Open().EnterRecord()); Ar.Close(); }
            UWorld* LoadedWorld; auto* LoadedLevel = CreateLevel(LoadedWorld);
            { FMemoryReader Reader(Bytes,true); FJsonArchiveInputFormatter Formatter(Reader); FStructuredArchive Ar(Formatter); LoadedLevel->Serialize(Ar.Open().EnterRecord()); Ar.Close(); Check(!Reader.IsError(), "hierarchy JSON read failed"); }
            AActor* LoadedChild = nullptr; AActor* LoadedParent = nullptr;
            for (AActor* Actor : LoadedLevel->GetActors()) { if (Actor->GetName()=="ChildActor") LoadedChild=Actor; if (Actor->GetName()=="ParentActor") LoadedParent=Actor; }
            Check(LoadedChild && LoadedParent && EditorHierarchy::GetParentActor(LoadedChild)==LoadedParent, "actor hierarchy not restored after JSON load");
            Check(LoadedChild->GetRootComponent()->GetAttachParent()->GetName()=="Anchor" && SameMatrix(Original,LoadedChild->GetRootComponent()->GetWorldMatrix()), "loaded actor attachment transform/anchor changed");
            Check(LoadedWorld->DestroyActor(LoadedParent), "parent deletion failed");
            Check(!LoadedChild->GetRootComponent()->GetAttachParent() && SameMatrix(Original,LoadedChild->GetRootComponent()->GetWorldMatrix()), "parent deletion moved child actor");
            Cleanup(LoadedWorld,LoadedLevel); Cleanup(World,Level);
        }
        auto* Pawn = NewObject<ADefaultPawn>();
        auto* Controller = NewObject<AMyPlayerController>();
        Controller->Possess(Pawn);
        FInputSystem::ClearAllStates();
        const auto ForwardKeys = [&]() {
            DispatchGameKeyboardInput([&](int32 Key, bool bDown) { Controller->InputKey(Key, bDown); });
            FInputSystem::UpdateInputStates();
        };
        FInputSystem::OnKeyDown(static_cast<int32>(EKeyCode::Q));
        ForwardKeys();
        Controller->Tick(0.1f); Pawn->Tick(0.1f);
        Check(Near(Pawn->GetActorLocation(), {0,0,-2}), "Q did not move default pawn down in world space");
        FInputSystem::OnKeyDown(static_cast<int32>(EKeyCode::E));
        ForwardKeys();
        Controller->Tick(0.1f); Pawn->Tick(0.1f);
        Check(Near(Pawn->GetActorLocation(), {0,0,-2}), "Q/E did not cancel");
        FInputSystem::OnKeyUp(static_cast<int32>(EKeyCode::Q));
        ForwardKeys();
        Controller->Tick(0.1f); Pawn->Tick(0.1f);
        Check(Near(Pawn->GetActorLocation(), {0,0,0}), "E did not move default pawn up");
        FInputSystem::OnKeyUp(static_cast<int32>(EKeyCode::E));
        ForwardKeys();
        Controller->Tick(0.1f); Pawn->Tick(0.1f);
        Check(Near(Pawn->GetActorLocation(), {0,0,0}), "E release was not forwarded; pawn kept ascending");
        Controller->UnPossess(); delete Controller; delete Pawn;
        {
            FEditorViewportClient Camera;
            FBoxSphereBounds Bounds{ {20,30,40}, {2,2,2}, 3.464f };
            Camera.FocusOnBounds(Bounds);
            const FVector Target = Camera.GetViewLocation();
            Camera.SetViewLocation(FVector::ZeroVector);
            Camera.FocusOnBounds(Bounds, true);
            Check(Near(Camera.GetViewLocation(), FVector::ZeroVector), "smooth focus jumped immediately");
            Camera.TickInput(0.125f,false,false,0,10,0.2f);
            Check(Near(Camera.GetViewLocation(), Target * 0.5f), "smooth focus midpoint is incorrect");
            Camera.TickInput(0.125f,false,false,0,10,0.2f);
            Check(Near(Camera.GetViewLocation(), Target), "smooth focus did not reach target");
            Camera.SetViewLocation(FVector::ZeroVector);
            Camera.FocusOnBounds(Bounds,true);
            Camera.TickInput(0.1f,true,false,0,10,0.2f);
            Check(Near(Camera.GetViewLocation(), FVector::ZeroVector), "manual navigation did not cancel focus");
        }
        {
            FRenderDevice Device;
            RenderCommand::Init(&Device);
            auto* PlaneActor = NewObject<AActor>();
            {
                FGizmo Gizmo;
                Gizmo.SetTarget(PlaneActor->GetRootComponent());
                Gizmo.SetSpace(EGizmoSpace::World);
                for (int Normal = 0; Normal < 3; ++Normal)
                {
                    PlaneActor->GetRootComponent()->SetRelativeLocation(FVector::ZeroVector);
                    const FVector Axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                    const FVector N = Axes[Normal], U = Axes[(Normal+1)%3], V = Axes[(Normal+2)%3];
                    Gizmo.BeginDrag(3+Normal, FRay(N*10.0f, -N), FVector2());
                    Gizmo.UpdateDrag(FRay(N*10.0f + U*2.0f + V*3.0f, -N), FVector2());
                    Check(Near(PlaneActor->GetActorLocation(), U*2.0f + V*3.0f), "plane drag failed or moved along its normal");
                    Gizmo.EndDrag();
                }
                PlaneActor->GetRootComponent()->SetRelativeLocation(FVector::ZeroVector);
                const FMatrix Projection = FMatrix::Identity;
                Gizmo.Update(FRay({0,0,10},{0,0,-1}), {590,157.5f}, Projection, 800, 600, false, {0,0,10}, true, false);
                Check(Gizmo.GetHoveredAxis() == 5, "XY plane handle picking failed");
            }
            delete PlaneActor;
            RenderCommand::Init(nullptr);
        }
        auto* Actor = NewObject<AActor>();
        auto* OldRoot = Actor->GetRootComponent();
        auto* Child = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(), FName("Child")));
        auto* Grandchild = Cast<USceneComponent>(Actor->AddComponent(USceneComponent::StaticClass(), FName("Grandchild")));
        OldRoot->SetRelativeLocation({10,20,30});
        OldRoot->SetRelativeRotation({0,35,0});
        Child->SetRelativeLocation({2,3,4});
        Grandchild->SetupAttachment(Child);
        Grandchild->SetRelativeLocation({1,2,3});
        const FVector OldPosition = OldRoot->GetWorldLocation(), ChildPosition = Child->GetWorldLocation(), GrandPosition = Grandchild->GetWorldLocation();
        Check(Actor->MakeRootComponent(Child), "root promotion rejected");
        Check(!Child->GetAttachParent() && OldRoot->GetAttachParent() == Child, "root hierarchy invalid");
        Check(Near(OldPosition,OldRoot->GetWorldLocation()) && Near(ChildPosition,Child->GetWorldLocation()) && Near(GrandPosition,Grandchild->GetWorldLocation()), "root promotion moved components");
        Check(!Actor->MakeRootComponent(Child), "same root should be rejected");
        OldRoot->SetName(FName("RenamedDefaultRoot"));
        auto* Snapshot = Clone(Actor, nullptr);
        Check(Snapshot && Snapshot->GetComponents().Num() == Actor->GetComponents().Num(), "snapshot component count changed");
        Check(Snapshot->GetRootComponent() != Child && Snapshot->GetRootComponent()->GetOwner() == Snapshot, "snapshot root/owner not remapped");
        Check(Snapshot->GetRootComponent()->GetAttachChildren().Num() == 2, "snapshot hierarchy not remapped");
        delete Actor;
        auto* Pasted = Clone(Snapshot, nullptr);
        Check(Pasted && Near(Pasted->GetActorLocation(), ChildPosition), "paste after source deletion failed");
        Check(Pasted->FindComponentByName(FName("RenamedDefaultRoot")) != nullptr, "renamed default component lost");
        TArray<uint8> Bytes;
        {
            FMemoryWriter Writer(Bytes);
            FJsonArchiveOutputFormatter Formatter(Writer);
            FStructuredArchive Ar(Formatter);
            Pasted->Serialize(Ar.Open().EnterRecord());
            Ar.Close();
        }
        auto* Loaded = NewObject<AActor>();
        {
            FMemoryReader Reader(Bytes, true);
            FJsonArchiveInputFormatter Formatter(Reader);
            FStructuredArchive Ar(Formatter);
            Loaded->Serialize(Ar.Open().EnterRecord());
            Ar.Close();
            Check(!Reader.IsError(), "actor JSON read failed");
        }
        Check(Loaded->GetComponents().Num() == Pasted->GetComponents().Num(), "load created ghost default component");
        Check(Loaded->GetRootComponent()->GetName() == "Child", "selected root not persisted");
        Check(Near(Loaded->GetActorLocation(), ChildPosition), "loaded root transform changed");
        auto* Camera = NewObject<ACameraActor>();
        Camera->GetCameraComponent()->SetName(FName("RenamedCamera"));
        auto* CameraCopy = Cast<ACameraActor>(Clone(Camera,nullptr));
        Check(CameraCopy->GetCameraComponent() == CameraCopy->GetRootComponent(), "native camera pointer points to stale constructor component");
        Check(CameraCopy->GetCameraComponent()->GetName() == "RenamedCamera", "camera component rename lost");
        delete CameraCopy; delete Camera; delete Loaded; delete Pasted; delete Snapshot;
        const std::string Marker = "UX smoke: UTF-8 " + std::to_string(GetTickCount64());
        HTR_LOG(Info, "{} 로그 저장 확인", Marker);
        bool FoundLog = false;
        for (const auto& Entry : std::filesystem::directory_iterator(FPaths::EngineDir()/"Logs"))
        {
            std::ifstream File(Entry.path());
            std::string Text((std::istreambuf_iterator<char>(File)), {});
            if (Text.find(Marker) != std::string::npos) FoundLog = true;
        }
        Check(FoundLog, "automatic session log missing");
        std::printf("PASS: %d checks (actor/component drag-drop, cross-panel attachment, separators, relative/world TRS, hierarchy persistence, deletion, Q/E, focus, gizmo, rename, duplication, logging)\n", Checks);
        return 0;
    }
    catch (const std::exception& Error) { std::printf("FAIL: %s\n", Error.what()); return 1; }
}
