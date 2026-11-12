#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Character.h"
#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void FViewportsPanel::DrawViewportToolbar(int32 ViewIndex)
{
    FEditorViewportClient* Client = GetViewportClient(ViewIndex);
    if (!Client || !Client->IsActive()) return;
    const auto& Rect = Client->GetRect();
    if (Rect.Width < 100.0f || Rect.Height < 40.0f) return;

    // 버튼 순서. Popups 배열과 앞부분 순서를 맞춘다.
    enum EItem { View, Mode, Add, Camera, Grid, Show, Render, Move, Rotate, Scale, Space, Layout, ItemCount };
    const char* Popups[] = { "View", "Mode", "Add", "Camera", "Grid", "Show", "Render" };
    EditorStyle::FPopupAnchor PopupAnchors[IM_ARRAYSIZE(Popups)]{};
    const EItem Order[] = { View, Mode, Add, Camera, Move, Rotate, Scale, Space, Grid, Show, Render, Layout };

    const bool bGameView = GameViewportClient && ViewIndex == GameViewportIndex;
    const char* Views[] = { "Perspective", "Top", "Front", "Right" };
    const char* Modes[] = { "Solid", "Wireframe", "Scene Depth", "World Normal", "Light Culling", "Cascade" };
    char GridLabel[32];
    std::snprintf(GridLabel, sizeof(GridLabel), "Grid %d###Grid",
        EditorSettings ? EditorSettings->GetSettings().GridSpacing : 1);
    const char* Labels[ItemCount]{};
    Labels[View] = Views[std::clamp(static_cast<int>(Client->GetViewportType()), 0, 3)];
    Labels[Mode] = Modes[std::clamp(static_cast<int>(Client->GetViewportMode()), 0, 5)];
    Labels[Add] = "+ Add";
    Labels[Camera] = "Camera";
    Labels[Grid] = GridLabel;
    Labels[Show] = "Show";
    Labels[Render] = "Render";
    Labels[Move] = "Move";
    Labels[Rotate] = "Rotate";
    Labels[Scale] = "Scale";
    Labels[Space] = Gizmo && Gizmo->GetSpace() == EGizmoSpace::World ? "World###Space" : "Local###Space";
    Labels[Layout] = CurrentLayoutMode == ELayoutMode::QuadSplit ? "Single" : "Quad";

    const int Count = ItemCount;
    const float Width = std::max(1.0f, Rect.Width - EditorStyle::Scale(12.0f));
    const float Gap = EditorStyle::Scale(4.0f);
    const float Padding = EditorStyle::Scale(4.0f);
    float Used = 0.0f;
    int Rows = 1;
    float ButtonWidths[ItemCount];
    for (int Index = 0; Index < Count; ++Index)
    {
        ButtonWidths[Index] = ImGui::CalcTextSize(Labels[Order[Index]], nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (Used > 0.0f && Used + Gap + ButtonWidths[Index] > Width - Padding * 2.0f)
        { ++Rows; Used = 0.0f; }
        Used += (Used > 0.0f ? Gap : 0.0f) + ButtonWidths[Index];
    }
    const float Height = std::min(Rect.Height - EditorStyle::Scale(12.0f),
        Rows * ImGui::GetFrameHeight() + (Rows - 1) * Gap + Padding * 2.0f);
    const ImVec2 Position{ ContentOrigin.x + Rect.X + EditorStyle::Scale(6.0f), ContentOrigin.y + Rect.Y + EditorStyle::Scale(6.0f) };
    ToolbarRects[ViewIndex] = { Position.x, Position.y, Width, Height };
    ToolbarHeights[ViewIndex] = Height + EditorStyle::Scale(6.0f);
    ImGui::SetCursorScreenPos(Position);
    ImGui::PushID(ViewIndex);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { Padding, Padding });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { Gap, Gap });
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorStyle::Scale(2.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 26, 30, 235));
    if (ImGui::BeginChild("ViewportToolbar", { Width, Height }, ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ActiveViewIndex = ViewIndex;
        Used = 0.0f;
        for (int Index = 0; Index < Count; ++Index)
        {
            const EItem Item = Order[Index];
            const float ButtonWidth = ButtonWidths[Index];
            if (Used > 0.0f && Used + Gap + ButtonWidth <= Width - Padding * 2.0f)
                ImGui::SameLine();
            else Used = 0.0f;
            const bool bGizmoItem = Item >= Move && Item <= Space;
            // PIE 게임 화면에서는 에디터 카메라·기즈모·스폰을 쓸 수 없다.
            const bool bDisabled = (bGizmoItem && !Gizmo) ||
                (bGameView && (bGizmoItem || Item == View || Item == Mode || Item == Add || Item == Camera));
            // 지금 선택된 기즈모 모드는 눌린 색으로 표시한다.
            const bool bSelected = Gizmo && Item >= Move && Item <= Scale &&
                static_cast<int>(Gizmo->GetMode()) == Item - Move;
            EditorStyle::PushSelectedButton(bSelected);
            ImGui::BeginDisabled(bDisabled);
            if (ImGui::Button(Labels[Item], { ButtonWidth, 0.0f }))
            {
                ActiveViewIndex = ViewIndex;
                if (Item <= Render) ImGui::OpenPopup(Popups[Item]);
                else if (Item <= Scale) Gizmo->SetMode(static_cast<EGizmoMode>(Item - Move));
                else if (Item == Space)
                    Gizmo->SetSpace(Gizmo->GetSpace() == EGizmoSpace::World ? EGizmoSpace::Local : EGizmoSpace::World);
                else if (Item == Layout)
                {
                    RequestedLayoutMode = CurrentLayoutMode == ELayoutMode::QuadSplit ? ELayoutMode::Single : ELayoutMode::QuadSplit;
                    RequestedSingleViewIndex = ViewIndex;
                    bHasLayoutRequest = true;
                }
            }
            if (Item <= Render) PopupAnchors[Item] = EditorStyle::LastItemAnchor();
            ImGui::EndDisabled();
            EditorStyle::PopSelectedButton(bSelected);
            if (Item >= Move && Item <= Scale && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Space: cycle Move / Rotate / Scale");
            Used += (Used > 0.0f ? Gap : 0.0f) + ButtonWidth;
        }
        if (EditorStyle::BeginButtonPopup("View", PopupAnchors[View]))
        {
            for (int Index = 0; Index < 4; ++Index)
                if (ImGui::MenuItem(Views[Index], nullptr, static_cast<int>(Client->GetViewportType()) == Index))
                    Client->SetViewportType(static_cast<ELevelViewportType>(Index));
            ImGui::EndPopup();
        }
        if (EditorStyle::BeginButtonPopup("Mode", PopupAnchors[Mode]))
        {
            for (int Index = 0; Index < 6; ++Index)
                if (ImGui::MenuItem(Modes[Index], nullptr, static_cast<int>(Client->GetViewportMode()) == Index))
                    Client->SetViewportMode(static_cast<EViewportMode>(Index));
            if (EditorSettings && Client->GetViewportMode() == EViewportMode::SceneDepth)
            {
                ImGui::Separator();
                ImGui::SetNextItemWidth(EditorStyle::Scale(120.0f));
                if (ImGui::DragFloat("Depth Max Distance", &EditorSettings->GetMutableSettings().SceneDepthMaxRange,
                    0.5f, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
                    EditorSettings->ApplySceneDepthRange();
            }
            ImGui::EndPopup();
        }
        if (EditorStyle::BeginButtonPopup("Add", PopupAnchors[Add])) { DrawSpawnMenu(*Client); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Camera", PopupAnchors[Camera], EditorStyle::Scale(330.0f))) { DrawCameraMenu(*Client); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Grid", PopupAnchors[Grid])) { DrawGridMenu(); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Show", PopupAnchors[Show])) { DrawShowMenu(); ImGui::EndPopup(); }
        if (EditorStyle::BeginButtonPopup("Render", PopupAnchors[Render])) { DrawRenderMenu(); ImGui::EndPopup(); }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::PopID();
}

void FViewportsPanel::DrawSpawnMenu(FEditorViewportClient& Client)
{
    ImGui::BeginDisabled(!World || (IsPlayingQuery && IsPlayingQuery()));
    auto Spawn = [&](const char* Label, UClass* Class)
    {
        if (ImGui::MenuItem(Label))
        {
            AActor* Actor = World->SpawnActor(Class);
            if (Actor)
            {
                if (USceneComponent* Root = Actor->GetRootComponent())
                    Root->SetRelativeLocation(Client.GetViewLocation() + Client.GetViewForward() * 5.0f);
                if (OnActorSpawned) OnActorSpawned(Actor);
            }
        }
    };
    ImGui::SeparatorText("Basic");
    Spawn("Camera", ACameraActor::StaticClass());
    Spawn("Character", ACharacter::StaticClass());
    Spawn("Text", ATextRenderActor::StaticClass());
    ImGui::SeparatorText("Meshes");
    Spawn("Skeletal Mesh", ASkeletalMeshActor::StaticClass());
    Spawn("Static Mesh", AStaticMeshActor::StaticClass());
    ImGui::SeparatorText("Lights");
    Spawn("Ambient Light", AAmbientLightActor::StaticClass());
    Spawn("Directional Light", ADirectionalLightActor::StaticClass());
    Spawn("Point Light", APointLightActor::StaticClass());
    Spawn("Spot Light", ASpotLightActor::StaticClass());
    ImGui::SeparatorText("Effects");
    Spawn("Decal", ADecalActor::StaticClass());
    Spawn("Height Fog", AExponentialHeightFogActor::StaticClass());
    Spawn("Particle", AParticleActor::StaticClass());
    ImGui::EndDisabled();
    if (World) ImGui::TextDisabled("%d actors", World->GetActorNum());
}

void FViewportsPanel::DrawCameraMenu(FEditorViewportClient& Client)
{
    ImGui::SeparatorText("Projection");
    ImGui::PushItemWidth(EditorStyle::Scale(180.0f));
    if (Client.IsPerspective())
    {
        float Fov = Client.GetViewFOV();
        if (ImGui::DragFloat("FOV", &Fov, 0.1f, 1.0f, 179.0f) && std::isfinite(Fov))
            Client.SetViewFOV(FMath::Clamp(Fov, 1.0f, 179.0f));
    }
    else
    {
        float Width = Client.GetOrthoWidth();
        if (ImGui::DragFloat("Ortho Width", &Width, 0.1f, 0.01f, 1000000.0f) && std::isfinite(Width))
            Client.SetOrthoWidth(FMath::Clamp(Width, 0.01f, 1000000.0f));
    }
    float Near = Client.GetNearClip(), Far = Client.GetFarClip();
    bool Changed = ImGui::InputFloat("Near Clip", &Near);
    Changed |= ImGui::InputFloat("Far Clip", &Far);
    if (Changed && std::isfinite(Near) && std::isfinite(Far) && Near > 0.0f && Far > Near)
        Client.SetClipPlanes(Near, Far);
    ImGui::SeparatorText("Transform");
    FVector Location = Client.GetViewLocation();
    if (ImGui::DragFloat3("Location", &Location.X, 0.1f) &&
        std::isfinite(Location.X) && std::isfinite(Location.Y) && std::isfinite(Location.Z))
        Client.SetViewLocation(Location);
    FRotator Rotation = Client.GetViewRotation();
    float Angles[] = { Rotation.Pitch, Rotation.Yaw, Rotation.Roll };
    if (ImGui::DragFloat3("Rotation", Angles, 0.1f) &&
        std::isfinite(Angles[0]) && std::isfinite(Angles[1]) && std::isfinite(Angles[2]))
        Client.SetViewRotation(FRotator(Angles[0], Angles[1], Angles[2]));
    if (EditorSettings)
    {
        ImGui::SeparatorText("Movement");
        auto& Settings = EditorSettings->GetMutableSettings();
        ImGui::SliderFloat("Speed", &Settings.CameraSpeed, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Sensitivity", &Settings.MouseSensitivity, 0.01f, 1.0f, "%.2f");
    }
    ImGui::PopItemWidth();
}

// 언리얼의 그리드 크기 버튼처럼 자주 쓰는 간격을 고르거나 직접 입력한다.
void FViewportsPanel::DrawGridMenu()
{
    if (!EditorSettings) return;
    int32& Spacing = EditorSettings->GetMutableSettings().GridSpacing;
    ImGui::SeparatorText("Grid spacing");
    for (const int32 Preset : { 1, 5, 10, 50, 100 })
    {
        char Label[16];
        std::snprintf(Label, sizeof(Label), "%d", Preset);
        if (ImGui::MenuItem(Label, nullptr, Spacing == Preset)) Spacing = Preset;
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(EditorStyle::Scale(120.0f));
    ImGui::SliderInt("Custom", &Spacing, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
}
