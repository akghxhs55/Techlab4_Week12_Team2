#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Settings/EditorSettings.h"
#include "Components/SkinnedMeshComponent.h"

// 보이기/숨기기 토글만 둔다. 품질·성능 옵션은 Render 메뉴가 맡는다.
void FViewportsPanel::DrawShowMenu()
{
    if (!EditorSettings) return;
    auto& Settings = EditorSettings->GetMutableSettings();
    ImGui::TextDisabled("Applies to all editor views");
    ImGui::SeparatorText("Scene");
    ImGui::Checkbox("Primitives", &Settings.bDrawPrimitives);
    ImGui::Checkbox("Skeletal Meshes", &Settings.bShowSkeletalMesh);
    ImGui::Checkbox("Decals", &Settings.bDrawDecals);
    ImGui::SeparatorText("Helpers");
    ImGui::Checkbox("Grid / Lines", &Settings.bDrawBatchLine);
    ImGui::Checkbox("Bounding Boxes", &Settings.bDrawBoundingBox);
    ImGui::Checkbox("Object UUID", &Settings.bShowUUID);
    ImGui::Checkbox("Cascade Bounds", &Settings.bDrawCascadeBounds);
    ImGui::SeparatorText("PIE");
    ImGui::Checkbox("Collision", &Settings.bShowCollision);
    ImGui::Checkbox("Root Bone", &Settings.bShowRootDebug);
    ImGui::Checkbox("Vignette", &Settings.bShowVignette);
    ImGui::Checkbox("Camera Fade / Letterbox", &Settings.bShowCameraOverlay);
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
