#include "AppSceneControlsPanel.h"

#include "AppRuntimeState.h"
#include "EffectRuntime.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>

namespace {
Vector3 FrontHitEffectPosition(const AppRuntimeState& runtimeState) {
    const float frontRadius = runtimeState.transform.scale.z;
    return {
        runtimeState.transform.translate.x,
        runtimeState.transform.translate.y,
        runtimeState.transform.translate.z - frontRadius - 0.15f,
    };
}

void SyncHeldEffect(
    EffectRuntime& effectRuntime,
    bool enabled,
    uint32_t& instanceId,
    const char* effectName,
    const Vector3& position,
    const Vector4& color,
    const Vector3& scale) {
    if (!enabled) {
        if (instanceId != 0) {
            effectRuntime.SetEffectPreviewLoop(instanceId, false);
            effectRuntime.StopEffect(instanceId);
            instanceId = 0;
        }
        return;
    }

    const EffectInstance* instance = effectRuntime.FindInstance(instanceId);
    if (instance == nullptr) {
        instanceId = effectRuntime.PlayEffectWithParams(effectName, position, color, scale);
        effectRuntime.SetEffectPreviewLoop(instanceId, true);
        instance = effectRuntime.FindInstance(instanceId);
        if (instance == nullptr) {
            return;
        }
    } else {
        effectRuntime.SetEffectPreviewLoop(instanceId, true);
    }

    if (instance->assetName != effectName) {
        effectRuntime.SetEffectPreviewLoop(instanceId, false);
        effectRuntime.StopEffect(instanceId);
        instanceId = effectRuntime.PlayEffectWithParams(effectName, position, color, scale);
        effectRuntime.SetEffectPreviewLoop(instanceId, true);
        return;
    }

    (void)effectRuntime.MoveInstance(instanceId, position, true);
}

void DisableHeldHitEffects(AppRuntimeState& runtimeState, EffectRuntime& effectRuntime) {
    runtimeState.vfx.holdHitPlaneBurst = false;
    runtimeState.vfx.holdHitRing = false;
    runtimeState.vfx.holdHitCylinder = false;
    runtimeState.vfx.holdHitCylinderCombo = false;
    SyncHeldEffect(
        effectRuntime,
        false,
        runtimeState.vfx.holdHitPlaneBurstInstanceId,
        "hit_plane_burst",
        {},
        {},
        {});
    SyncHeldEffect(
        effectRuntime,
        false,
        runtimeState.vfx.holdHitRingInstanceId,
        "hit_ring",
        {},
        {},
        {});
    SyncHeldEffect(
        effectRuntime,
        false,
        runtimeState.vfx.holdHitCylinderInstanceId,
        "hit_cylinder",
        {},
        {},
        {});
    SyncHeldEffect(
        effectRuntime,
        false,
        runtimeState.vfx.holdHitCylinderComboInstanceId,
        "hit_cylinder_combo",
        {},
        {},
        {});
}

void ApplyVfxShowcaseMode(AppRuntimeState& runtimeState) {
    runtimeState.clearColor[0] = 0.78f;
    runtimeState.clearColor[1] = 0.76f;
    runtimeState.clearColor[2] = 0.74f;
    runtimeState.clearColor[3] = 1.0f;

    runtimeState.useMonsterBall = false;
    runtimeState.showAnimatedCube = false;
    runtimeState.showSkinnedModel = false;
    runtimeState.showSkeletonDebug = false;
    runtimeState.showSkybox = false;
    runtimeState.showProceduralBackdrop = true;
    runtimeState.showVfxModelObjects = false;
    runtimeState.vfx.enableTrailMeshStream = true;
    runtimeState.vfx.enableTrailMeshStreamAutoFallback = false;
    runtimeState.vfx.trailMeshStreamFallbackActive = false;

    runtimeState.directionalLightData.color = {0.55f, 0.7f, 1.0f, 1.0f};
    runtimeState.directionalLightData.direction = {0.25f, -1.0f, 0.2f};
    runtimeState.directionalLightData.intensity = 0.12f;

    runtimeState.pointLightData.color = {0.35f, 0.65f, 1.0f, 1.0f};
    runtimeState.pointLightData.intensity = 0.0f;
    runtimeState.pointLightData.radius = 6.0f;
    runtimeState.pointLightData.decay = 2.0f;
}

void ResetIceProjectileShots(AppVfxRuntimeState& vfxState) {
    vfxState.iceProjectilePreviewActive = false;
    vfxState.iceProjectileImpactSpawned = false;
    vfxState.iceProjectileInstanceId = 0;
    vfxState.iceProjectileTimer = 0.0f;
    for (AppVfxRuntimeState::IceProjectileShotState& shot : vfxState.iceProjectileShots) {
        shot = {};
    }
}
} // namespace

void DrawSceneLightingControlsPanel(
    AppRuntimeState& runtimeState) {
    ImGui::ColorEdit3("Light Color",
        reinterpret_cast<float*>(&runtimeState.directionalLightData.color));

    ImGui::SliderFloat3(
        "Light Direction",
        reinterpret_cast<float*>(&runtimeState.directionalLightData.direction),
        -1.0f,
        1.0f);

    ImGui::SliderFloat(
        "Intensity",
        &runtimeState.directionalLightData.intensity,
        0.0f,
        10.0f);
}

void DrawMaterialSettingsPanel(
    AppRuntimeState& runtimeState,
    const std::function<void()>& onAddParticle) {
    ImGui::Begin("Material Settings");
    DrawMaterialSettingsControlsPanel(runtimeState, onAddParticle);
    ImGui::End();
}

