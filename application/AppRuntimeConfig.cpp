#include "AppRuntimeConfig.h"
#include "course/RailShortCourse.h"

#include "AppRuntimeState.h"
#include "AppVfxRuntimeState.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {
bool HasEnvironmentVariable(const char* name) {
    return GetEnvironmentVariableA(name, nullptr, 0) > 0;
}

uint32_t GetEnvironmentUInt(const char* name, uint32_t fallback) {
    char value[32]{};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value)) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<uint32_t>(parsed);
}

float GetEnvironmentFloat(const char* name, float fallback) {
    char value[32]{};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value)) {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value) {
        return fallback;
    }
    return parsed;
}

std::string GetEnvironmentString(const char* name, const std::string& fallback) {
    char value[512]{};
    const DWORD length = GetEnvironmentVariableA(
        name,
        value,
        static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value)) {
        return fallback;
    }
    return std::string(value, length);
}
} // namespace

AppStartupScene ParseAppStartupSceneArguments(
    int argumentCount,
    const wchar_t* const* arguments) {
    if (argumentCount <= 1 || arguments == nullptr) {
        return AppStartupScene::RailShooter;
    }

    bool explicitVfxPreview = false;
    bool explicitMultiMaterialShowcase = false;
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        if (arguments[argumentIndex] == nullptr) {
            continue;
        }
        const std::wstring_view argument(arguments[argumentIndex]);
        if (argument == L"--rail-shooter" ||
            argument == L"--combat-loop-10s" ||
            argument == L"--combat-loop-expansion" ||
            argument == L"--combat-loop-ui-proof" ||
            argument == L"--combat-loop-ui-proof=interrupt" ||
            argument == L"--combat-loop-ui-proof=shootdown" ||
            argument == L"--combat-loop-ui-proof=evade") {
            return AppStartupScene::RailShooter;
        }
        if (argument == L"--multi-material-showcase") {
            explicitMultiMaterialShowcase = true;
        }
        if (argument == L"--vfx-preview") {
            explicitVfxPreview = true;
        }
    }
    if (explicitMultiMaterialShowcase) {
        return AppStartupScene::MultiMaterialShowcase;
    }
    return explicitVfxPreview
        ? AppStartupScene::VfxPreview
        : AppStartupScene::RailShooter;
}

bool ParseCombatLoop10SecondModeArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept {
    if (argumentCount <= 1 || arguments == nullptr) {
        return false;
    }
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        if (arguments[argumentIndex] != nullptr &&
            std::wstring_view(arguments[argumentIndex]) == L"--combat-loop-10s") {
            return true;
        }
    }
    return false;
}

AppStartupScene ResolveAppStartupSceneFromCommandLine() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return AppStartupScene::RailShooter;
    }

    const AppStartupScene startupScene =
        ParseAppStartupSceneArguments(argumentCount, arguments);
    LocalFree(arguments);
    return startupScene;
}

bool ResolveCombatLoop10SecondModeFromCommandLine() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return false;
    }

    const bool enabled =
        ParseCombatLoop10SecondModeArguments(argumentCount, arguments);
    LocalFree(arguments);
    return enabled;
}

bool ParseCombatLoopExpansionModeArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept {
    if (argumentCount <= 1 || arguments == nullptr) {
        return false;
    }
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        if (arguments[argumentIndex] != nullptr &&
            std::wstring_view(arguments[argumentIndex]) ==
                L"--combat-loop-expansion") {
            return true;
        }
    }
    return false;
}

bool ResolveCombatLoopExpansionModeFromCommandLine() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return false;
    }
    const bool enabled =
        ParseCombatLoopExpansionModeArguments(argumentCount, arguments);
    LocalFree(arguments);
    return enabled;
}

bool ParseCombatLoopDefenseUiProofArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept {
    return ParseCombatLoopDefenseUiProofVariantArguments(
               argumentCount, arguments) !=
        CombatLoopDefenseUiProofVariant::Disabled;
}

