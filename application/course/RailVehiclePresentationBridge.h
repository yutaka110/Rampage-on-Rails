#pragma once

#include <cstdint>

#include "RailVehicleMovementSystem.h"
#include "RailVehicleRideDynamicsSystem.h"
#include "RailVehicleTrackContactPoseSolver.h"

struct RailVehiclePresentationSettings final {
    bool enabled = true;
    float wheelRadius = 0.62f;
    float maximumVisualBankDegrees = 18.0f;
    float maximumVisualPitchDegrees = 8.0f;
    float suspensionAmplitude = 0.10f;
    float railJointSpacing = 3.2f;
    float smoothingResponse = 12.0f;
    float jointImpactSpeedThreshold = 12.0f;
    float brakeSparkDecelerationThreshold = 24.0f;
};

// Presentation-only output. Rendering, audio and VFX consume this frame, but
// it must never feed collision, weapon rays or authoritative transforms.
struct RailVehiclePresentationFrame final {
    bool visible = false;
    Vector3 visualPosition{};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 right{1.0f, 0.0f, 0.0f};
    float wheelRotationRadians = 0.0f;
    float visualBankDegrees = 0.0f;
    float visualPitchDegrees = 0.0f;
    float visualYawDegrees = 0.0f;
    float suspensionOffset = 0.0f;
    float speedNormalized = 0.0f;
    float rollingAudioVolume = 0.0f;
    float rollingAudioPitch = 1.0f;
    float brakeAudioVolume = 0.0f;
    bool jointImpactThisFrame = false;
    bool brakeSparksActive = false;
    uint64_t crossedJointIndex = 0;
    uint64_t sourceVehicleRevision = 0;
    uint64_t revision = 0;
};

struct RailVehiclePresentationInput final {
    const RailVehicleDefinition* definition = nullptr;
    const RailVehicleRuntimeState* state = nullptr;
    float deltaTime = 0.0f;
    RailVehiclePresentationSettings settings{};
    bool rideProfileActive = false;
    float rideProfileBlend = 0.0f;
    float rideAnticipatedSignedCurvature = 0.0f;
    float rideVisualBankScale = 1.0f;
    float rideMaximumVisualBankDegrees = 18.0f;
    const RailVehicleRideDynamicsFrame* rideDynamics = nullptr;
    const RailVehicleTrackContactPoseFrame* trackContactPose = nullptr;
    float brakePitchDegrees = 0.0f;
    float brakeCompression = 0.0f;
};

class RailVehiclePresentationBridge final {
public:
    void Reset();
    void Update(const RailVehiclePresentationInput& input);

    const RailVehiclePresentationFrame& Frame() const noexcept { return frame_; }

private:
    RailVehiclePresentationFrame frame_{};
    float smoothedBankDegrees_ = 0.0f;
    float smoothedPitchDegrees_ = 0.0f;
    uint64_t previousJointIndex_ = 0;
    bool jointInitialized_ = false;
    uint64_t revision_ = 0;
};