void DrawMaterialSettingsControlsPanel(
    AppRuntimeState& runtimeState,
    const std::function<void()>& onAddParticle) {
    ImGui::ColorEdit4("Material Color",
        reinterpret_cast<float*>(&runtimeState.materialData.color));
    ImGui::Checkbox("Enable Lighting", reinterpret_cast<bool*>(&runtimeState.materialData.enableLighting));
    const char* specularModeLabels[] = {"Phong", "Blinn-Phong"};
    int specularMode = std::clamp(runtimeState.materialData.specularMode, 0, 1);
    runtimeState.materialData.specularMode = specularMode;
    if (ImGui::Combo(
            "Specular Mode",
            &specularMode,
            specularModeLabels,
            IM_ARRAYSIZE(specularModeLabels))) {
        runtimeState.materialData.specularMode = specularMode;
    }
    ImGui::SliderFloat("Shininess", &runtimeState.materialData.shininess, 1.0f, 64.0f);
    ImGui::SliderFloat(
        "Environment Reflection",
        &runtimeState.materialData.environmentCoefficient,
        0.0f,
        1.0f);

    ImGui::Text("Recommended: shininess 8-16");

    ImGui::Text("Scale");
    ImGui::DragFloat3("Scale", reinterpret_cast<float*>(&runtimeState.transform.scale),
        0.01f, 0.01f, 10.0f);

    ImGui::Text("Rotate");
    ImGui::DragFloat3("Rotate", reinterpret_cast<float*>(&runtimeState.transform.rotate),
        0.01f, -3.14f, 3.14f);

    ImGui::Text("Translate");
    ImGui::DragFloat3("Translate",
        reinterpret_cast<float*>(&runtimeState.transform.translate), 0.01f,
        -100.0f, 100.0f);

    ImGui::SeparatorText("Scene Objects");
    ImGui::Checkbox("Show Neutral Sphere", &runtimeState.useMonsterBall);
    ImGui::SameLine();
    ImGui::Checkbox("Show Animated Cube", &runtimeState.showAnimatedCube);
    ImGui::SameLine();
    ImGui::Checkbox("Show Skinned", &runtimeState.showSkinnedModel);
    ImGui::SameLine();
    ImGui::Checkbox("Show Skeleton", &runtimeState.showSkeletonDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Show Skybox", &runtimeState.showSkybox);
    ImGui::SameLine();
    ImGui::Checkbox("Procedural Backdrop", &runtimeState.showProceduralBackdrop);
    ImGui::SameLine();
    ImGui::Checkbox("Play Animation", &runtimeState.playAnimatedCube);
    ImGui::SameLine();
    ImGui::Checkbox("Loop Animation", &runtimeState.loopAnimatedCube);
    const char* skinnedModelLabels[] = {
        "simpleSkin",
        "human walk",
        "human sneakWalk",
    };
    int selectedSkinnedModel = static_cast<int>(runtimeState.selectedSkinnedModelIndex);
    if (ImGui::Combo(
            "Skinned Model",
            &selectedSkinnedModel,
            skinnedModelLabels,
            IM_ARRAYSIZE(skinnedModelLabels))) {
        runtimeState.selectedSkinnedModelIndex =
            static_cast<uint32_t>((std::max)(0, selectedSkinnedModel));
        runtimeState.animatedCubeTime = 0.0f;
    }
    ImGui::TextUnformatted("Skeleton Debug Source: selected Skinned Model");
    ImGui::SliderFloat("Animation Speed", &runtimeState.animatedCubeSpeed, -2.0f, 2.0f);
    ImGui::SliderFloat("Animation Time", &runtimeState.animatedCubeTime, 0.0f, 5.0f);
    ImGui::DragFloat3(
        "Animated Cube Scale",
        reinterpret_cast<float*>(&runtimeState.animatedCubeTransform.scale),
        0.01f,
        0.01f,
        10.0f);
    ImGui::DragFloat3(
        "Animated Cube Rotate",
        reinterpret_cast<float*>(&runtimeState.animatedCubeTransform.rotate),
        0.01f,
        -3.14f,
        3.14f);
    ImGui::DragFloat3(
        "Animated Cube Translate",
        reinterpret_cast<float*>(&runtimeState.animatedCubeTransform.translate),
        0.01f,
        -100.0f,
        100.0f);
    ImGui::DragFloat3(
        "Skinned Scale",
        reinterpret_cast<float*>(&runtimeState.skinnedModelTransform.scale),
        0.01f,
        0.01f,
        10.0f);
    ImGui::DragFloat3(
        "Skinned Rotate",
        reinterpret_cast<float*>(&runtimeState.skinnedModelTransform.rotate),
        0.01f,
        -3.14f,
        3.14f);
    ImGui::DragFloat3(
        "Skinned Translate",
        reinterpret_cast<float*>(&runtimeState.skinnedModelTransform.translate),
        0.01f,
        -100.0f,
        100.0f);

    ImGui::SeparatorText("Hand GPU Particle");
    if (ImGui::Checkbox(
            "Enable Hand Particle Attachment",
            &runtimeState.handParticleAttachment.enabled) &&
        runtimeState.handParticleAttachment.enabled) {
        runtimeState.showSkinnedModel = true;
        runtimeState.vfx.enableParticles = true;
        if (runtimeState.selectedSkinnedModelIndex == 0) {
            runtimeState.selectedSkinnedModelIndex = 1;
            runtimeState.animatedCubeTime = 0.0f;
        }
    }
    const char* handJointLabels[] = {
        "Right Hand",
        "Left Hand",
        "Custom (environment)",
    };
    int selectedHandJoint =
        runtimeState.handParticleAttachment.jointName == "mixamorig:LeftHand"
        ? 1
        : runtimeState.handParticleAttachment.jointName == "mixamorig:RightHand"
            ? 0
            : 2;
    if (ImGui::Combo(
            "Hand Joint",
            &selectedHandJoint,
            handJointLabels,
            IM_ARRAYSIZE(handJointLabels))) {
        if (selectedHandJoint == 0) {
            runtimeState.handParticleAttachment.jointName = "mixamorig:RightHand";
        } else if (selectedHandJoint == 1) {
            runtimeState.handParticleAttachment.jointName = "mixamorig:LeftHand";
        }
    }
    ImGui::DragFloat3(
        "Hand Socket Offset",
        reinterpret_cast<float*>(&runtimeState.handParticleAttachment.socketOffset.translate),
        0.005f,
        -2.0f,
        2.0f);
    const HandParticleAttachmentTelemetry& handTelemetry =
        runtimeState.handParticleAttachmentTelemetry;
    ImGui::Text(
        "Attachment: %s | Socket: %s | Instance: %u",
        HandParticleAttachmentStatusName(handTelemetry.status),
        BoneSocketStatusName(handTelemetry.socketStatus),
        handTelemetry.effectInstanceId);
    ImGui::Text(
        "Hand World: %.3f, %.3f, %.3f",
        handTelemetry.worldPosition.x,
        handTelemetry.worldPosition.y,
        handTelemetry.worldPosition.z);
    ImGui::Text("Effect: %s", runtimeState.handParticleAttachment.effectName.c_str());

    ImGui::SeparatorText("Weapon Attachment");
    if (ImGui::Checkbox(
            "Enable Weapon Attachment",
            &runtimeState.weaponAttachment.enabled) &&
        runtimeState.weaponAttachment.enabled) {
        runtimeState.showSkinnedModel = true;
        if (runtimeState.selectedSkinnedModelIndex == 0) {
            runtimeState.selectedSkinnedModelIndex = 1;
            runtimeState.animatedCubeTime = 0.0f;
        }
    }
    int selectedWeaponJoint =
        runtimeState.weaponAttachment.jointName == "mixamorig:LeftHand"
        ? 1
        : runtimeState.weaponAttachment.jointName == "mixamorig:RightHand"
            ? 0
            : 2;
    if (ImGui::Combo(
            "Weapon Hand Joint",
            &selectedWeaponJoint,
            handJointLabels,
            IM_ARRAYSIZE(handJointLabels))) {
        if (selectedWeaponJoint == 0) {
            runtimeState.weaponAttachment.jointName = "mixamorig:RightHand";
        } else if (selectedWeaponJoint == 1) {
            runtimeState.weaponAttachment.jointName = "mixamorig:LeftHand";
        }
    }
    ImGui::DragFloat3(
        "Weapon Socket Offset",
        reinterpret_cast<float*>(&runtimeState.weaponAttachment.socketOffset.translate),
        0.005f,
        -2.0f,
        2.0f);
    ImGui::DragFloat4(
        "Weapon Socket Rotation (Quaternion XYZW)",
        reinterpret_cast<float*>(&runtimeState.weaponAttachment.socketOffset.rotate),
        0.005f,
        -1.0f,
        1.0f);
    ImGui::DragFloat3(
        "Weapon Socket Scale",
        reinterpret_cast<float*>(&runtimeState.weaponAttachment.socketOffset.scale),
        0.005f,
        0.01f,
        5.0f);
    const WeaponAttachmentTelemetry& weaponTelemetry =
        runtimeState.weaponAttachmentTelemetry;
    ImGui::Text(
        "Weapon: %s | Socket: %s | Model: %u",
        WeaponAttachmentStatusName(weaponTelemetry.status),
        BoneSocketStatusName(weaponTelemetry.socketStatus),
        weaponTelemetry.modelIndex);
    ImGui::Text(
        "Weapon World: %.3f, %.3f, %.3f",
        weaponTelemetry.worldPosition.x,
        weaponTelemetry.worldPosition.y,
        weaponTelemetry.worldPosition.z);
    ImGui::TextUnformatted("Asset: training_sword (shared procedural mesh)");

    ImGui::SeparatorText("Camera");
    ImGui::Checkbox("Debug Camera Input", &runtimeState.camera.enableDebugInput);
    ImGui::DragFloat3(
        "Camera Position",
        reinterpret_cast<float*>(&runtimeState.camera.transform.translate),
        0.05f,
        -100.0f,
        100.0f);
    ImGui::DragFloat3(
        "Camera Rotation",
        reinterpret_cast<float*>(&runtimeState.camera.transform.rotate),
        0.01f,
        -3.14f,
        3.14f);
    ImGui::SliderAngle("Camera FOV", &runtimeState.camera.fovY, 10.0f, 120.0f);
    ImGui::DragFloat("Camera Near", &runtimeState.camera.nearZ, 0.01f, 0.001f, 100.0f);
    ImGui::DragFloat("Camera Far", &runtimeState.camera.farZ, 1.0f, 1.0f, 5000.0f);
    ImGui::DragFloat("Debug Move Speed", &runtimeState.camera.debugMoveSpeed, 0.05f, 0.0f, 10.0f);
    ImGui::DragFloat("Debug Fast Multiplier", &runtimeState.camera.debugFastMoveMultiplier, 0.1f, 1.0f, 24.0f);
    ImGui::DragFloat("Debug Slow Multiplier", &runtimeState.camera.debugSlowMoveMultiplier, 0.01f, 0.01f, 1.0f);
    ImGui::DragFloat("Debug Rotate Speed", &runtimeState.camera.debugRotateSpeed, 0.001f, 0.0f, 0.20f);

    ImGui::SeparatorText("Rail Terrain Authoring");
    TerrainAuthoringState& terrain = runtimeState.terrain;
    ImGui::Checkbox("Terrain Authoring", &terrain.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Debug Draw", &terrain.showDebugDraw);
    const char* terrainDisplayModeLabels[] = {
        "Lit",
        "Unlit",
        "Wireframe",
        "Debug",
        "Detail Normal",
    };
    int terrainDisplayMode = static_cast<int>(terrain.displayMode);
    if (ImGui::Combo(
            "Terrain Display Mode",
            &terrainDisplayMode,
            terrainDisplayModeLabels,
            IM_ARRAYSIZE(terrainDisplayModeLabels))) {
        terrainDisplayMode = std::clamp(terrainDisplayMode, 0, 4);
        terrain.displayMode = static_cast<TerrainDisplayMode>(terrainDisplayMode);
        if (terrain.displayMode == TerrainDisplayMode::Debug) {
            terrain.showDebugDraw = true;
        }
    }
    ImGui::SeparatorText("Terrain Material");
    ImGui::ColorEdit3("Base Color", &terrain.materialBaseColor.x);
    ImGui::SliderFloat("Brightness", &terrain.materialBrightness, 0.05f, 3.0f);
    ImGui::SliderFloat("Rock Noise", &terrain.materialNoiseStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Strata Lines", &terrain.materialStrataStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Strata Breakup", &terrain.materialStrataBreakupStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Specular", &terrain.materialSpecularStrength, 0.0f, 0.25f);
    ImGui::SliderFloat("Rim Light", &terrain.materialRimLightStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Backlight Rim Boost", &terrain.materialBacklightRimBoost, 0.0f, 2.0f);
    ImGui::SliderFloat("Floor Sand Shadow", &terrain.materialFloorSandShadowStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Detail Normal", &terrain.materialDetailNormalStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Micro Detail", &terrain.materialMicroDetailStrength, 0.0f, 2.0f);
    ImGui::Checkbox("Use Detail Cache", &terrain.useDetailTextureCache);
    ImGui::SliderFloat("Detail Cache Scale", &terrain.materialDetailCacheScale, 0.25f, 4.0f);
    ImGui::SliderFloat("Detail Tile Size", &terrain.materialDetailTileWorldSize, 32.0f, 240.0f, "%.1f");
    ImGui::SliderFloat("Near Detail Scale", &terrain.materialDetailNearScale, 0.25f, 3.0f, "%.2f");
    ImGui::SliderFloat("Far Detail Scale", &terrain.materialDetailFarScale, 0.15f, 1.5f, "%.2f");
    ImGui::SliderFloat("Detail Distance Blend", &terrain.materialDetailDistanceBlend, 40.0f, 420.0f, "%.1f");
    ImGui::Checkbox("Use Detail Normal Map", &terrain.useDetailNormalMap);
    ImGui::SliderFloat("Detail Normal Map Strength", &terrain.materialDetailNormalMapStrength, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Cache / Normal Hybrid", &terrain.materialDetailHybridBlend, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Invert Detail Normal Y", &terrain.invertDetailNormalY);
    ImGui::SliderFloat("Cavity AO", &terrain.materialCavityAoStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Sky Fill", &terrain.materialSkyFillStrength, 0.0f, 1.2f);
    if (ImGui::Button("Reset Terrain Material")) {
        terrain.materialBaseColor = {1.02f, 0.96f, 0.90f, 1.0f};
        terrain.materialBrightness = 1.0f;
        terrain.materialNoiseStrength = 1.0f;
        terrain.materialStrataStrength = 1.0f;
        terrain.materialStrataBreakupStrength = 0.92f;
        terrain.materialSpecularStrength = 0.135f;
        terrain.materialRimLightStrength = 0.84f;
        terrain.materialBacklightRimBoost = 0.72f;
        terrain.materialFloorSandShadowStrength = 0.38f;
        terrain.materialDetailNormalStrength = 1.18f;
        terrain.materialMicroDetailStrength = 1.16f;
        terrain.useDetailTextureCache = true;
        terrain.materialDetailCacheScale = 1.35f;
        terrain.materialDetailTileWorldSize = 72.0f;
        terrain.materialDetailNearScale = 1.75f;
        terrain.materialDetailFarScale = 0.72f;
        terrain.materialDetailDistanceBlend = 180.0f;
        terrain.useDetailNormalMap = true;
        terrain.materialDetailNormalMapStrength = 0.82f;
        terrain.materialDetailHybridBlend = 0.66f;
        terrain.invertDetailNormalY = false;
        terrain.materialCavityAoStrength = 0.82f;
        terrain.materialSkyFillStrength = 0.30f;
    }
    ImGui::SeparatorText("Cascaded Shadows");
    ImGui::Checkbox("CSM Enabled", &terrain.cascadeShadowEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Cascade Bounds", &terrain.showCascadeBounds);
    ImGui::SameLine();
    ImGui::Checkbox("Shadow Debug View", &terrain.showShadowDebugView);
    ImGui::SliderFloat("Shadow Bias", &terrain.cascadeShadowBias, 0.0001f, 0.0120f, "%.4f");
    ImGui::SliderFloat("Shadow Strength", &terrain.cascadeShadowStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Cascade 0 Split", &terrain.cascadeShadowSplit0, 20.0f, 240.0f, "%.1f");
    terrain.cascadeShadowSplit1 = (std::max)(terrain.cascadeShadowSplit1, terrain.cascadeShadowSplit0 + 10.0f);
    ImGui::SliderFloat("Cascade 1 Split", &terrain.cascadeShadowSplit1, terrain.cascadeShadowSplit0 + 10.0f, 520.0f, "%.1f");
    terrain.cascadeShadowSplit2 = (std::max)(terrain.cascadeShadowSplit2, terrain.cascadeShadowSplit1 + 10.0f);
    ImGui::SliderFloat("Cascade 2 Split", &terrain.cascadeShadowSplit2, terrain.cascadeShadowSplit1 + 10.0f, 1040.0f, "%.1f");
    terrain.cascadeShadowSplit3 = (std::max)(terrain.cascadeShadowSplit3, terrain.cascadeShadowSplit2 + 10.0f);
    ImGui::SliderFloat("Cascade 3 Split", &terrain.cascadeShadowSplit3, terrain.cascadeShadowSplit2 + 10.0f, 1800.0f, "%.1f");
    ImGui::SliderInt("Shadow Preview Cascade", &terrain.shadowDebugCascade, 0, 3);
    if (ImGui::Button("Reset CSM")) {
        terrain.cascadeShadowEnabled = true;
        terrain.cascadeShadowBias = 0.0018f;
        terrain.cascadeShadowStrength = 0.68f;
        terrain.cascadeShadowSplit0 = 120.0f;
        terrain.cascadeShadowSplit1 = 260.0f;
        terrain.cascadeShadowSplit2 = 520.0f;
        terrain.cascadeShadowSplit3 = 960.0f;
        terrain.shadowDebugCascade = 0;
    }
    ImGui::SeparatorText("Debris Hi-Z Occlusion");
    ImGui::Checkbox("Hi-Z Debug Preview", &terrain.showHiZDebugPreview);
    ImGui::SliderInt("Hi-Z Preview Mip", &terrain.hiZDebugMip, 0, 4);
    ImGui::SliderInt("Debris Occlusion Mip", &terrain.debrisOcclusionMip, 0, 4);
    ImGui::SliderFloat("Occlusion Strength", &terrain.debrisOcclusionStrength, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Occlusion Depth Bias", &terrain.debrisOcclusionDepthBias, 0.0f, 0.05f, "%.4f");
    int debrisOcclusionUpdateInterval = static_cast<int>(terrain.debrisOcclusionUpdateInterval);
    if (ImGui::SliderInt("Occlusion Update Interval", &debrisOcclusionUpdateInterval, 1, 8)) {
        terrain.debrisOcclusionUpdateInterval =
            static_cast<uint32_t>(std::clamp(debrisOcclusionUpdateInterval, 1, 8));
    }
    if (ImGui::Button("Reset Debris Occlusion")) {
        terrain.showHiZDebugPreview = false;
        terrain.hiZDebugMip = 3;
        terrain.debrisOcclusionMip = 2;
        terrain.debrisOcclusionStrength = 0.75f;
        terrain.debrisOcclusionDepthBias = 0.012f;
        terrain.debrisOcclusionUpdateInterval = 4;
    }
    ImGui::SeparatorText("Canyon Sun / Sky");
    ImGui::Checkbox("Use Canyon Sun", &terrain.useCanyonSunLighting);
    ImGui::ColorEdit3("Sun Color", &terrain.canyonSunColor.x);
    ImGui::SliderFloat3("Sun Direction", &terrain.canyonSunDirection.x, -1.0f, 1.0f);
    ImGui::SliderFloat("Sun Intensity", &terrain.canyonSunIntensity, 0.0f, 8.0f);
    if (ImGui::Button("Apply Canyon Sun")) {
        terrain.useCanyonSunLighting = true;
        terrain.canyonSunColor = {1.0f, 0.74f, 0.46f, 1.0f};
        terrain.canyonSunDirection = {-0.38f, -0.52f, 0.76f};
        terrain.canyonSunIntensity = 2.4f;
        terrain.materialSkyFillStrength = 0.30f;
        terrain.materialRimLightStrength = 0.84f;
        terrain.materialBacklightRimBoost = 0.72f;
    }
    ImGui::SeparatorText("Terrain Generation");
    ImGui::Checkbox("Freeze Course Runtime", &terrain.freezeCourseRuntime);
    ImGui::Checkbox("Auto Advance Rail Preview", &terrain.autoAdvancePreview);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Reload Preset", &terrain.autoReloadPreset);
    ImGui::Checkbox("Show Rail", &terrain.showRailPath);
    ImGui::SameLine();
    ImGui::Checkbox("Show Corridor", &terrain.showCorridor);
    ImGui::SameLine();
    ImGui::Checkbox("Show Chunks", &terrain.showChunks);
    ImGui::SameLine();
    ImGui::Checkbox("Show Spawns", &terrain.showSpawnCandidates);
    ImGui::SameLine();
    ImGui::Checkbox("Show Rock Scatter", &terrain.showRockScatter);
    ImGui::Checkbox("Show Volume Slice", &terrain.showVolumeSlice);
    ImGui::SameLine();
    ImGui::Checkbox("Show SDF Samples", &terrain.showSdfSamples);
    ImGui::SameLine();
    ImGui::Checkbox("Show Cascade Bounds", &terrain.showCascadeBounds);
    ImGui::DragFloat("Rail Preview Distance", &terrain.previewDistance, 1.0f, 0.0f, 10000.0f);
    ImGui::DragFloat("Rail Preview Speed", &terrain.previewSpeed, 1.0f, 0.0f, 400.0f);
    ImGui::InputScalar("Terrain Seed", ImGuiDataType_U32, &terrain.settings.seed);
    ImGui::DragFloat("Chunk Length", &terrain.settings.chunkLength, 1.0f, 10.0f, 300.0f);
    int visibleAheadChunks = static_cast<int>(terrain.settings.visibleAheadChunks);
    if (ImGui::SliderInt("Visible Ahead Chunks", &visibleAheadChunks, 1, 32)) {
        terrain.settings.visibleAheadChunks = static_cast<uint32_t>((std::max)(visibleAheadChunks, 1));
    }
    int visibleBehindChunks = static_cast<int>(terrain.settings.visibleBehindChunks);
    if (ImGui::SliderInt("Visible Behind Chunks", &visibleBehindChunks, 0, 16)) {
        terrain.settings.visibleBehindChunks = static_cast<uint32_t>((std::max)(visibleBehindChunks, 0));
    }
    ImGui::DragFloat("Corridor Radius", &terrain.settings.corridorRadius, 0.25f, 4.0f, 80.0f);
    ImGui::DragFloat("Canyon Half Width", &terrain.settings.canyonHalfWidth, 0.5f, 8.0f, 160.0f);
    ImGui::DragFloat("Wall Height", &terrain.settings.wallHeight, 0.5f, 4.0f, 180.0f);
    ImGui::DragFloat("Noise Strength", &terrain.settings.noiseStrength, 0.25f, 0.0f, 80.0f);
    ImGui::SliderFloat("Volume Roughness", &terrain.settings.volumeRoughness, 0.0f, 1.5f);
    ImGui::SliderFloat("Volume Arch Scale", &terrain.settings.volumeArchScale, 0.0f, 2.0f);
    ImGui::SliderFloat("SDF Carve Density", &terrain.settings.sdfCarveDensity, 0.0f, 1.0f);
    ImGui::SliderFloat("SDF Carve Strength", &terrain.settings.sdfCarveStrength, 0.0f, 1.2f);
    ImGui::SliderFloat("SDF Carve Scale", &terrain.settings.sdfCarveScale, 0.25f, 2.5f);
    ImGui::SliderFloat("Opening Silhouette Strength", &terrain.settings.openingSilhouetteStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Opening Silhouette Scale", &terrain.settings.openingSilhouetteScale, 0.35f, 2.5f);
    ImGui::DragFloat("Open Canyon Start", &terrain.settings.openCanyonStartDistance, 2.0f, 0.0f, 5000.0f);
    ImGui::DragFloat("Open Canyon Transition", &terrain.settings.openCanyonTransitionLength, 2.0f, 1.0f, 2000.0f);
    ImGui::SliderFloat("Open Canyon Strength", &terrain.settings.openCanyonStrength, 0.0f, 1.0f);
    ImGui::DragFloat("Open Canyon Far Wall Distance", &terrain.settings.openCanyonFarWallDistance, 2.0f, 20.0f, 1000.0f);
    ImGui::DragFloat("Open Canyon Far Wall Height", &terrain.settings.openCanyonFarWallHeight, 2.0f, 20.0f, 1000.0f);
    ImGui::SliderFloat("Open Canyon Layer Spread", &terrain.settings.openCanyonLayerSpread, 0.0f, 2.0f);
    int surfaceLongitudinalSteps = static_cast<int>(terrain.settings.surfaceLongitudinalSteps);
    if (ImGui::SliderInt("Surface Length Steps", &surfaceLongitudinalSteps, 12, 64)) {
        terrain.settings.surfaceLongitudinalSteps = static_cast<uint32_t>(std::clamp(surfaceLongitudinalSteps, 12, 64));
    }
    int surfaceRadialSegments = static_cast<int>(terrain.settings.surfaceRadialSegments);
    if (ImGui::SliderInt("Surface Radial Segments", &surfaceRadialSegments, 16, 96)) {
        terrain.settings.surfaceRadialSegments = static_cast<uint32_t>(std::clamp(surfaceRadialSegments, 16, 96));
    }
    ImGui::SliderFloat("LOD Near Distance", &terrain.settings.lodNearDistance, 40.0f, 1000.0f);
    ImGui::SliderFloat("LOD Far Distance", &terrain.settings.lodFarDistance, 80.0f, 1800.0f);
    ImGui::SliderFloat("Rock Pillar Density", &terrain.settings.rockPillarDensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Rock Scatter Density", &terrain.settings.rockScatterDensity, 0.0f, 1.5f);
    ImGui::SliderFloat("Rock Scatter Scale", &terrain.settings.rockScatterScale, 0.2f, 2.5f);
    ImGui::SliderFloat("Rock Embed Strength", &terrain.settings.rockEmbedStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Contact Pebbles", &terrain.settings.rockContactPebbleDensity, 0.0f, 1.5f);
    ImGui::SliderFloat("Floor Pebble Density", &terrain.settings.floorPebbleDensity, 0.0f, 1.5f);
    ImGui::SliderFloat("Rock Cluster Strength", &terrain.settings.rockClusterStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Rock Root Shadow", &terrain.settings.rockRootShadowStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Rock Mother Blend", &terrain.settings.rockMotherBlendStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Rock Material Variation", &terrain.settings.rockMaterialVariation, 0.0f, 1.0f);
    ImGui::SliderFloat("Mother Rock Erosion", &terrain.settings.motherRockErosionStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Large Scale Erosion", &terrain.settings.largeScaleErosionStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Surface Breakup Density", &terrain.settings.surfaceBreakupDensity, 0.0f, 1.5f);
    ImGui::SliderFloat("Overhang Feature Density", &terrain.settings.archDensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Dust Zone Density", &terrain.settings.dustZoneDensity, 0.0f, 1.0f);
    ImGui::Checkbox("Show VFX Zones", &terrain.showVfxZones);
    if (ImGui::Button("Save Terrain Preset")) {
        terrain.requestSavePreset = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Terrain Preset")) {
        terrain.requestLoadPreset = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Terrain Preset")) {
        terrain.requestReloadPreset = true;
    }

    ImGui::SeparatorText("VFX Model Objects");
    ImGui::Checkbox("Show VFX Model Objects", &runtimeState.showVfxModelObjects);
    const char* modelObjectLabels[] = {
        "Object 0",
        "Object 1",
        "Object 2",
    };
    int selectedModelObject = static_cast<int>(runtimeState.selectedVfxModelObjectIndex);
    if (ImGui::Combo(
            "VFX Model Object",
            &selectedModelObject,
            modelObjectLabels,
            IM_ARRAYSIZE(modelObjectLabels))) {
        runtimeState.selectedVfxModelObjectIndex =
            static_cast<uint32_t>((std::max)(0, selectedModelObject));
    }
    runtimeState.selectedVfxModelObjectIndex = (std::min)(
        runtimeState.selectedVfxModelObjectIndex,
        static_cast<uint32_t>(runtimeState.vfxModelObjects.size() - 1));
    RuntimeVfxModelObjectState& selectedObject =
        runtimeState.vfxModelObjects[runtimeState.selectedVfxModelObjectIndex];
    ImGui::Checkbox("Object Visible", &selectedObject.visible);
    const char* vfxModelLabels[] = {
        "ball",
        "animated_cube",
        "organic_arch_large",
        "rib_tunnel_wall",
        "root_spire_column",
        "curved_canyon_wall",
        "vista_hole_wall",
        "spire_broken_bridge_arc",
        "multi_material_demo",
        "training_sword",
    };
    int selectedModelIndex = static_cast<int>(selectedObject.modelIndex);
    if (ImGui::Combo(
            "Object Model",
            &selectedModelIndex,
            vfxModelLabels,
            IM_ARRAYSIZE(vfxModelLabels))) {
        selectedObject.modelIndex =
            static_cast<uint32_t>((std::max)(0, selectedModelIndex));
    }
    ImGui::DragFloat3(
        "Object Scale",
        reinterpret_cast<float*>(&selectedObject.transform.scale),
        0.01f,
        0.01f,
        10.0f);
    ImGui::DragFloat3(
        "Object Rotate",
        reinterpret_cast<float*>(&selectedObject.transform.rotate),
        0.01f,
        -3.14f,
        3.14f);
    ImGui::DragFloat3(
        "Object Translate",
        reinterpret_cast<float*>(&selectedObject.transform.translate),
        0.01f,
        -100.0f,
        100.0f);

    ImGui::DragFloat2("UVTranslate", &runtimeState.uvTransformSprite.translate.x, 0.01f,
        -10.0f, 10.0f);
    ImGui::DragFloat2("UVScale", &runtimeState.uvTransformSprite.scale.x, 0.01f, -10.0f,
        10.0f);
    ImGui::SliderAngle("UVRotate", &runtimeState.uvTransformSprite.rotate.z);

    ImGui::DragFloat3(
        "EmitterTranslate",
        &runtimeState.emitter.transform.translate.x,
        0.01f,
        -100.0f,
        100.0f);
    if (ImGui::Button("Place Emitter On Front Hit")) {
        runtimeState.emitter.transform.translate = FrontHitEffectPosition(runtimeState);
    }

    ImGui::DragFloat3("Field Accel", &runtimeState.accelerationField.acceleration.x, 0.1f);
    ImGui::DragFloat3("Field Min", &runtimeState.accelerationField.area.min.x, 0.1f);
    ImGui::DragFloat3("Field Max", &runtimeState.accelerationField.area.max.x, 0.1f);

    if (ImGui::Button("Add Particle (Emitter)") && onAddParticle) {
        onAddParticle();
    }

    ImGui::DragFloat3("Point Pos", &runtimeState.pointLightData.position.x, 0.05f);
    ImGui::DragFloat("Point Intensity", &runtimeState.pointLightData.intensity, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloat("Point Radius", &runtimeState.pointLightData.radius, 0.1f, 0.1f, 100.0f);
    ImGui::DragFloat("Point Decay", &runtimeState.pointLightData.decay, 0.05f, 0.1f, 8.0f);
}

void DrawVfxRuntimeControlsPanel(
    const VfxRuntimeControlsPanelInput& input) {
    if (input.runtimeState == nullptr || input.effectRuntime == nullptr ||
        input.trailMeshStreamStartupTelemetryFrames == nullptr) {
        return;
    }

    AppRuntimeState& runtimeState = *input.runtimeState;
    EffectRuntime& effectRuntime = *input.effectRuntime;
    uint32_t& trailMeshStreamStartupTelemetryFrames = *input.trailMeshStreamStartupTelemetryFrames;

    bool runtimePaused = effectRuntime.IsPaused();
    if (ImGui::Checkbox("Pause Effect Runtime", &runtimePaused)) {
        effectRuntime.SetPaused(runtimePaused);
    }
    float runtimeSpeed = effectRuntime.SpeedMultiplier();
    if (ImGui::SliderFloat("Effect Runtime Speed", &runtimeSpeed, 0.0f, 4.0f)) {
        effectRuntime.SetSpeedMultiplier(runtimeSpeed);
    }
    ImGui::Checkbox("Auto Play VFX Demo", &runtimeState.vfx.autoPlayVfxDemo);
    ImGui::Checkbox("VFX Showcase Mode", &runtimeState.vfx.showcaseMode);
    if (runtimeState.vfx.showcaseMode) {
        ApplyVfxShowcaseMode(runtimeState);
    }
    ImGui::Checkbox("Click Viewport To Fire Ice", &runtimeState.vfx.iceProjectileClickToFire);
    ImGui::Checkbox("Loop Electric Orb Strike", &runtimeState.vfx.electricOrbStrikeLoop);
    ImGui::Text(
        "Electric Orb Strike: active=%s timer=%.2f duration=%.2f",
        runtimeState.vfx.electricOrbStrikeActive ? "true" : "false",
        runtimeState.vfx.electricOrbStrikeTimer,
        runtimeState.vfx.electricOrbStrikeDuration);
    ImGui::SeparatorText("VFX Visibility");
    ImGui::Checkbox("Particles", &runtimeState.vfx.enableParticles);
    ImGui::SameLine();
    ImGui::Checkbox("Trails", &runtimeState.vfx.enableTrails);
    ImGui::SameLine();
    ImGui::Checkbox("Beams", &runtimeState.vfx.enableBeams);
    ImGui::SameLine();
    ImGui::Checkbox("Distortion", &runtimeState.vfx.enableDistortions);
    ImGui::SameLine();
    ImGui::Checkbox("Rings", &runtimeState.vfx.enableRings);
    ImGui::SameLine();
    ImGui::Checkbox("Cylinders", &runtimeState.vfx.enableCylinders);
    ImGui::SameLine();
    ImGui::Checkbox("Electric Orb", &runtimeState.vfx.enableElectricOrbStrike);
    ImGui::Checkbox("Skinned Surface VFX", &runtimeState.vfx.enableSkinnedSurfaceVfx);
    ImGui::Checkbox("Trail Mesh Stream", &runtimeState.vfx.enableTrailMeshStream);
    ImGui::Checkbox("Trail Mesh Stream Safety Fallback", &runtimeState.vfx.enableTrailMeshStreamAutoFallback);
    if (ImGui::Checkbox(
            "Trail Mesh Stream Startup Telemetry Log",
            &runtimeState.vfx.enableTrailMeshStreamStartupTelemetry)) {
        trailMeshStreamStartupTelemetryFrames = 0;
    }
    ImGui::Text(
        "trailMeshStreamStartupTelemetry=%s frames=%u/300",
        runtimeState.vfx.enableTrailMeshStreamStartupTelemetry ? "on" : "off",
        trailMeshStreamStartupTelemetryFrames);
    ImGui::SliderFloat("Demo Spawn Interval", &runtimeState.vfx.autoPlayVfxInterval, 0.1f, 2.0f, "%.2f");
    ImGui::SliderFloat("Demo Spawn Radius", &runtimeState.vfx.autoPlayVfxRadius, 0.0f, 8.0f, "%.2f");
    const Vector3 heldEffectPosition = FrontHitEffectPosition(runtimeState);
    ImGui::SeparatorText("Held Hit Effects");
    ImGui::Checkbox("Hold Plane Burst", &runtimeState.vfx.holdHitPlaneBurst);
    ImGui::SameLine();
    ImGui::Checkbox("Hold Ring", &runtimeState.vfx.holdHitRing);
    ImGui::SameLine();
    ImGui::Checkbox("Hold Cylinder", &runtimeState.vfx.holdHitCylinder);
    ImGui::SameLine();
    ImGui::Checkbox("Hold Cylinder Combo", &runtimeState.vfx.holdHitCylinderCombo);
    SyncHeldEffect(
        effectRuntime,
        runtimeState.vfx.holdHitPlaneBurst,
        runtimeState.vfx.holdHitPlaneBurstInstanceId,
        "hit_plane_burst",
        heldEffectPosition,
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f});
    SyncHeldEffect(
        effectRuntime,
        runtimeState.vfx.holdHitRing,
        runtimeState.vfx.holdHitRingInstanceId,
        "hit_ring",
        heldEffectPosition,
        {0.9f, 0.95f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f});
    SyncHeldEffect(
        effectRuntime,
        runtimeState.vfx.holdHitCylinder,
        runtimeState.vfx.holdHitCylinderInstanceId,
        "hit_cylinder",
        heldEffectPosition,
        {0.65f, 0.85f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f});
    SyncHeldEffect(
        effectRuntime,
        runtimeState.vfx.holdHitCylinderCombo,
        runtimeState.vfx.holdHitCylinderComboInstanceId,
        "hit_cylinder_combo",
        heldEffectPosition,
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f});
    if (ImGui::Button("Play warp_core")) {
        effectRuntime.PlayEffectWithParams(
            "warp_core",
            runtimeState.emitter.transform.translate,
            {1.0f, 0.75f, 0.35f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Play ice_projectile")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.enableParticles = true;
        runtimeState.vfx.enableTrails = true;
        runtimeState.vfx.enableRings = true;
        runtimeState.vfx.enableCylinders = true;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.vfx.showcaseMode = true;
        ApplyVfxShowcaseMode(runtimeState);
        runtimeState.vfx.iceProjectileStart = {0.0f, -1.55f, -3.05f};
        runtimeState.vfx.iceProjectileTarget = {2.5f, 0.7f, 0.42f};
        runtimeState.vfx.iceProjectilePreviewActive = true;
        runtimeState.vfx.iceProjectileImpactSpawned = false;
        runtimeState.vfx.iceProjectileInstanceId = 0;
        runtimeState.vfx.iceProjectileTimer = 0.0f;
        effectRuntime.ClearInstances();
        for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState.vfx.iceProjectileShots) {
            shot = {};
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Electric Orb Strike")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        ResetIceProjectileShots(runtimeState.vfx);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.showcaseMode = true;
        runtimeState.vfx.enableParticles = false;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.vfx.enableRings = false;
        runtimeState.vfx.enableCylinders = false;
        runtimeState.vfx.enableElectricOrbStrike = true;
        runtimeState.vfx.electricOrbStrikeActive = true;
        runtimeState.vfx.electricOrbStrikeTimer = 0.0f;
        runtimeState.vfx.electricOrbStrikeDuration = 4.25f;
        runtimeState.showAnimatedCube = false;
        runtimeState.showSkinnedModel = false;
        runtimeState.showVfxModelObjects = false;
        effectRuntime.ClearInstances();
    }
    if (ImGui::Button("Play hit_plane_burst")) {
        runtimeState.vfx.enableParticles = true;
        effectRuntime.PlayEffectWithParams(
            "hit_plane_burst",
            runtimeState.emitter.transform.translate,
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Play hit_ring")) {
        runtimeState.vfx.enableRings = true;
        effectRuntime.PlayEffectWithParams(
            "hit_ring",
            runtimeState.emitter.transform.translate,
            {0.9f, 0.95f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Play hit_cylinder")) {
        runtimeState.vfx.enableCylinders = true;
        effectRuntime.PlayEffectWithParams(
            "hit_cylinder",
            runtimeState.emitter.transform.translate,
            {0.65f, 0.85f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus hit_combo")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.enableParticles = true;
        runtimeState.vfx.enableRings = true;
        runtimeState.vfx.enableCylinders = false;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.emitter.transform.translate = FrontHitEffectPosition(runtimeState);
        effectRuntime.ClearInstances();
        ResetIceProjectileShots(runtimeState.vfx);
        effectRuntime.PlayEffectWithParams(
            "hit_ring_plane_combo",
            runtimeState.emitter.transform.translate,
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus hit_cylinder")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.enableParticles = false;
        runtimeState.vfx.enableRings = false;
        runtimeState.vfx.enableCylinders = true;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.emitter.transform.translate = FrontHitEffectPosition(runtimeState);
        effectRuntime.ClearInstances();
        ResetIceProjectileShots(runtimeState.vfx);
        effectRuntime.PlayEffectWithParams(
            "hit_cylinder",
            runtimeState.emitter.transform.translate,
            {0.65f, 0.85f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus cylinder_combo")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.enableParticles = true;
        runtimeState.vfx.enableRings = true;
        runtimeState.vfx.enableCylinders = true;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.emitter.transform.translate = FrontHitEffectPosition(runtimeState);
        effectRuntime.ClearInstances();
        ResetIceProjectileShots(runtimeState.vfx);
        effectRuntime.PlayEffectWithParams(
            "hit_cylinder_combo",
            runtimeState.emitter.transform.translate,
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus hit_plane_burst")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        runtimeState.vfx.autoPlayVfxDemo = false;
        runtimeState.vfx.enableParticles = true;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.vfx.enableRings = false;
        runtimeState.vfx.enableCylinders = false;
        runtimeState.emitter.transform.translate = FrontHitEffectPosition(runtimeState);
        effectRuntime.ClearInstances();
        ResetIceProjectileShots(runtimeState.vfx);
        effectRuntime.PlayEffectWithParams(
            "hit_plane_burst",
            runtimeState.emitter.transform.translate,
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Play authoring_metadata_demo")) {
        effectRuntime.PlayEffectWithParams(
            "authoring_metadata_demo",
            runtimeState.emitter.transform.translate,
            {0.55f, 0.9f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Play authoring_registry_only_demo")) {
        effectRuntime.PlayEffectWithParams(
            "authoring_registry_only_demo",
            runtimeState.emitter.transform.translate,
            {0.9f, 0.65f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Play unknown_technique_demo")) {
        effectRuntime.PlayEffectWithParams(
            "authoring_unknown_technique_demo",
            runtimeState.emitter.transform.translate,
            {1.0f, 0.45f, 0.25f, 1.0f},
            {1.0f, 1.0f, 1.0f});
    }
    if (ImGui::Button("Clear Effects")) {
        DisableHeldHitEffects(runtimeState, effectRuntime);
        ResetIceProjectileShots(runtimeState.vfx);
        runtimeState.vfx.electricOrbStrikeActive = false;
        runtimeState.vfx.electricOrbStrikeTimer = 0.0f;
        effectRuntime.ClearInstances();
    }
}
