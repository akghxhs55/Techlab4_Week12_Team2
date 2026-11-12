#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "Components/SkinnedMeshComponent.h"
#include "Core/StatOverlay.h"

// UE의 표시(Show) 메뉴: 기본값 복원, 뷰포트 통계, 표시 플래그 체크 목록.
// 보이기/숨기기만 둔다. 품질·성능 옵션은 Render 메뉴가 맡는다. 모든 에디터 뷰에 함께 적용된다.
void FViewportsPanel::DrawShowMenu()
{
    if (!EditorSettings) return;
    using namespace ViewportToolbarWidgets;
    auto& Settings = EditorSettings->GetMutableSettings();

    if (IconMenuItem(nullptr, "Use Defaults", false, EIndicator::None))
    {
        const FEditorSettings Defaults{};
        Settings.bDrawPrimitives = Defaults.bDrawPrimitives;
        Settings.bShowSkeletalMesh = Defaults.bShowSkeletalMesh;
        Settings.bDrawDecals = Defaults.bDrawDecals;
        Settings.bDrawBatchLine = Defaults.bDrawBatchLine;
        Settings.bDrawBoundingBox = Defaults.bDrawBoundingBox;
        Settings.bShowUUID = Defaults.bShowUUID;
        Settings.bDrawCascadeBounds = Defaults.bDrawCascadeBounds;
        Settings.bShowCollision = Defaults.bShowCollision;
        Settings.bShowRootDebug = Defaults.bShowRootDebug;
        Settings.bShowVignette = Defaults.bShowVignette;
        Settings.bShowCameraOverlay = Defaults.bShowCameraOverlay;
    }

    // 뷰포트 통계: 콘솔의 stat 명령과 같은 상태를 켜고 끈다.
    if (ImGui::BeginMenu("      Viewport Stats"))
    {
        struct FStatItem { const char* Label; const char* Command; EStatFlags Flag; };
        const FStatItem Stats[] = {
            { "FPS", "stat fps", EStatFlags::FPS },
            { "Memory", "stat memory", EStatFlags::Memory },
            { "Profile", "stat profile", EStatFlags::Profile },
            { "Light", "stat light", EStatFlags::Light },
            { "Decal", "stat decal", EStatFlags::Decal },
            { "Skinning", "stat skinning", EStatFlags::Skinning },
        };
        FString Message;
        if (IconMenuItem(nullptr, "Hide All", false, EIndicator::None))
            FStatOverlay::ExecCommand("stat none", Message);
        SectionHeader("Common Stats");
        for (const FStatItem& Stat : Stats)
            if (IconMenuItem(nullptr, Stat.Label, FStatOverlay::IsEnabled(Stat.Flag), EIndicator::Check))
                FStatOverlay::ExecCommand(Stat.Command, Message);
        ImGui::EndMenu();
    }
    // BeginMenu 줄 앞에 통계 아이콘을 덧그린다.
    {
        const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
        const float CenterY = (Min.y + Max.y) * 0.5f, X = Min.x + EditorStyle::Scale(22.0f);
        if (const ImTextureID Icon = GetIcon("Stats"); Icon != ImTextureID_Invalid)
            ImGui::GetWindowDrawList()->AddImage(Icon, { X - EditorStyle::Scale(20.0f), CenterY - IconSize() * 0.5f },
                { X - EditorStyle::Scale(4.0f), CenterY + IconSize() * 0.5f }, { 0, 0 }, { 1, 1 }, IM_COL32(200, 200, 200, 255));
    }

    auto Flag = [](const char* Icon, const char* Label, bool& bValue)
    {
        if (IconMenuItem(Icon, Label, bValue, EIndicator::Check)) bValue = !bValue;
    };
    SectionHeader("Common Show Flags");
    Flag("Bounds", "Bounding Boxes", Settings.bDrawBoundingBox);
    Flag("Decals", "Decals", Settings.bDrawDecals);
    Flag("Grid", "Grid", Settings.bDrawBatchLine);
    Flag("SkeletalMesh", "Skeletal Meshes", Settings.bShowSkeletalMesh);
    Flag("StaticMesh", "Static Meshes", Settings.bDrawPrimitives);
    SectionHeader("Developer");
    Flag("Cascade", "Cascade Bounds", Settings.bDrawCascadeBounds);
    Flag("Pivot", "Object UUID", Settings.bShowUUID);
    SectionHeader("PIE");
    Flag("Collision", "Collision", Settings.bShowCollision);
    Flag("SkeletalMesh", "Root Bone", Settings.bShowRootDebug);
    Flag("Lighting", "Vignette", Settings.bShowVignette);
    Flag("Camera", "Camera Fade / Letterbox", Settings.bShowCameraOverlay);
    ImGui::Spacing();
    ImGui::TextDisabled("Applies to all editor views");
}

