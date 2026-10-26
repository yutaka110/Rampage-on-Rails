#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>

#include "AppVfxRuntimeState.h"
#include "SunfallAtmosphere.h"
#include "HandParticleAttachment.h"
#include "WeaponAttachment.h"
#include "terrain/TerrainGenerationSettings.h"
#include "utils/math/MathUtils.h"

struct RuntimeAabbState {
    Vector3 min{};
    Vector3 max{};
};

struct RuntimeEmitterState {
    Transform transform{};
    uint32_t count = 0;
    float frequency = 0.0f;
    float frequencyTime = 0.0f;
};

struct RuntimeAccelerationFieldState {
    Vector3 acceleration{};
    RuntimeAabbState area{};
};

struct RuntimeSkinningTimingPathStats {
    bool valid = false;
    uint32_t sampleCount = 0;
    uint64_t lastTicks = 0;
    uint64_t minTicks = 0;
    uint64_t maxTicks = 0;
    double averageTicks = 0.0;
};

struct RuntimeSkinningTimingStats {
    RuntimeSkinningTimingPathStats vertexShaderTotal{};
    RuntimeSkinningTimingPathStats computeTotal{};
    RuntimeSkinningTimingPathStats computeSurfaceOnly{};
};

enum class RuntimeSkinningPath : uint8_t {
    Unavailable,
    ComputeShader,
    VertexShader,
};

inline constexpr uint32_t kRuntimeVfxModelObjectCount = 3;

struct RuntimeVfxModelObjectState {
    Transform transform{};
    uint32_t modelIndex = 0;
    bool visible = true;
};

struct RuntimeCameraState {
    Transform transform{};
    float fovY = 0.25f * 3.14159265358979323846f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    float debugMoveSpeed = 0.65f;
    float debugFastMoveMultiplier = 6.0f;
    float debugSlowMoveMultiplier = 0.25f;
    float debugRotateSpeed = 0.02f;
    bool enableDebugInput = true;
};

struct RuntimeSubmissionShowcaseState {
    bool enabled = false;
    bool gamepadConnected = false;
    bool keyboardInputEnabled = false;
    bool keyboardActive = false;
    uint32_t controllerIndex = UINT32_MAX;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float moveMagnitude = 0.0f;
};

struct RuntimeSkinnedAnimationBlendState {
    bool active = false;
    uint32_t fromModelIndex = UINT32_MAX;
    uint32_t toModelIndex = UINT32_MAX;
    float fromTime = 0.0f;
    float toTime = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.25f;
    float alpha = 0.0f;
};

struct RuntimeWeaponDrawTelemetry {
    bool submitted = false;
    bool screenBoundsVisible = false;
    bool screenBoundsReadable = false;
    uint32_t submittedSubMeshCount = 0;
    uint32_t materialCount = 0;
    Vector2 screenMinimum{};
    Vector2 screenMaximum{};
};

struct AppRuntimeState {
    Transform transform{};
    Transform animatedCubeTransform{};
    Transform skinnedModelTransform{};
    Transform transformSprite{};
    Transform uvTransformSprite{};
    Material materialData{};

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
    float clearColor[4] = { 0.35f, 0.5f, 0.8f, 1.0f };

    RuntimeEmitterState emitter{};
    RuntimeAccelerationFieldState accelerationField{};
    HandParticleAttachmentSettings handParticleAttachment{};
    HandParticleAttachmentTelemetry handParticleAttachmentTelemetry{};
    HandParticleAttachmentSettings leftHandParticleAttachment{};
    HandParticleAttachmentTelemetry leftHandParticleAttachmentTelemetry{};
    WeaponAttachmentSettings weaponAttachment{};
    WeaponAttachmentTelemetry weaponAttachmentTelemetry{};
    RuntimeWeaponDrawTelemetry weaponDrawTelemetry{};

    DirectionalLight directionalLightData{};
    SunfallAtmosphereSettings sunfall{};
    PointLight pointLightData{};
    SpotLight spotLight{};
    RuntimeCameraState camera{};
    RuntimeSubmissionShowcaseState submissionShowcase{};
    RuntimeSkinnedAnimationBlendState skinnedAnimationBlend{};
    RuntimeSkinningPath activeSkinningPath = RuntimeSkinningPath::Unavailable;
    Vector3 cameraWorldPosition{};

    bool useMonsterBall = false;
    bool showAnimatedCube = true;
    bool showSkinnedModel = false;
    bool showSkeletonDebug = false;
    bool showSkybox = false;
    bool showProceduralBackdrop = true;
    bool showSprite = true;
    uint32_t selectedSkinnedModelIndex = 0;
    bool showVfxModelObjects = true;
    uint32_t selectedVfxModelObjectIndex = 0;
    std::array<RuntimeVfxModelObjectState, kRuntimeVfxModelObjectCount> vfxModelObjects{};
    bool playAnimatedCube = true;
    bool loopAnimatedCube = true;
    float animatedCubeTime = 0.0f;
    float animatedCubeSpeed = 1.0f;
    AppVfxRuntimeState vfx{};
    RuntimeSkinningTimingStats skinningTiming{};
    float debugDepthPreviewNear = 0.1f;
    float debugDepthPreviewFar = 25.0f;
    float debugDepthPreviewPower = 1.35f;
    float debugEmissivePreviewBoost = 2.0f;
    TerrainAuthoringState terrain{};
};
