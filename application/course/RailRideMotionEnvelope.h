#pragma once

#include <cstdint>

#include "RailRideDirector.h"
#include "RailVehicleMovementSystem.h"

struct RailRideMotionEnvelopeInput final {
    const RailVehicleDefinition* vehicleDefinition = nullptr;
    const RailVehicleRuntimeState* vehicleState = nullptr;
    const RailRideDirectorFrame* ride = nullptr;
    const RailPath* railPath = nullptr;
    float normalSpeedScale = 1.0f;
    float manualBoostMultiplier = 1.0f;
};

struct RailRideMotionEnvelopeFrame final {
    float normalRequestedSpeed = 0.0f;
    float manualBoostMultiplier = 1.0f;
    bool active = false;
    bool cornerLimited = false;
    float sourceRequestedSpeed = 0.0f;
    float requestedSpeed = 0.0f;
    float cornerSafeSpeed = 0.0f;
    float anticipatedCurvature = 0.0f;
    float accelerationLimit = 0.0f;
    float brakingLimit = 0.0f;
    float jerkLimit = 0.0f;
    uint64_t sourceRideRevision = 0;
    uint64_t revision = 0;
};

// Converts authored ride intent into constraints. It never integrates speed
// or distance; RailVehicleMovementSystem remains the sole authority.
class RailRideMotionEnvelope final {
public:
    void Reset();
    const RailRideMotionEnvelopeFrame& Evaluate(
        const RailRideMotionEnvelopeInput& input);
    const RailRideMotionEnvelopeFrame& Frame() const noexcept { return frame_; }

private:
    RailRideMotionEnvelopeFrame frame_{};
    uint64_t revision_ = 0;
};