CombatLoopDefenseUiProofVariant ParseCombatLoopDefenseUiProofVariantArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept {
    if (argumentCount <= 1 || arguments == nullptr) {
        return CombatLoopDefenseUiProofVariant::Disabled;
    }
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        if (arguments[argumentIndex] == nullptr) continue;
        const std::wstring_view argument(arguments[argumentIndex]);
        if (argument == L"--combat-loop-ui-proof") {
            return CombatLoopDefenseUiProofVariant::Sequence;
        }
        if (argument == L"--combat-loop-ui-proof=interrupt") {
            return CombatLoopDefenseUiProofVariant::Interrupt;
        }
        if (argument == L"--combat-loop-ui-proof=shootdown") {
            return CombatLoopDefenseUiProofVariant::ShootDown;
        }
        if (argument == L"--combat-loop-ui-proof=evade") {
            return CombatLoopDefenseUiProofVariant::Evade;
        }
    }
    return CombatLoopDefenseUiProofVariant::Disabled;
}

bool ResolveCombatLoopDefenseUiProofFromCommandLine() {
    return ResolveCombatLoopDefenseUiProofVariantFromCommandLine() !=
        CombatLoopDefenseUiProofVariant::Disabled;
}

CombatLoopDefenseUiProofVariant
ResolveCombatLoopDefenseUiProofVariantFromCommandLine() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return CombatLoopDefenseUiProofVariant::Disabled;
    }
    const CombatLoopDefenseUiProofVariant variant =
        ParseCombatLoopDefenseUiProofVariantArguments(argumentCount, arguments);
    LocalFree(arguments);
    return variant;
}

void ResetMultiMaterialShowcaseHumanoidPose(AppRuntimeState& runtimeState) {
    runtimeState.skinnedModelTransform.scale = {1.40f, 1.40f, 1.40f};
    runtimeState.skinnedModelTransform.rotate = {0.0f, 0.0f, 0.0f};
    runtimeState.skinnedModelTransform.translate = {-0.90f, -1.25f, -1.0f};
}

float ResolveHumanoidMovementYaw(float moveX, float moveY) noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    constexpr float kDirectionEpsilon = 0.0001f;

    if (!std::isfinite(moveX) || !std::isfinite(moveY) ||
        std::hypot(moveX, moveY) <= kDirectionEpsilon) {
        return 0.0f;
    }

    // The submission humanoid faces local -Z at yaw zero, while movement input
    // uses +Z as forward. Apply the asset-axis correction only when movement
    // selects a facing direction; the idle/reset presentation remains yaw zero.
    float correctedYaw = std::fmod(std::atan2(moveX, moveY) + kPi, kTwoPi);
    if (correctedYaw < 0.0f) {
        correctedYaw += kTwoPi;
    }
    return correctedYaw;
}

bool DidHumanoidMovementStart(
    float previousMoveMagnitude,
    float currentMoveMagnitude) noexcept {
    constexpr float kMovementDeadZone = 0.08f;
    if (!std::isfinite(previousMoveMagnitude) ||
        !std::isfinite(currentMoveMagnitude)) {
        return false;
    }
    return previousMoveMagnitude <= kMovementDeadZone &&
        currentMoveMagnitude > kMovementDeadZone;
}

float AdvanceHumanoidMovementYaw(
    float currentYaw,
    float targetYaw,
    float maxDelta) noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    if (!std::isfinite(currentYaw) || !std::isfinite(targetYaw) ||
        !std::isfinite(maxDelta) || maxDelta < 0.0f) {
        return std::isfinite(currentYaw) ? currentYaw : 0.0f;
    }

    // Compare the desired movement direction with the model's actual facing,
    // not with the previous input sample. Consecutive gamepad samples can each
    // differ by less than a threshold while their accumulated turn is large.
    float shortestDelta = std::fmod(targetYaw - currentYaw + kPi, kTwoPi);
    if (shortestDelta < 0.0f) {
        shortestDelta += kTwoPi;
    }
    shortestDelta -= kPi;

    const float appliedDelta =
        (std::clamp)(shortestDelta, -maxDelta, maxDelta);
    float result = std::fmod(currentYaw + appliedDelta, kTwoPi);
    if (result < 0.0f) {
        result += kTwoPi;
    }
    return result;
}

