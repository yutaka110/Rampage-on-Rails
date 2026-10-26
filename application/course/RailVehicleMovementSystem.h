#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CourseAsset.h"
#include "../terrain/RailPath.h"

// Defines who owns the player's rail-local position while occupying a vehicle.
// VehicleMounted is the mine-cart default: the vehicle mount is authoritative
// and free-flight movement is suspended without destroying its runtime state.
enum class RailVehicleMountedMovementMode : uint8_t {
    FreeOffset,
    VehicleMounted,
};

struct RailVehicleMountDefinition final {
    Vector3 player{0.0f, 0.0f, 0.0f};
    Vector3 weapon{0.0f, 1.4f, 1.8f};
    Vector3 camera{0.0f, 3.2f, -7.5f};
    Vector3 damageVfx{0.0f, 1.0f, 0.0f};
};

// Immutable vehicle tuning. The vehicle remains kinematic and advances through
// CourseRuntime; physics may report contacts but never becomes rail authority.
struct RailVehicleDefinition final {
    std::string vehicleId = "rail_vehicle.mine_cart";
    RailVehicleMountedMovementMode mountedMovementMode =
        RailVehicleMountedMovementMode::VehicleMounted;
    Vector3 collisionHalfExtents{2.4f, 1.4f, 4.0f};
    float bodyVerticalOffset = 1.4f;
    float maximumSpeed = 68.0f;
    float acceleration = 22.0f;
    float serviceBrakeDeceleration = 36.0f;
    float emergencyBrakeDeceleration = 72.0f;
    float maximumLateralAcceleration = 26.0f;
    float curvatureLookAheadDistance = 8.0f;
    float endStopTolerance = 0.02f;
    float maximumHitPoints = 250.0f;
    bool allowDerail = false;
    RailVehicleMountDefinition mounts{};

    static RailVehicleDefinition MineCartDefaults();
    bool Validate(std::string* errorMessage = nullptr) const;
};

const char* ToString(RailVehicleMountedMovementMode mode) noexcept;

// Serializable/checkpoint-safe authoritative vehicle state. Presentation-only
// suspension and wheel animation deliberately do not live here.
struct RailVehicleRuntimeState final {
    bool initialized = false;
    std::string vehicleId;
    float distance = 0.0f;
    float previousDistance = 0.0f;
    float speed = 0.0f;
    float requestedSpeed = 0.0f;
    float safeSpeed = 0.0f;
    float acceleration = 0.0f;
    float impactSpeedMultiplier = 1.0f;
    float impactSlowdownRemainingSeconds = 0.0f;
    float hitPoints = 0.0f;
    float normalizedProgress = 0.0f;
    float curvature = 0.0f;
    float signedCurvature = 0.0f;
    float grade = 0.0f;
    uint32_t segmentIndex = 0;
    Vector3 position{};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 right{1.0f, 0.0f, 0.0f};
    Vector3 playerMountPosition{};
    Vector3 weaponMountPosition{};
    Vector3 cameraMountPosition{};
    Vector3 damageVfxMountPosition{};
    bool movementEnabled = false;
    bool emergencyBraking = false;
    bool atCourseEnd = false;
    bool stopped = true;
    uint64_t frameIndex = 0;
    uint64_t lastImpactSlowdownSequence = 0;
    uint64_t revision = 0;
};

struct RailVehicleMovementInput final {
    // Opens bounded speed headroom only while the shared boost policy requests
    // it. Curvature, braking and collision slowdowns still own safety.
    float manualBoostMultiplier = 1.0f;
    float deltaTime = 0.0f;
    float requestedSpeed = 0.0f;
    bool movementEnabled = true;
    bool emergencyBrake = false;
    bool motionEnvelopeActive = false;
    float accelerationLimit = 0.0f;
    float brakingLimit = 0.0f;
    float jerkLimit = 0.0f;
    CourseRuntime* courseRuntime = nullptr;
    const RailPath* railPath = nullptr;
};

struct RailVehicleMovementFrame final {
    RailVehicleRuntimeState state{};
    std::vector<CourseEventMarker> triggeredEvents;
    float traveledDistance = 0.0f;
    bool reachedCourseEndThisFrame = false;
    bool beganEmergencyBrakeThisFrame = false;
    bool impactSlowdownActive = false;
};

class RailVehicleMovementSystem final {
public:
    RailVehicleMovementSystem();

    bool Initialize(
        const RailVehicleDefinition& definition,
        std::string* errorMessage = nullptr);
    void Reset(
        float distance = 0.0f,
        float speed = 0.0f,
        const RailPath* railPath = nullptr);
    bool RestoreState(
        const RailVehicleRuntimeState& state,
        const RailPath* railPath = nullptr,
        std::string* errorMessage = nullptr);
    void SynchronizeHitPoints(float hitPoints);
    bool ApplyImpactSlowdown(
        float speedMultiplier,
        float durationSeconds,
        uint64_t resultSequence);
    const RailVehicleMovementFrame& Update(const RailVehicleMovementInput& input);

    const RailVehicleDefinition& Definition() const noexcept { return definition_; }
    const RailVehicleRuntimeState& State() const noexcept { return state_; }
    const RailVehicleMovementFrame& Frame() const noexcept { return frame_; }
    bool IsInitialized() const noexcept { return state_.initialized; }

private:
    void EvaluatePose(const RailPath& railPath);
    float ComputeSafeSpeed(const RailPath& railPath, float speedCeiling);
    void UpdateMounts();

    RailVehicleDefinition definition_{};
    RailVehicleRuntimeState state_{};
    RailVehicleMovementFrame frame_{};
};
