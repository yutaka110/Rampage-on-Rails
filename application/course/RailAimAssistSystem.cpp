#include "RailAimAssistSystem.h"
#include "GameplaySettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool RailAimAssistSettings::Validate(std::string* errorMessage) const {
    if (!gameplay::settings::InRange(minimumDistance, 0.0f, 100000.0f) ||
        !gameplay::settings::InRange(maximumDistance, 0.01f, 100000.0f) ||
        minimumDistance >= maximumDistance ||
        !gameplay::settings::InRange(mouseAcquireAngleDegrees, 0.01f, 45.0f) ||
        !gameplay::settings::InRange(gamepadAcquireAngleDegrees, 0.01f, 45.0f) ||
        !gameplay::settings::InRange(retentionAngleMultiplier, 1.0f, 4.0f) ||
        !gameplay::settings::InRange(mouseMagnetismStrength, 0.0f, 1.0f) ||
        !gameplay::settings::InRange(gamepadMagnetismStrength, 0.0f, 1.0f) ||
        !gameplay::settings::InRange(mouseMaximumCorrectionDegrees, 0.0f, 45.0f) ||
        !gameplay::settings::InRange(gamepadMaximumCorrectionDegrees, 0.0f, 45.0f) ||
        !gameplay::settings::InRange(maximumCorrectionSpeedDegrees, 0.0f, 2000.0f) ||
        !gameplay::settings::InRange(highIntentReticleSpeed, 1.0f, 100000.0f) ||
        !gameplay::settings::InRange(minimumHighIntentStrength, 0.0f, 1.0f) ||
        !gameplay::settings::InRange(targetSwitchAdvantage, 0.0f, 2.0f) ||
        !gameplay::settings::InRange(targetRetentionSeconds, 0.0f, 5.0f) ||
        !gameplay::settings::InRange(angleWeight, 0.0f, 2.0f) ||
        !gameplay::settings::InRange(forwardWeight, 0.0f, 2.0f) ||
        !gameplay::settings::InRange(anchorPriorityWeight, 0.0f, 2.0f) ||
        !gameplay::settings::InRange(enemyPriorityBonus, 0.0f, 2.0f) ||
        !gameplay::settings::InRange(retainedTargetBonus, 0.0f, 2.0f) ||
        maximumVisibilityQueries > 256) {
        return gameplay::settings::Result(false, errorMessage, "Aim-assist settings exceed valid distance, angle, strength or visibility limits.");
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDirectionEpsilon = 0.000001f;

float Clamp01(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool Finite(const Vector3& value) {
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

Vector3 Add(const Vector3& left, const Vector3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector3 Subtract(const Vector3& left, const Vector3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(const Vector3& left, const Vector3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float LengthSquared(const Vector3& value) {
    return Dot(value, value);
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float lengthSquared = LengthSquared(value);
    if (!Finite(lengthSquared) || lengthSquared <= kDirectionEpsilon) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

float Degrees(float radians) {
    return radians * (180.0f / kPi);
}

float AngleDegrees(const Vector3& left, const Vector3& right) {
    return Degrees(std::acos((std::clamp)(Dot(left, right), -1.0f, 1.0f)));
}

bool SameTarget(const RailLockTargetHandle& left, const RailLockTargetHandle& right) {
    return left.kind == right.kind && left.actorId == right.actorId &&
        left.generationId == right.generationId;
}

RailAimHitKind ExpectedHitKind(RailLockTargetKind kind) {
    return kind == RailLockTargetKind::Enemy
        ? RailAimHitKind::Enemy
        : RailAimHitKind::Obstacle;
}

float AcquireAngle(
    RailAimAssistInputDevice device,
    const RailAimAssistSettings& settings) {
    return device == RailAimAssistInputDevice::Gamepad
        ? settings.gamepadAcquireAngleDegrees
        : settings.mouseAcquireAngleDegrees;
}

float MagnetismStrength(
    RailAimAssistInputDevice device,
    const RailAimAssistSettings& settings) {
    return device == RailAimAssistInputDevice::Gamepad
        ? settings.gamepadMagnetismStrength
        : settings.mouseMagnetismStrength;
}

float MaximumCorrection(
    RailAimAssistInputDevice device,
    const RailAimAssistSettings& settings) {
    return device == RailAimAssistInputDevice::Gamepad
        ? settings.gamepadMaximumCorrectionDegrees
        : settings.mouseMaximumCorrectionDegrees;
}

bool DeviceEnabled(
    RailAimAssistInputDevice device,
    const RailAimAssistSettings& settings) {
    return device == RailAimAssistInputDevice::Gamepad
        ? settings.gamepadEnabled
        : settings.mouseKeyboardEnabled;
}

void ResetResolvedHit(RailAimState& aim) {
    aim.worldAimPoint = Add(aim.worldRayOrigin, Scale(aim.worldRayDirection, aim.maxDistance));
    aim.worldAimNormal = {};
    aim.aimDistance = aim.maxDistance;
    aim.hitKind = RailAimHitKind::None;
    aim.hitActorId = 0;
    aim.hitSourceIndex = UINT32_MAX;
    aim.hasWorldHit = false;
}
} // namespace

void RailAimAssistSystem::Reset() {
    retainedTarget_ = {};
    retainedTargetMissingSeconds_ = 0.0f;
    hasRetainedTarget_ = false;
    frame_ = {};
}

void RailAimAssistSystem::Update(const RailAimAssistFrameInput& input) {
    frame_ = {};
    frame_.inputDevice = input.inputDevice;
    if (input.rawAim != nullptr) {
        frame_.rawAim = *input.rawAim;
        frame_.assistedAim = *input.rawAim;
    }

    const float dt = (std::clamp)(input.deltaTime, 0.0f, 0.1f);
    const RailAimAssistSettings& settings = input.settings;
    const bool validAim = input.rawAim != nullptr && input.rawAim->valid &&
        Finite(input.rawAim->worldRayOrigin) && Finite(input.rawAim->worldRayDirection) &&
        Finite(input.rawAim->maxDistance) && input.rawAim->maxDistance > 0.0f;
    const bool enabled = input.enabled && settings.enabled &&
        DeviceEnabled(input.inputDevice, settings);
    if (!validAim || !enabled || input.lockModeActive || input.anchors == nullptr) {
        if (input.lockModeActive || !enabled || !validAim) {
            hasRetainedTarget_ = false;
            retainedTargetMissingSeconds_ = 0.0f;
        }
        return;
    }

    const Vector3 rawDirection = NormalizeOr(input.rawAim->worldRayDirection, {});
    if (LengthSquared(rawDirection) <= kDirectionEpsilon) {
        hasRetainedTarget_ = false;
        return;
    }

    const float baseAcquireAngle = (std::max)(0.01f, AcquireAngle(input.inputDevice, settings));
    frame_.candidates.reserve(input.anchors->size());
    for (const RailLockAnchor& anchor : *input.anchors) {
        RailAimAssistCandidate candidate{};
        candidate.target = anchor.target;
        candidate.worldPosition = anchor.worldPosition;
        candidate.retainedTarget = hasRetainedTarget_ && SameTarget(anchor.target, retainedTarget_);

        const Vector3 toTarget = Subtract(anchor.worldPosition, input.rawAim->worldRayOrigin);
        const float distanceSquared = LengthSquared(toTarget);
        candidate.distance = distanceSquared > 0.0f ? std::sqrt(distanceSquared) : 0.0f;
        if (anchor.target.actorId == 0 || !Finite(anchor.worldPosition) ||
            !Finite(candidate.distance) || candidate.distance <= 0.001f) {
            candidate.rejectReason = RailAimAssistRejectReason::InvalidTarget;
            frame_.candidates.push_back(candidate);
            continue;
        }
        if (candidate.distance < settings.minimumDistance ||
            candidate.distance > (std::min)(settings.maximumDistance, input.rawAim->maxDistance)) {
            candidate.rejectReason = RailAimAssistRejectReason::OutOfRange;
            frame_.candidates.push_back(candidate);
            continue;
        }
        if (anchor.lineOfSightBlocked) {
            candidate.rejectReason = RailAimAssistRejectReason::RegistryOccluded;
            frame_.candidates.push_back(candidate);
            continue;
        }

        const Vector3 targetDirection = NormalizeOr(toTarget, rawDirection);
        candidate.angularErrorDegrees = AngleDegrees(rawDirection, targetDirection);
        const float candidateAcquireAngle = candidate.retainedTarget
            ? baseAcquireAngle * (std::max)(1.0f, settings.retentionAngleMultiplier)
            : baseAcquireAngle;
        if (!Finite(candidate.angularErrorDegrees) ||
            candidate.angularErrorDegrees > candidateAcquireAngle) {
            candidate.rejectReason = RailAimAssistRejectReason::OutsideAssistCone;
            frame_.candidates.push_back(candidate);
            continue;
        }

        candidate.angleScore = 1.0f - Clamp01(candidate.angularErrorDegrees / candidateAcquireAngle);
        candidate.forwardScore = 1.0f - Clamp01(
            (candidate.distance - settings.minimumDistance) /
            (std::max)(0.001f, settings.maximumDistance - settings.minimumDistance));
        candidate.priorityScore = Clamp01(anchor.priority);
        candidate.score =
            candidate.angleScore * settings.angleWeight +
            candidate.forwardScore * settings.forwardWeight +
            candidate.priorityScore * settings.anchorPriorityWeight +
            (anchor.target.kind == RailLockTargetKind::Enemy ? settings.enemyPriorityBonus : 0.0f) +
            (candidate.retainedTarget ? settings.retainedTargetBonus : 0.0f);
        candidate.eligible = true;
        frame_.candidates.push_back(candidate);
    }

    std::vector<size_t> eligibleIndices;
    eligibleIndices.reserve(frame_.candidates.size());
    for (size_t index = 0; index < frame_.candidates.size(); ++index) {
        if (frame_.candidates[index].eligible) {
            eligibleIndices.push_back(index);
        }
    }
    std::stable_sort(eligibleIndices.begin(), eligibleIndices.end(), [&](size_t left, size_t right) {
        return frame_.candidates[left].score > frame_.candidates[right].score;
    });
    // A retained target always receives one visibility query even if many new
    // candidates enter the cone. This keeps the visibility budget from
    // accidentally defeating hysteresis during dense encounters.
    const auto retainedIndex = std::find_if(
        eligibleIndices.begin(),
        eligibleIndices.end(),
        [&](size_t index) { return frame_.candidates[index].retainedTarget; });
    if (retainedIndex != eligibleIndices.end() && retainedIndex != eligibleIndices.begin()) {
        const size_t index = *retainedIndex;
        eligibleIndices.erase(retainedIndex);
        eligibleIndices.insert(eligibleIndices.begin(), index);
    }

    if (settings.requireWorldVisibility) {
        uint32_t queryBudget = settings.maximumVisibilityQueries;
        for (size_t index : eligibleIndices) {
            RailAimAssistCandidate& candidate = frame_.candidates[index];
            if (queryBudget == 0 || input.visibilityQuery == nullptr) {
                candidate.eligible = false;
                candidate.rejectReason = RailAimAssistRejectReason::VisibilityBudget;
                continue;
            }
            --queryBudget;
            ++frame_.visibilityQueries;
            candidate.visibilityTested = true;

            RailAimState visibilityAim = *input.rawAim;
            visibilityAim.worldRayDirection = NormalizeOr(
                Subtract(candidate.worldPosition, visibilityAim.worldRayOrigin),
                rawDirection);
            visibilityAim.maxDistance = (std::min)(
                input.rawAim->maxDistance,
                candidate.distance + 0.01f);
            ResetResolvedHit(visibilityAim);
            RailWorldRaycastInput query = *input.visibilityQuery;
            query.aim = &visibilityAim;
            const RailAimHit hit = RailWorldRaycast::Query(query);
            if (!hit.hit || hit.actorId != candidate.target.actorId ||
                hit.kind != ExpectedHitKind(candidate.target.kind)) {
                candidate.eligible = false;
                candidate.rejectReason = RailAimAssistRejectReason::WorldOccluded;
            }
        }
    }

    RailAimAssistCandidate* best = nullptr;
    RailAimAssistCandidate* retained = nullptr;
    for (RailAimAssistCandidate& candidate : frame_.candidates) {
        if (!candidate.eligible) {
            continue;
        }
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
        if (candidate.retainedTarget) {
            retained = &candidate;
        }
    }
    if (retained != nullptr && best != retained &&
        best != nullptr && best->score < retained->score + settings.targetSwitchAdvantage) {
        best = retained;
    }

    if (best == nullptr) {
        retainedTargetMissingSeconds_ += dt;
        if (retainedTargetMissingSeconds_ > (std::max)(0.0f, settings.targetRetentionSeconds)) {
            hasRetainedTarget_ = false;
            retainedTargetMissingSeconds_ = 0.0f;
        }
        return;
    }

    best->selected = true;
    retainedTarget_ = best->target;
    hasRetainedTarget_ = true;
    retainedTargetMissingSeconds_ = 0.0f;
    frame_.target = best->target;
    frame_.targetWorldPosition = best->worldPosition;
    frame_.targetScore = best->score;
    frame_.retainedTarget = best->retainedTarget;

    const Vector3 targetDirection = NormalizeOr(
        Subtract(best->worldPosition, input.rawAim->worldRayOrigin),
        rawDirection);
    const float intent = Clamp01(
        (std::max)(0.0f, input.reticleSpeedPixelsPerSecond) /
        (std::max)(1.0f, settings.highIntentReticleSpeed));
    const float intentScale = 1.0f - intent *
        (1.0f - Clamp01(settings.minimumHighIntentStrength));
    const float proximity = 1.0f - Clamp01(best->angularErrorDegrees / baseAcquireAngle);
    const float strength = Clamp01(
        MagnetismStrength(input.inputDevice, settings) *
        (0.35f + proximity * 0.65f) * intentScale);
    frame_.inputFrictionScale =
        1.0f - Clamp01(proximity * strength) * 0.48f;
    frame_.frictionActive = frame_.inputFrictionScale < 0.9999f;
    const float correctionLimit = (std::max)(0.0f, MaximumCorrection(input.inputDevice, settings));
    const float speedLimit = (std::max)(0.0f, settings.maximumCorrectionSpeedDegrees) * dt;
    const float correctionDegrees = (std::min)({
        best->angularErrorDegrees * strength,
        correctionLimit,
        speedLimit});
    if (correctionDegrees <= 0.00001f || best->angularErrorDegrees <= 0.00001f) {
        return;
    }

    const float blend = Clamp01(correctionDegrees / best->angularErrorDegrees);
    frame_.assistedAim.worldRayDirection = NormalizeOr(
        Add(Scale(rawDirection, 1.0f - blend), Scale(targetDirection, blend)),
        rawDirection);
    ResetResolvedHit(frame_.assistedAim);
    frame_.correctionDegrees = AngleDegrees(rawDirection, frame_.assistedAim.worldRayDirection);
    frame_.appliedStrength = strength;
    frame_.active = frame_.correctionDegrees > 0.00001f;
}

const char* ToRailAimAssistInputDeviceString(RailAimAssistInputDevice device) {
    switch (device) {
    case RailAimAssistInputDevice::MouseKeyboard: return "Mouse/Keyboard";
    case RailAimAssistInputDevice::Gamepad: return "Gamepad";
    }
    return "Unknown";
}

const char* ToRailAimAssistRejectReasonString(RailAimAssistRejectReason reason) {
    switch (reason) {
    case RailAimAssistRejectReason::None: return "None";
    case RailAimAssistRejectReason::Disabled: return "Disabled";
    case RailAimAssistRejectReason::LockModeActive: return "Lock Mode Active";
    case RailAimAssistRejectReason::InvalidAim: return "Invalid Aim";
    case RailAimAssistRejectReason::InvalidTarget: return "Invalid Target";
    case RailAimAssistRejectReason::OutOfRange: return "Out Of Range";
    case RailAimAssistRejectReason::OutsideAssistCone: return "Outside Assist Cone";
    case RailAimAssistRejectReason::RegistryOccluded: return "Registry Occluded";
    case RailAimAssistRejectReason::WorldOccluded: return "World Occluded";
    case RailAimAssistRejectReason::VisibilityBudget: return "Visibility Budget";
    }
    return "Unknown";
}