bool BeginSkinnedAnimationBlend(
    AppRuntimeState& runtimeState,
    unsigned int targetModelIndex,
    float durationSeconds) noexcept {
    if (runtimeState.skinnedAnimationBlend.active ||
        targetModelIndex == runtimeState.selectedSkinnedModelIndex) {
        return false;
    }

    RuntimeSkinnedAnimationBlendState& blend =
        runtimeState.skinnedAnimationBlend;
    blend = {};
    blend.active = true;
    blend.fromModelIndex = runtimeState.selectedSkinnedModelIndex;
    blend.toModelIndex = targetModelIndex;
    blend.fromTime = runtimeState.animatedCubeTime;
    blend.toTime = 0.0f;
    blend.duration = (std::max)(0.0f, durationSeconds);
    blend.alpha = blend.duration <= 0.0f ? 1.0f : 0.0f;
    return true;
}

float AdvanceSkinnedAnimationBlend(
    AppRuntimeState& runtimeState,
    float deltaTime) noexcept {
    RuntimeSkinnedAnimationBlendState& blend =
        runtimeState.skinnedAnimationBlend;
    if (!blend.active) {
        return blend.alpha;
    }

    blend.elapsed += (std::max)(0.0f, deltaTime);
    blend.alpha = blend.duration <= 0.0f
        ? 1.0f
        : (std::clamp)(blend.elapsed / blend.duration, 0.0f, 1.0f);
    return blend.alpha;
}

void CompleteSkinnedAnimationBlend(AppRuntimeState& runtimeState) noexcept {
    RuntimeSkinnedAnimationBlendState& blend =
        runtimeState.skinnedAnimationBlend;
    if (!blend.active) {
        return;
    }
    runtimeState.selectedSkinnedModelIndex = blend.toModelIndex;
    runtimeState.animatedCubeTime = blend.toTime;
    blend.active = false;
    blend.alpha = 1.0f;
}

void CancelSkinnedAnimationBlend(AppRuntimeState& runtimeState) noexcept {
    runtimeState.skinnedAnimationBlend = {};
}

bool ShouldAdvancePreviewRuntime(
    bool editorRuntimeAdvance,
    bool submissionShowcaseEnabled) noexcept {
    // Editor previews remain controlled by Play/Sim/Step. The submission scene
    // is a self-running executable, so its animation and GPU emitters must keep
    // advancing even while the surrounding editor play session is stopped.
    return editorRuntimeAdvance || submissionShowcaseEnabled;
}

