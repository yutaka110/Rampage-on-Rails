#pragma once

#include <cstdint>
#include <string>

#include "RailVehicleTrackContactPoseSolver.h"
#include "RailVehicleRideDynamicsSystem.h"

struct RailVehicleCameraInertiaSettings final {
    bool enabled = true;
    float positionResponseHz = 2.4f;
    float targetResponseHz = 3.2f;
    float fovResponseHz = 2.0f;
    float accelerationLagDistance = 1.15f;
    float brakingLeadDistance = 0.70f;
    float lateralForceScale = 0.0018f;
    float maximumLateralLag = 1.10f;
    float turnLeadScale = 0.0030f;
    float maximumTurnLead = 2.20f;
    float speedFovDegrees = 5.0f;
    float accelerationFovDegrees = 1.25f;
    float releaseBeatLagDistance = 0.25f;
    float releaseBeatFovDegrees = 1.50f;
    float rollInheritance = 0.24f;
    float suspensionFollow = 0.42f;
    float maximumPresentationOffset = 0.18f;
    float aimFocusSuppression = 0.72f;
    float distanceDiscontinuityThreshold = 12.0f;
    float maximumSubstepSeconds = 1.0f / 120.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailVehicleCameraInertiaInput final {
    const RailVehicleDefinition* definition = nullptr;
    const RailVehicleRuntimeState* state = nullptr;
    const RailVehicleTrackContactPoseFrame* trackContact = nullptr;
    const RailVehicleRideDynamicsFrame* rideDynamics = nullptr;
    const RailRideDirectorFrame* ride = nullptr;
    float deltaTime = 0.0f;
    bool gameplayActive = true;
    bool aimFocusActive = false;
    float manualBoostStrength = 0.0f;
    float manualBoostIgnition = 0.0f;
    RailVehicleCameraInertiaSettings settings{};
};

struct RailVehicleCameraInertiaFrame final {
    bool active = false;
    bool historyResetThisFrame = false;
    Vector3 gameplayPositionOffset{};
    Vector3 gameplayTargetOffset{};
    Vector3 presentationPositionOffset{};
    Vector3 presentationTargetOffset{};
    float rollOffsetRadians = 0.0f;
    float fovOffsetRadians = 0.0f;
    uint64_t sourceVehicleRevision = 0;
    uint64_t sourceTrackContactRevision = 0;
    uint64_t sourceRideRevision = 0;
    uint64_t revision = 0;
};

class RailVehicleCameraInertiaBridge final {
public:
    void Reset();
    const RailVehicleCameraInertiaFrame& Update(
        const RailVehicleCameraInertiaInput& input);
    const RailVehicleCameraInertiaFrame& Frame() const noexcept { return frame_; }

private:
    struct Spring { float value=0.0f; float velocity=0.0f; };
    static void StepSpring(Spring& spring, float target, float frequencyHz, float dt);
    RailVehicleCameraInertiaFrame frame_{};
    Spring longitudinal_{};
    Spring lateral_{};
    Spring targetLead_{};
    Spring roll_{};
    Spring fov_{};
    float previousDistance_ = 0.0f;
    std::string previousVehicleId_;
    bool initialized_ = false;
    uint64_t revision_ = 0;
};
