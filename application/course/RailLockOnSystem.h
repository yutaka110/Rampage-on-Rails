#pragma once

#include "RailAimAssistSystem.h"
#include "RailLockResolver.h"
#include "RailReticleController.h"
#include "RailTargetRegistry.h"
#include "RailWorldRaycast.h"

namespace ge3::debug {
class DebugDrawSystem;
}

struct RailLockOnFrameInput {
    HWND hwnd = nullptr;
    float deltaTime = 0.016f;
    float playerDistance = 0.0f;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    bool hasCursorPosition = false;
    Vector2 cursorPosition{};
    const Matrix4x4* gameplayViewProjection = nullptr;
    const RailPath* railPath = nullptr;
    const CourseSpawnRuntime* spawnRuntime = nullptr;
    const CourseAsset* course = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    const TerrainEditLayer* terrainPreview = nullptr;
    Vector3 gameplayCameraPosition{};
    float aimRayMaxDistance = 120.0f;
    bool gamepadConnected = false;
    Vector2 gamepadAim{};
    bool keyboardDirectionalAimEnabled = true;
    bool shiftLockEnabled = true;
    bool aimAssistEnabled = true;
};

class RailLockOnSystem {
public:
    void Reset();
    void Update(const RailLockOnFrameInput& input);
    void AppendDebugDraw(ge3::debug::DebugDrawSystem& debugDraw) const;

    const RailReticleState& Reticle() const { return reticle_.State(); }
    const RailAimState& Aim() const { return reticle_.State().aim; }
    const RailAimState& RawAim() const { return aimAssist_.RawAim(); }
    const RailAimAssistFrame& AimAssist() const { return aimAssist_.Frame(); }
    const std::vector<RailLockToken>& Tokens() const { return resolver_.Tokens(); }
    const RailLockRelease& LastRelease() const { return lastRelease_; }
    const RailLockDebugFrame& DebugFrame() const { return debugFrame_; }
    const RailLockSettings& Settings() const { return settings_; }
    bool Configure(const RailLockSettings& settings, std::string* errorMessage = nullptr);
    const RailAimAssistSettings& AimAssistSettings() const { return aimAssistSettings_; }
    bool ConfigureAimAssist(const RailAimAssistSettings& settings, std::string* errorMessage = nullptr);
    const AimInputDeviceRouterState& AimInputDevice() const {
        return reticle_.InputDeviceState();
    }

private:
    RailLockSettings settings_{};
    RailAimAssistSettings aimAssistSettings_{};
    RailReticleController reticle_;
    RailAimAssistSystem aimAssist_;
    RailTargetRegistry registry_;
    RailLockResolver resolver_;
    RailLockDebugFrame debugFrame_{};
    RailLockRelease lastRelease_{};
    float elapsedTime_ = 0.0f;
};