void ApplyMultiMaterialShowcasePresentationDefaults(AppRuntimeState& runtimeState) {
    constexpr float kPi = 3.14159265358979323846f;

    // Keep the submission shot deterministic. Editor state, autosave recovery,
    // and the previously opened scene must not change the presentation camera.
    runtimeState.camera.enableDebugInput = false;
    runtimeState.camera.transform.scale = {1.0f, 1.0f, 1.0f};
    runtimeState.camera.transform.rotate = {0.0f, 0.0f, 0.0f};
    runtimeState.camera.transform.translate = {0.0f, 0.0f, -5.25f};
    runtimeState.camera.fovY = 0.26f * kPi;
    runtimeState.camera.nearZ = 0.1f;
    runtimeState.camera.farZ = 100.0f;

    runtimeState.clearColor[0] = 0.008f;
    runtimeState.clearColor[1] = 0.012f;
    runtimeState.clearColor[2] = 0.025f;
    runtimeState.clearColor[3] = 1.0f;

    runtimeState.materialData.color = {1.0f, 1.0f, 1.0f, 1.0f};
    runtimeState.materialData.enableLighting = true;
    runtimeState.materialData.uvTransform = MakeIdentity4x4();
    runtimeState.materialData.shininess = 18.0f;
    runtimeState.materialData.environmentCoefficient = 0.02f;
    runtimeState.materialData.specularMode = 1;

    runtimeState.directionalLightData.color = {1.0f, 0.94f, 0.86f, 1.0f};
    runtimeState.directionalLightData.direction = {0.35f, -1.0f, -0.45f};
    runtimeState.directionalLightData.intensity = 1.28f;
    runtimeState.pointLightData.color = {0.48f, 0.64f, 1.0f, 1.0f};
    runtimeState.pointLightData.position = {-1.75f, 1.65f, -2.25f};
    runtimeState.pointLightData.intensity = 0.48f;
    runtimeState.pointLightData.radius = 10.0f;
    runtimeState.pointLightData.decay = 2.0f;
    runtimeState.spotLight.intensity = 0.0f;

    runtimeState.useMonsterBall = false;
    runtimeState.showAnimatedCube = false;
    runtimeState.showSkinnedModel = true;
    runtimeState.showSkeletonDebug = false;
    runtimeState.showSkybox = false;
    runtimeState.showProceduralBackdrop = false;
    runtimeState.showSprite = false;
    runtimeState.showVfxModelObjects = true;
    runtimeState.submissionShowcase = {};
    runtimeState.submissionShowcase.enabled = true;
    CancelSkinnedAnimationBlend(runtimeState);

    // The submission executable must demonstrate the Bone Socket -> Effect ->
    // GPU Particle path without requiring an environment variable or editor UI.
    runtimeState.handParticleAttachment = {};
    runtimeState.handParticleAttachment.enabled = true;
    runtimeState.handParticleAttachment.jointName = "mixamorig:RightHand";
    runtimeState.handParticleAttachment.effectName = "hand_socket_particle";
    runtimeState.handParticleAttachment.socketOffset.translate = {0.0f, 0.10f, -0.04f};
    runtimeState.handParticleAttachment.color = {0.35f, 0.85f, 1.0f, 1.0f};
    runtimeState.handParticleAttachment.effectScale = {0.90f, 0.90f, 0.90f};
    // A second, deliberately compact emitter makes the left hand's Bone
    // Socket origin immediately readable beside the wider blue right-hand VFX.
    runtimeState.leftHandParticleAttachment = {};
    runtimeState.leftHandParticleAttachment.enabled = true;
    runtimeState.leftHandParticleAttachment.jointName = "mixamorig:LeftHand";
    runtimeState.leftHandParticleAttachment.effectName = "left_hand_socket_particle";
    runtimeState.leftHandParticleAttachment.socketOffset.translate = {0.0f, 0.07f, -0.04f};
    runtimeState.leftHandParticleAttachment.color = {1.0f, 0.12f, 0.04f, 1.0f};
    runtimeState.leftHandParticleAttachment.effectScale = {0.90f, 0.90f, 0.90f};

    // Submission evidence: attach the shared procedural training sword to the
    // animated right hand without requiring an editor toggle or environment
    // variable. Keep the first visible reference pose aligned to the authored
    // +Y blade axis, enlarge it enough to read, and bias it toward the camera
    // so the blade cannot be hidden inside the hand/body depth range.
    runtimeState.weaponAttachment = {};
    runtimeState.weaponAttachment.enabled = true;
    runtimeState.weaponAttachment.jointName = "mixamorig:RightHand";
    runtimeState.weaponAttachment.socketOffset.scale = {1.10f, 1.10f, 1.10f};
    runtimeState.weaponAttachment.socketOffset.rotate = {0.0f, 0.0f, 0.0f, 1.0f};
    runtimeState.weaponAttachment.socketOffset.translate = {0.0f, 0.02f, -0.12f};
    runtimeState.weaponDrawTelemetry = {};

    runtimeState.vfx.enableParticles = true;
    runtimeState.vfx.enableTrails = false;
    runtimeState.vfx.enableBeams = false;
    runtimeState.vfx.enableDistortions = false;
    runtimeState.vfx.enableRings = false;
    runtimeState.vfx.enableCylinders = false;
    runtimeState.vfx.enableElectricOrbStrike = false;
    // Submission effects own their state, render, and indirect-argument
    // buffers. This avoids interference from the shared VFX pool and keeps the
    // executable result independent of editor telemetry/ImGui execution.
    // The submission effects have a fixed, independently validated workload.
    // Keep them on the dedicated GPU storage path so the shared VFX pool cannot
    // consume or overwrite their indirect draw arguments before rasterization.
    runtimeState.vfx.enableParticleDedicatedResources = true;
    runtimeState.vfx.enableParticleDedicatedResourceProbe = false;
    runtimeState.vfx.particleDedicatedResourceFallbackActive = false;

    ResetMultiMaterialShowcaseHumanoidPose(runtimeState);
    runtimeState.playAnimatedCube = true;
    runtimeState.loopAnimatedCube = true;
    runtimeState.animatedCubeTime = 0.0f;
    runtimeState.animatedCubeSpeed = 1.0f;
}

