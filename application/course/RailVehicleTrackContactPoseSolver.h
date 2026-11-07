#pragma once

#include <cstdint>
#include <string>

#include "RailVehicleMovementSystem.h"

// Presentation-only rigid-body pose fitted to four wheel/rail contacts. The
// movement system remains the sole authority for distance, speed, collision
// and gameplay mount positions.
struct RailVehicleTrackContactPoseSettings final {
    bool enabled = true;
    float wheelbase = 3.9f;
    float trackGauge = 3.8f;
    // Height from the authored mesh pivot to the lowest running-gear point.
    float bodyPivotHeightAboveContacts = 0.95f;
    // Deliberate visual air gap. This keeps the cart readable without making
    // the wheels look detached from the rail head.
    float contactClearance = 0.05f;
    float railHeadVerticalOffset = 0.0f;
    float maximumSuspensionTravel = 0.35f;
    float minimumContactSeparation = 0.05f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailVehicleTrackContactPoseInput final {
    const RailVehicleDefinition* definition = nullptr;
    const RailVehicleRuntimeState* state = nullptr;
    const RailPath* railPath = nullptr;
    RailVehicleTrackContactPoseSettings settings{};
    const RailVehicleRideArc* rideArc = nullptr;
};

struct RailVehicleTrackContactPoseFrame final {
    bool valid = false;
    bool clampedAtRailStart = false;
    bool clampedAtRailEnd = false;
    float rearDistance = 0.0f;
    float frontDistance = 0.0f;
    float grade = 0.0f;
    Vector3 rearLeftContact{};
    Vector3 rearRightContact{};
    Vector3 frontLeftContact{};
    Vector3 frontRightContact{};
    // Compatibility axle centers retained for existing diagnostics.
    Vector3 rearContact{};
    Vector3 frontContact{};
    Vector3 contactCentroid{};
    Vector3 visualPosition{};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 right{1.0f, 0.0f, 0.0f};
    float rearLeftSuspensionOffset = 0.0f;
    float rearRightSuspensionOffset = 0.0f;
    float frontLeftSuspensionOffset = 0.0f;
    float frontRightSuspensionOffset = 0.0f;
    float maximumContactPlaneError = 0.0f;
    bool allWheelsSupported = false;
    uint8_t contactCount = 0;
    uint64_t sourceVehicleRevision = 0;
    uint64_t revision = 0;
};

class RailVehicleTrackContactPoseSolver final {
public:
    void Reset();
    const RailVehicleTrackContactPoseFrame& Solve(
        const RailVehicleTrackContactPoseInput& input);
    const RailVehicleTrackContactPoseFrame& Frame() const noexcept { return frame_; }

private:
    RailVehicleTrackContactPoseFrame frame_{};
    uint64_t revision_ = 0;
};
