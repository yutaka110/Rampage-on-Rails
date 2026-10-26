#include "RailVehicleMovementSystem.h"
#include "RailManualBoost.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

void SetError(std::string* errorMessage, const std::string& message) {
    if (errorMessage != nullptr) *errorMessage = message;
}

bool Finite(float value) noexcept { return std::isfinite(value); }

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(Vector3 a, Vector3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Approach(float current, float target, float maximumDelta) noexcept {
    if (current < target) return (std::min)(current + maximumDelta, target);
    return (std::max)(current - maximumDelta, target);
}

bool FiniteVector(Vector3 value) noexcept {
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

} // namespace

RailVehicleDefinition RailVehicleDefinition::MineCartDefaults() {
    return {};
}

bool RailVehicleDefinition::Validate(std::string* errorMessage) const {
    if (vehicleId.empty()) {
        SetError(errorMessage, "RailVehicleDefinition.vehicleId must not be empty.");
        return false;
    }
    if (!FiniteVector(collisionHalfExtents) || !Finite(bodyVerticalOffset) ||
        !Finite(maximumSpeed) || !Finite(acceleration) ||
        !Finite(serviceBrakeDeceleration) || !Finite(emergencyBrakeDeceleration) ||
        !Finite(maximumLateralAcceleration) || !Finite(curvatureLookAheadDistance) ||
        !Finite(endStopTolerance) || !Finite(maximumHitPoints) ||
        !FiniteVector(mounts.player) || !FiniteVector(mounts.weapon) ||
        !FiniteVector(mounts.camera) || !FiniteVector(mounts.damageVfx)) {
        SetError(errorMessage, "RailVehicleDefinition contains a non-finite value.");
        return false;
    }
    if (collisionHalfExtents.x <= 0.0f || collisionHalfExtents.y <= 0.0f ||
        collisionHalfExtents.z <= 0.0f || maximumSpeed <= 0.0f ||
        acceleration <= 0.0f || serviceBrakeDeceleration <= 0.0f ||
        emergencyBrakeDeceleration < serviceBrakeDeceleration ||
        maximumLateralAcceleration <= 0.0f || curvatureLookAheadDistance <= 0.0f ||
        endStopTolerance < 0.0f || maximumHitPoints <= 0.0f) {
        SetError(errorMessage, "Rail vehicle tuning contains an invalid range.");
        return false;
    }
    switch (mountedMovementMode) {
    case RailVehicleMountedMovementMode::FreeOffset:
    case RailVehicleMountedMovementMode::VehicleMounted:
        break;
    default:
        SetError(errorMessage, "Rail vehicle mounted movement mode is invalid.");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

const char* ToString(RailVehicleMountedMovementMode mode) noexcept {
    switch (mode) {
    case RailVehicleMountedMovementMode::FreeOffset: return "FreeOffset";
    case RailVehicleMountedMovementMode::VehicleMounted: return "VehicleMounted";
    }
    return "Unknown";
}

RailVehicleMovementSystem::RailVehicleMovementSystem() {
    (void)Initialize(RailVehicleDefinition::MineCartDefaults(), nullptr);
}

bool RailVehicleMovementSystem::Initialize(
    const RailVehicleDefinition& definition,
    std::string* errorMessage) {
    if (!definition.Validate(errorMessage)) return false;
    definition_ = definition;
    Reset();
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void RailVehicleMovementSystem::Reset(
    float distance,
    float speed,
    const RailPath* railPath) {
    state_ = {};
    state_.initialized = true;
    state_.vehicleId = definition_.vehicleId;
    state_.distance = Finite(distance) ? (std::max)(0.0f, distance) : 0.0f;
    state_.previousDistance = state_.distance;
    state_.speed = Finite(speed)
        ? (std::clamp)(speed, 0.0f, definition_.maximumSpeed)
        : 0.0f;
    state_.safeSpeed = definition_.maximumSpeed;
    state_.impactSpeedMultiplier = 1.0f;
    state_.impactSlowdownRemainingSeconds = 0.0f;
    state_.hitPoints = definition_.maximumHitPoints;
    state_.stopped = state_.speed <= 0.001f;
    if (railPath != nullptr && railPath->Length() > 0.0f) {
        state_.distance = (std::clamp)(state_.distance, 0.0f, railPath->Length());
        state_.previousDistance = state_.distance;
        EvaluatePose(*railPath);
    }
    ++state_.revision;
    frame_ = {};
    frame_.state = state_;
}

bool RailVehicleMovementSystem::RestoreState(
    const RailVehicleRuntimeState& restored,
    const RailPath* railPath,
    std::string* errorMessage) {
    if (!restored.initialized || restored.vehicleId != definition_.vehicleId ||
        !Finite(restored.distance) || !Finite(restored.speed) ||
        !Finite(restored.hitPoints) ||
        !Finite(restored.impactSpeedMultiplier) ||
        !Finite(restored.impactSlowdownRemainingSeconds) ||
        restored.distance < 0.0f ||
        restored.speed < 0.0f || restored.speed > definition_.maximumSpeed * RailManualBoost::MaximumMultiplier + 0.001f ||
        restored.impactSpeedMultiplier <= 0.0f ||
        restored.impactSpeedMultiplier > 1.0f ||
        restored.impactSlowdownRemainingSeconds < 0.0f ||
        restored.impactSlowdownRemainingSeconds > 5.0f ||
        restored.hitPoints < 0.0f ||
        restored.hitPoints > definition_.maximumHitPoints + 0.001f) {
        SetError(errorMessage, "Rail vehicle checkpoint does not match the active definition.");
        return false;
    }
    if (railPath != nullptr && restored.distance > railPath->Length() + 0.001f) {
        SetError(errorMessage, "Rail vehicle checkpoint distance exceeds the active path.");
        return false;
    }
    state_ = restored;
    if (railPath != nullptr && railPath->Length() > 0.0f) EvaluatePose(*railPath);
    ++state_.revision;
    frame_ = {};
    frame_.state = state_;
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void RailVehicleMovementSystem::SynchronizeHitPoints(float hitPoints) {
    if (!state_.initialized || !Finite(hitPoints)) return;
    const float clamped = (std::clamp)(
        hitPoints, 0.0f, definition_.maximumHitPoints);
    if (std::abs(state_.hitPoints - clamped) <= 0.0001f) return;
    state_.hitPoints = clamped;
    ++state_.revision;
    frame_.state = state_;
}

bool RailVehicleMovementSystem::ApplyImpactSlowdown(
    float speedMultiplier,
    float durationSeconds,
    uint64_t resultSequence) {
    if (!state_.initialized || resultSequence == 0 ||
        resultSequence <= state_.lastImpactSlowdownSequence ||
        !Finite(speedMultiplier) || !Finite(durationSeconds) ||
        speedMultiplier <= 0.0f || speedMultiplier > 1.0f ||
        durationSeconds <= 0.0f || durationSeconds > 5.0f) {
        return false;
    }
    state_.impactSpeedMultiplier = state_.impactSlowdownRemainingSeconds > 0.0f
        ? (std::min)(state_.impactSpeedMultiplier, speedMultiplier)
        : speedMultiplier;
    state_.impactSlowdownRemainingSeconds = (std::max)(
        state_.impactSlowdownRemainingSeconds, durationSeconds);
    state_.lastImpactSlowdownSequence = resultSequence;
    ++state_.revision;
    frame_.state = state_;
    frame_.impactSlowdownActive = true;
    return true;
}

const RailVehicleMovementFrame& RailVehicleMovementSystem::Update(
    const RailVehicleMovementInput& input) {
    frame_ = {};
    if (!state_.initialized || input.courseRuntime == nullptr ||
        input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        frame_.state = state_;
        return frame_;
    }

    const RailPath& railPath = *input.railPath;
    const float deltaTime = Finite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    if (std::abs(input.courseRuntime->Distance() - state_.distance) > 0.001f) {
        state_.distance = (std::clamp)(
            input.courseRuntime->Distance(), 0.0f, railPath.Length());
        state_.previousDistance = state_.distance;
    }

    const bool beganEmergencyBrake =
        input.emergencyBrake && !state_.emergencyBraking;
    state_.movementEnabled = input.movementEnabled;
    state_.emergencyBraking = input.emergencyBrake;
    const float speedCeiling = definition_.maximumSpeed * RailManualBoost::ClampMultiplier(input.manualBoostMultiplier);
    state_.requestedSpeed = Finite(input.requestedSpeed)
        ? (std::clamp)(input.requestedSpeed, 0.0f, speedCeiling)
        : 0.0f;
    state_.safeSpeed = ComputeSafeSpeed(railPath, speedCeiling);
    float targetSpeed = input.movementEnabled
        ? (std::min)(state_.requestedSpeed, state_.safeSpeed)
        : 0.0f;
    const float travelEnd = input.courseRuntime->TraversalEndDistance(railPath);
    if (travelEnd < railPath.Length()) {
        const float remaining = (std::max)(0.0f, travelEnd - state_.distance);
        const float approachSpeed = remaining <= definition_.endStopTolerance ? 0.0f
            : std::sqrt(2.0f * definition_.serviceBrakeDeceleration * remaining);
        targetSpeed = (std::min)(targetSpeed, approachSpeed);
    }
    const bool impactSlowdownActive =
        state_.impactSlowdownRemainingSeconds > 0.0f;
    if (impactSlowdownActive) {
        targetSpeed *= state_.impactSpeedMultiplier;
    }
    if (input.emergencyBrake) targetSpeed = 0.0f;
    const float previousSpeed = state_.speed;
    const float accelerationLimit = input.motionEnvelopeActive &&
        Finite(input.accelerationLimit) && input.accelerationLimit > 0.0f
        ? input.accelerationLimit : definition_.acceleration;
    const float brakingLimit = input.motionEnvelopeActive &&
        Finite(input.brakingLimit) && input.brakingLimit > 0.0f
        ? input.brakingLimit : definition_.serviceBrakeDeceleration;
    const float rate = targetSpeed > state_.speed
        ? accelerationLimit
        : (input.emergencyBrake
            ? definition_.emergencyBrakeDeceleration
            : brakingLimit);
    if (input.motionEnvelopeActive && !input.emergencyBrake &&
        Finite(input.jerkLimit) && input.jerkLimit > 0.0f &&
        deltaTime > 0.0f) {
        const float desiredAcceleration = targetSpeed > state_.speed
            ? rate : (targetSpeed < state_.speed ? -rate : 0.0f);
        const float constrainedAcceleration = Approach(
            state_.acceleration,
            desiredAcceleration,
            input.jerkLimit * deltaTime);
        const float candidate = (std::clamp)(
            state_.speed + constrainedAcceleration * deltaTime,
            0.0f,
            (std::max)(previousSpeed, speedCeiling));
        if (targetSpeed > state_.speed + 0.0001f) {
            state_.speed = (std::clamp)(candidate, state_.speed, targetSpeed);
        } else if (targetSpeed < state_.speed - 0.0001f) {
            state_.speed = (std::clamp)(candidate, targetSpeed, state_.speed);
        } else {
            state_.speed = targetSpeed;
        }
    } else {
        state_.speed = Approach(state_.speed, targetSpeed, rate * deltaTime);
    }
    state_.acceleration = deltaTime > 0.000001f
        ? (state_.speed - previousSpeed) / deltaTime
        : 0.0f;

    state_.previousDistance = state_.distance;
    if (deltaTime > 0.0f && input.movementEnabled && state_.speed > 0.0f) {
        frame_.triggeredEvents = input.courseRuntime->AdvanceClamped(
            deltaTime,
            railPath,
            state_.speed);
    }
    state_.distance = input.courseRuntime->Distance();
    frame_.traveledDistance = state_.distance - state_.previousDistance;
    const bool wasAtEnd = state_.atCourseEnd;
    state_.atCourseEnd =
        state_.distance >= travelEnd - definition_.endStopTolerance;
    if (state_.atCourseEnd) {
        // Keep CourseRuntime and vehicle authority at the same terminal distance.
        state_.speed = 0.0f;
        state_.acceleration = 0.0f;
    }
    state_.stopped = state_.speed <= 0.001f;
    state_.impactSlowdownRemainingSeconds = (std::max)(
        0.0f, state_.impactSlowdownRemainingSeconds - deltaTime);
    if (state_.impactSlowdownRemainingSeconds <= 0.0f) {
        state_.impactSpeedMultiplier = 1.0f;
    }
    EvaluatePose(railPath);
    ++state_.frameIndex;
    ++state_.revision;

    frame_.reachedCourseEndThisFrame = state_.atCourseEnd && !wasAtEnd;
    frame_.beganEmergencyBrakeThisFrame = beganEmergencyBrake;
    frame_.impactSlowdownActive = impactSlowdownActive;
    frame_.state = state_;
    return frame_;
}

float RailVehicleMovementSystem::ComputeSafeSpeed(const RailPath& railPath, float speedCeiling) {
    const float lookAhead = (std::max)(0.1f, definition_.curvatureLookAheadDistance);
    const RailPathSample before = railPath.Evaluate(
        (std::max)(0.0f, state_.distance - lookAhead * 0.5f));
    const RailPathSample after = railPath.Evaluate(
        (std::min)(railPath.Length(), state_.distance + lookAhead * 0.5f));
    const float dot = (std::clamp)(Dot(before.tangent, after.tangent), -1.0f, 1.0f);
    const float angle = std::acos(dot);
    state_.curvature = angle / lookAhead;
    const Vector3 turn = Cross(before.tangent, after.tangent);
    state_.signedCurvature = state_.curvature *
        (Dot(turn, before.up) < 0.0f ? -1.0f : 1.0f);
    if (state_.curvature <= 0.00001f) return speedCeiling;
    const float curveSpeed = std::sqrt(
        definition_.maximumLateralAcceleration / state_.curvature);
    return (std::clamp)(curveSpeed, 0.0f, speedCeiling);
}

void RailVehicleMovementSystem::EvaluatePose(const RailPath& railPath) {
    const RailPathSample sample = railPath.Evaluate(state_.distance);
    state_.forward = sample.tangent;
    state_.up = sample.up;
    state_.right = sample.right;
    state_.grade = (std::clamp)(sample.tangent.y, -1.0f, 1.0f);
    state_.position = Add(sample.position, Scale(sample.up, definition_.bodyVerticalOffset));
    state_.normalizedProgress = railPath.Length() > 0.0f
        ? (std::clamp)(state_.distance / railPath.Length(), 0.0f, 1.0f)
        : 0.0f;
    state_.segmentIndex = 0;
    const uint32_t segmentCount = railPath.SegmentCount();
    for (uint32_t segment = 0; segment < segmentCount; ++segment) {
        const float end = railPath.SegmentStartDistance(segment) +
            railPath.SegmentLength(segment);
        state_.segmentIndex = segment;
        if (state_.distance <= end || segment + 1 == segmentCount) break;
    }
    UpdateMounts();
}

void RailVehicleMovementSystem::UpdateMounts() {
    const auto transformMount = [this](Vector3 local) {
        Vector3 world = state_.position;
        world = Add(world, Scale(state_.right, local.x));
        world = Add(world, Scale(state_.up, local.y));
        world = Add(world, Scale(state_.forward, local.z));
        return world;
    };
    state_.playerMountPosition = transformMount(definition_.mounts.player);
    state_.weaponMountPosition = transformMount(definition_.mounts.weapon);
    state_.cameraMountPosition = transformMount(definition_.mounts.camera);
    state_.damageVfxMountPosition = transformMount(definition_.mounts.damageVfx);
}