void ApplyEnvironmentRuntimeConfig(AppRuntimeState& runtimeState) {
    ApplyEnvironmentRuntimeConfig(runtimeState.vfx);

    if (HasEnvironmentVariable("GE3_SHOW_SKINNED")) {
        runtimeState.showSkinnedModel = true;
    }
    if (HasEnvironmentVariable("GE3_SHOW_SKELETON")) {
        runtimeState.showSkeletonDebug = true;
    }
    if (HasEnvironmentVariable("GE3_HIDE_ANIMATED_CUBE")) {
        runtimeState.showAnimatedCube = false;
    }
    if (HasEnvironmentVariable("GE3_HIDE_MAIN_MODEL")) {
        runtimeState.useMonsterBall = false;
    }
    runtimeState.selectedSkinnedModelIndex =
        GetEnvironmentUInt("GE3_SKINNED_MODEL_INDEX", runtimeState.selectedSkinnedModelIndex);
    for (uint32_t objectIndex = 0;
         objectIndex < runtimeState.vfxModelObjects.size();
         ++objectIndex) {
        const std::string variableName =
            "GE3_VFX_MODEL_" + std::to_string(objectIndex) + "_INDEX";
        runtimeState.vfxModelObjects[objectIndex].modelIndex = GetEnvironmentUInt(
            variableName.c_str(),
            runtimeState.vfxModelObjects[objectIndex].modelIndex);
    }
    if (HasEnvironmentVariable("GE3_HAND_PARTICLE")) {
        runtimeState.handParticleAttachment.enabled = true;
        runtimeState.showSkinnedModel = true;
        runtimeState.vfx.enableParticles = true;
        if (!HasEnvironmentVariable("GE3_SKINNED_MODEL_INDEX")) {
            runtimeState.selectedSkinnedModelIndex = 1;
        }
    }
    runtimeState.handParticleAttachment.jointName = GetEnvironmentString(
        "GE3_HAND_PARTICLE_JOINT",
        runtimeState.handParticleAttachment.jointName);
    runtimeState.handParticleAttachment.effectName = GetEnvironmentString(
        "GE3_HAND_PARTICLE_EFFECT",
        runtimeState.handParticleAttachment.effectName);
    runtimeState.handParticleAttachment.socketOffset.translate.x = GetEnvironmentFloat(
        "GE3_HAND_PARTICLE_OFFSET_X",
        runtimeState.handParticleAttachment.socketOffset.translate.x);
    runtimeState.handParticleAttachment.socketOffset.translate.y = GetEnvironmentFloat(
        "GE3_HAND_PARTICLE_OFFSET_Y",
        runtimeState.handParticleAttachment.socketOffset.translate.y);
    runtimeState.handParticleAttachment.socketOffset.translate.z = GetEnvironmentFloat(
        "GE3_HAND_PARTICLE_OFFSET_Z",
        runtimeState.handParticleAttachment.socketOffset.translate.z);
    if (HasEnvironmentVariable("GE3_WEAPON_ATTACHMENT")) {
        runtimeState.weaponAttachment.enabled = true;
        runtimeState.showSkinnedModel = true;
        if (!HasEnvironmentVariable("GE3_SKINNED_MODEL_INDEX")) {
            runtimeState.selectedSkinnedModelIndex = 1;
        }
    }
    runtimeState.weaponAttachment.jointName = GetEnvironmentString(
        "GE3_WEAPON_ATTACHMENT_JOINT",
        runtimeState.weaponAttachment.jointName);
    runtimeState.weaponAttachment.socketOffset.translate.x = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_OFFSET_X",
        runtimeState.weaponAttachment.socketOffset.translate.x);
    runtimeState.weaponAttachment.socketOffset.translate.y = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_OFFSET_Y",
        runtimeState.weaponAttachment.socketOffset.translate.y);
    runtimeState.weaponAttachment.socketOffset.translate.z = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_OFFSET_Z",
        runtimeState.weaponAttachment.socketOffset.translate.z);
    runtimeState.weaponAttachment.socketOffset.rotate.x = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_ROTATION_X",
        runtimeState.weaponAttachment.socketOffset.rotate.x);
    runtimeState.weaponAttachment.socketOffset.rotate.y = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_ROTATION_Y",
        runtimeState.weaponAttachment.socketOffset.rotate.y);
    runtimeState.weaponAttachment.socketOffset.rotate.z = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_ROTATION_Z",
        runtimeState.weaponAttachment.socketOffset.rotate.z);
    runtimeState.weaponAttachment.socketOffset.rotate.w = GetEnvironmentFloat(
        "GE3_WEAPON_ATTACHMENT_ROTATION_W",
        runtimeState.weaponAttachment.socketOffset.rotate.w);
    if (HasEnvironmentVariable("GE3_PAUSE_ANIMATION")) {
        runtimeState.playAnimatedCube = false;
    }
    runtimeState.animatedCubeTime =
        GetEnvironmentFloat("GE3_ANIMATION_TIME", runtimeState.animatedCubeTime);
    runtimeState.skinnedModelTransform.scale.x =
        GetEnvironmentFloat("GE3_SKINNED_SCALE_X", runtimeState.skinnedModelTransform.scale.x);
    runtimeState.skinnedModelTransform.scale.y =
        GetEnvironmentFloat("GE3_SKINNED_SCALE_Y", runtimeState.skinnedModelTransform.scale.y);
    runtimeState.skinnedModelTransform.scale.z =
        GetEnvironmentFloat("GE3_SKINNED_SCALE_Z", runtimeState.skinnedModelTransform.scale.z);
    runtimeState.skinnedModelTransform.translate.x =
        GetEnvironmentFloat("GE3_SKINNED_TRANSLATE_X", runtimeState.skinnedModelTransform.translate.x);
    runtimeState.skinnedModelTransform.translate.y =
        GetEnvironmentFloat("GE3_SKINNED_TRANSLATE_Y", runtimeState.skinnedModelTransform.translate.y);
    runtimeState.skinnedModelTransform.translate.z =
        GetEnvironmentFloat("GE3_SKINNED_TRANSLATE_Z", runtimeState.skinnedModelTransform.translate.z);
}

