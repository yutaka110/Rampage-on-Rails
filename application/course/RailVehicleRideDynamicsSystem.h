#pragma once

#include <cstdint>
#include <string>

#include "RailRideDirector.h"
#include "RailTrackFeedbackDirector.h"
#include "RailVehicleMovementSystem.h"

// Presentation-only response tuning. None of these values may feed the
// authoritative vehicle transform, collision, weapon ray or mount positions.
struct RailVehicleRideDynamicsSettings final {
    bool enabled = true;
    float rollFrequencyHz = 2.6f;
    float rollDampingRatio = 0.72f;
    float pitchFrequencyHz = 3.2f;
    float pitchDampingRatio = 0.86f;
    float yawFrequencyHz = 2.8f;
    float yawDampingRatio = 0.90f;
    float suspensionFrequencyHz = 7.5f;
    float suspensionDampingRatio = 0.76f;
    float maximumBankDegrees = 18.0f;
    float maximumPitchDegrees = 8.0f;
    float maximumYawLagDegrees = 4.5f;
    float yawLagSeconds = 0.12f;
    float gradePitchScale = 1.0f;
    float accelerationPitchScale = 0.45f;
    float jerkPitchDegreesPerUnit = 0.012f;
    float maximumJerkPitchDegrees = 2.5f;
    float suspensionAmplitude = 0.10f;
    float railJointSpacing = 3.2f;
    float distanceDiscontinuityThreshold = 12.0f;
    float maximumSubstepSeconds = 1.0f / 120.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailVehicleRideDynamicsInput final {
    const RailVehicleDefinition* definition = nullptr;
    const RailVehicleRuntimeState* state = nullptr;
    const RailRideDirectorFrame* ride = nullptr;
    const RailTrackFeedbackFrame* trackFeedback = nullptr;
    float deltaTime = 0.0f;
    float manualBoostStrength = 0.0f;
    float manualBoostIgnition = 0.0f;
    RailVehicleRideDynamicsSettings settings{};
};

struct RailVehicleRideDynamicsFrame final {
    bool valid = false;
    bool historyResetThisFrame = false;
    bool comfortClamped = false;
    float targetBankDegrees = 0.0f;
    float targetPitchDegrees = 0.0f;
    float targetYawDegrees = 0.0f;
    float visualBankDegrees = 0.0f;
    float visualPitchDegrees = 0.0f;
    float visualYawDegrees = 0.0f;
    float suspensionOffset = 0.0f;
    float accelerationJerk = 0.0f;
    uint64_t sourceVehicleRevision = 0;
    uint64_t sourceRideRevision = 0;
    uint64_t revision = 0;
};

// Adds cinematic inertia to the vehicle presentation while leaving
// RailVehicleMovementSystem as the only distance/speed/pose authority.
class RailVehicleRideDynamicsSystem final {
public:
    void Reset();
    const RailVehicleRideDynamicsFrame& Update(
        const RailVehicleRideDynamicsInput& input);

    const RailVehicleRideDynamicsFrame& Frame() const noexcept { return frame_; }

private:
    struct SpringState final {
        float value = 0.0f;
        float velocity = 0.0f;
    };

    static void StepSpring(
        SpringState& spring,
        float target,
        float frequencyHz,
        float dampingRatio,
        float deltaTime);

    RailVehicleRideDynamicsFrame frame_{};
    SpringState roll_{};
    SpringState pitch_{};
    SpringState yaw_{};
    SpringState suspension_{};
    float previousAcceleration_ = 0.0f;
    float previousDistance_ = 0.0f;
    std::string previousVehicleId_;
    bool initialized_ = false;
    uint64_t revision_ = 0;
};
