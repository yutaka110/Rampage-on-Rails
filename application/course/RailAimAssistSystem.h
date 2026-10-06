#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RailTargetRegistry.h"
#include "RailWorldRaycast.h"

enum class RailAimAssistInputDevice : uint8_t {
    MouseKeyboard,
    Gamepad,
};

enum class RailAimAssistRejectReason : uint8_t {
    None,
    Disabled,
    LockModeActive,
    InvalidAim,
    InvalidTarget,
    OutOfRange,
    OutsideAssistCone,
    RegistryOccluded,
    WorldOccluded,
    VisibilityBudget,
};

struct RailAimAssistSettings {
    bool enabled = true;
    bool mouseKeyboardEnabled = true;
    bool gamepadEnabled = true;
    bool requireWorldVisibility = true;
    float minimumDistance = 4.0f;
    float maximumDistance = 120.0f;
    float mouseAcquireAngleDegrees = 4.25f;
    float gamepadAcquireAngleDegrees = 8.5f;
    float retentionAngleMultiplier = 1.45f;
    float mouseMagnetismStrength = 0.16f;
    float gamepadMagnetismStrength = 0.58f;
    float mouseMaximumCorrectionDegrees = 2.0f;
    float gamepadMaximumCorrectionDegrees = 7.0f;
    float maximumCorrectionSpeedDegrees = 210.0f;
    float highIntentReticleSpeed = 1450.0f;
    float minimumHighIntentStrength = 0.18f;
    float targetSwitchAdvantage = 0.14f;
    float targetRetentionSeconds = 0.18f;
    float angleWeight = 0.68f;
    float forwardWeight = 0.14f;
    float anchorPriorityWeight = 0.18f;
    float enemyPriorityBonus = 0.08f;
    float retainedTargetBonus = 0.10f;
    uint32_t maximumVisibilityQueries = 8;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailAimAssistCandidate {
    RailLockTargetHandle target{};
    Vector3 worldPosition{};
    float distance = 0.0f;
    float angularErrorDegrees = 0.0f;
    float angleScore = 0.0f;
    float forwardScore = 0.0f;
    float priorityScore = 0.0f;
    float score = 0.0f;
    RailAimAssistRejectReason rejectReason = RailAimAssistRejectReason::None;
    bool retainedTarget = false;
    bool visibilityTested = false;
    bool eligible = false;
    bool selected = false;
};

struct RailAimAssistFrameInput {
    const RailAimState* rawAim = nullptr;
    const std::vector<RailLockAnchor>* anchors = nullptr;
    const RailWorldRaycastInput* visibilityQuery = nullptr;
    RailAimAssistInputDevice inputDevice = RailAimAssistInputDevice::MouseKeyboard;
    RailAimAssistSettings settings{};
    float deltaTime = 0.016f;
    float reticleSpeedPixelsPerSecond = 0.0f;
    bool enabled = true;
    bool lockModeActive = false;
};

struct RailAimAssistFrame {
    RailAimState rawAim{};
    RailAimState assistedAim{};
    std::vector<RailAimAssistCandidate> candidates;
    RailLockTargetHandle target{};
    Vector3 targetWorldPosition{};
    float targetScore = 0.0f;
    float correctionDegrees = 0.0f;
    float appliedStrength = 0.0f;
    float inputFrictionScale = 1.0f;
    uint32_t visibilityQueries = 0;
    RailAimAssistInputDevice inputDevice = RailAimAssistInputDevice::MouseKeyboard;
    bool active = false;
    bool frictionActive = false;
    bool retainedTarget = false;
};

class RailAimAssistSystem {
public:
    void Reset();
    void Update(const RailAimAssistFrameInput& input);

    const RailAimAssistFrame& Frame() const { return frame_; }
    const RailAimState& RawAim() const { return frame_.rawAim; }
    const RailAimState& AssistedAim() const { return frame_.assistedAim; }

private:
    RailLockTargetHandle retainedTarget_{};
    float retainedTargetMissingSeconds_ = 0.0f;
    bool hasRetainedTarget_ = false;
    RailAimAssistFrame frame_{};
};

const char* ToRailAimAssistInputDeviceString(RailAimAssistInputDevice device);
const char* ToRailAimAssistRejectReasonString(RailAimAssistRejectReason reason);