void ApplyEnvironmentRuntimeConfig(AppVfxRuntimeState& runtimeState) {
    if (HasEnvironmentVariable("GE3_SKINNED_SURFACE_VFX")) {
        runtimeState.enableSkinnedSurfaceVfx = true;
    }
    if (HasEnvironmentVariable("GE3_DISTORTION_DEDICATED_TELEMETRY")) {
        runtimeState.enableDistortionDedicatedResources = true;
        runtimeState.enableDistortionDedicatedTelemetry = true;
        if (HasEnvironmentVariable("GE3_DISTORTION_DEDICATED_NO_AUTO_FALLBACK")) {
            runtimeState.enableDistortionDedicatedAutoFallback = false;
        }
    }
    if (HasEnvironmentVariable("GE3_BEAM_DEDICATED_TELEMETRY")) {
        runtimeState.enableBeamDedicatedTelemetry = true;
    }
    if (HasEnvironmentVariable("GE3_BEAM_DEDICATED_NO_AUTO_FALLBACK")) {
        runtimeState.enableBeamDedicatedAutoFallback = false;
    }
}

std::string ParseRailCourseArguments(int count,const wchar_t* const* args) {
    for(int i=1;args && i<count;++i) if(args[i] && std::wstring_view(args[i])==L"--full-course")
        return "Resources/courses/CanyonAssaultRoute01.course";
    return RailShortCoursePath;
}
std::string ResolveRailCoursePathFromCommandLine() {
    int count=0; auto* args=CommandLineToArgvW(GetCommandLineW(),&count);
    auto path=ParseRailCourseArguments(count,args);
    if(args) LocalFree(args);
    return path;
}