// 모든 뷰에 공통인 렌더 품질·성능 옵션
void FViewportsPanel::DrawRenderMenu()
{
    if (!EditorSettings) return;
    auto& Settings = EditorSettings->GetMutableSettings();
    ImGui::TextDisabled("Applies to all editor views");
    ImGui::PushItemWidth(EditorStyle::Scale(160.0f));

    ImGui::SeparatorText("Lighting");
    const char* Models[] = { "Gouraud", "Lambert", "Phong", "Unlit" };
    int Lighting = static_cast<int>(Settings.LightingModel);
    if (ImGui::Combo("Lighting Model", &Lighting, Models, IM_ARRAYSIZE(Models)))
        Settings.LightingModel = static_cast<ELightingModel>(Lighting);
    const char* Culling[] = { "Off", "Tiled" };
    int LightCulling = static_cast<int>(Settings.LightCullingMode);
    if (ImGui::Combo("Light Culling", &LightCulling, Culling, IM_ARRAYSIZE(Culling)))
        Settings.LightCullingMode = static_cast<ELightCullingMode>(LightCulling);

    ImGui::SeparatorText("Shadows");
    ImGui::Checkbox("Cache Shadows", &Settings.bCacheShadows);
    const char* Quality[] = { "Very Low", "Low", "Medium", "High", "Very High" };
    ImGui::Combo("Directional", &Settings.DirectionalShadowMapSizeLevel, Quality, IM_ARRAYSIZE(Quality));
    ImGui::Combo("Point", &Settings.PointShadowMapSizeLevel, Quality, IM_ARRAYSIZE(Quality));
    ImGui::Combo("Spot", &Settings.SpotShadowMapSizeLevel, Quality, IM_ARRAYSIZE(Quality));
    const char* Filters[] = { "Off", "8 Samples", "16 Samples", "32 Samples", "PCSS" };
    int Filter = static_cast<int>(Settings.PcfMode);
    if (ImGui::Combo("Filter", &Filter, Filters, IM_ARRAYSIZE(Filters)))
        Settings.PcfMode = static_cast<EPcfMode>(Filter);

    ImGui::SeparatorText("Post Process");
    ImGui::Checkbox("FXAA", &Settings.bEnableFXAA);
    ImGui::Checkbox("Gamma Correction", &Settings.bGammaCorrection);
    ImGui::BeginDisabled(!Settings.bGammaCorrection);
    ImGui::SliderFloat("Display Gamma", &Settings.DisplayGamma, 1.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Performance");
    const char* Decals[] = { "Off", "Linear AABB", "BVH" };
    int DecalCulling = Settings.bDecalBVHCulling ? 2 : (Settings.bDecalCulling ? 1 : 0);
    if (ImGui::Combo("Decal Broad Phase", &DecalCulling, Decals, IM_ARRAYSIZE(Decals)))
    {
        Settings.bDecalCulling = DecalCulling != 0;
        Settings.bDecalBVHCulling = DecalCulling == 2;
    }
    ImGui::Checkbox("Decal Narrow Phase (SAT)", &Settings.bDecalNarrowCulling);
    ImGui::Checkbox("Decal OBBTree", &Settings.bDecalOBBTree);
    bool GPU = USkinnedMeshComponent::bUseGPUSkinning;
    if (ImGui::Checkbox("GPU Skinning", &GPU)) USkinnedMeshComponent::SetGPUSkinningEnabled(GPU);

    ImGui::PopItemWidth();
}
