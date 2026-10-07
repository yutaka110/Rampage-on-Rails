#include "RailCameraDirector.h"
#include "../diagnostics/DronePerformanceProfile.h"
#include "GameplaySettingsValidation.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(const Vector3& value) {
    return std::sqrt((std::max)(0.0f, Dot(value, value)));
}

float Length2(const Vector2& value) {
    return std::sqrt((std::max)(0.0f, value.x * value.x + value.y * value.y));
}

Vector2 Add2(const Vector2& a, const Vector2& b) {
    return {a.x + b.x, a.y + b.y};
}

Vector2 Scale2(const Vector2& value, float scale) {
    return {value.x * scale, value.y * scale};
}

Vector2 LerpVector2(const Vector2& a, const Vector2& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
    };
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float len2 = Dot(value, value);
    if (len2 <= 0.000001f) {
        return fallback;
    }
    const float invLen = 1.0f / std::sqrt(len2);
    return Scale(value, invLen);
}

Vector3 RotateAroundAxis(const Vector3& value, const Vector3& axis, float radians) {
    const Vector3 n = NormalizeOr(axis, {0.0f, 0.0f, 1.0f});
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Add(
        Add(Scale(value, c), Scale(Cross(n, value), s)),
        Scale(n, Dot(n, value) * (1.0f - c)));
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStep(float t) {
    const float x = (std::clamp)(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

CourseCameraKey LerpRig(const CourseCameraKey& a, const CourseCameraKey& b, float t) {
    CourseCameraKey result{};
    result.distance = Lerp(a.distance, b.distance, t);
    result.backDistance = Lerp(a.backDistance, b.backDistance, t);
    result.verticalOffset = Lerp(a.verticalOffset, b.verticalOffset, t);
    result.lateralOffset = Lerp(a.lateralOffset, b.lateralOffset, t);
    result.lookAheadDistance = Lerp(a.lookAheadDistance, b.lookAheadDistance, t);
    result.lookUpOffset = Lerp(a.lookUpOffset, b.lookUpOffset, t);
    result.lookForwardOffset = Lerp(a.lookForwardOffset, b.lookForwardOffset, t);
    result.fovY = Lerp(a.fovY, b.fovY, t);
    result.roll = Lerp(a.roll, b.roll, t);
    return result;
}

bool Contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

bool ContainsInsensitive(std::string value, const char* token) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string needle = token;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find(needle) != std::string::npos;
}

std::string SectionSignature(const CourseSection* section) {
    if (section == nullptr) {
        return "-";
    }
    return section->name + "|" + section->category;
}

std::string SectionDisplayName(const CourseSection* section) {
    if (section == nullptr) {
        return "-";
    }
    return section->category.empty() ? section->name : section->name + " [" + section->category + "]";
}

void AppendMode(std::string& mode, const char* label) {
    if (mode == "Default" || mode.empty()) {
        mode = label;
        return;
    }
    if (mode.find(label) == std::string::npos) {
        mode += " + ";
        mode += label;
    }
}

float Degrees(float value) {
    return value * kPi / 180.0f;
}

float RadiansToDegrees(float value) {
    return value * 180.0f / kPi;
}

Vector3 ResolveRailLocal(
    const RailPath& railPath,
    float spawnDistance,
    float distanceOffset,
    float lateralOffset,
    float verticalOffset) {
    const RailPathSample sample = railPath.Evaluate(spawnDistance + distanceOffset);
    return Add(
        Add(sample.position, Scale(sample.right, lateralOffset)),
        Scale(sample.up, verticalOffset));
}

float ActorDistance(float spawnDistance, float distanceOffset) {
    return spawnDistance + distanceOffset;
}

bool RoleLooksBoss(const std::string& role) {
    return ContainsInsensitive(role, "boss") ||
        ContainsInsensitive(role, "gatekeeper") ||
        ContainsInsensitive(role, "core");
}

struct RailCameraCompositionProjection {
    Vector2 normalized{};
    float forwardDistance = 0.0f;
    bool inFront = false;
};

RailCameraCompositionProjection ProjectCompositionPoint(
    const RailCameraDirectorFrame& frame,
    const Vector3& point,
    float aspectRatio) {
    const Vector3 forward = NormalizeOr(Subtract(frame.target, frame.position), frame.forward);
    const Vector3 right = NormalizeOr(Cross(frame.up, forward), {1.0f, 0.0f, 0.0f});
    const Vector3 up = NormalizeOr(Cross(forward, right), frame.up);
    const Vector3 delta = Subtract(point, frame.position);
    const float forwardDistance = Dot(delta, forward);

    RailCameraCompositionProjection result{};
    result.forwardDistance = forwardDistance;
    result.inFront = forwardDistance > 0.001f;
    if (!result.inFront) {
        return result;
    }

    const float tanY = std::tan((std::max)(0.05f, frame.rig.fovY) * 0.5f);
    const float tanX = tanY * (std::max)(0.25f, aspectRatio);
    result.normalized = {
        Dot(delta, right) / ((std::max)(0.001f, forwardDistance) * (std::max)(0.001f, tanX)),
        Dot(delta, up) / ((std::max)(0.001f, forwardDistance) * (std::max)(0.001f, tanY)),
    };
    return result;
}

bool SegmentIntersectsAabb(
    const Vector3& start,
    const Vector3& end,
    const Vector3& minBounds,
    const Vector3& maxBounds,
    float& outT) {
    const Vector3 direction = Subtract(end, start);
    float tMin = 0.02f;
    float tMax = 0.96f;
    const auto testAxis = [&](float startValue, float dirValue, float minValue, float maxValue) {
        if (std::abs(dirValue) <= 0.00001f) {
            return startValue >= minValue && startValue <= maxValue;
        }
        const float inv = 1.0f / dirValue;
        float t1 = (minValue - startValue) * inv;
        float t2 = (maxValue - startValue) * inv;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(start.x, direction.x, minBounds.x, maxBounds.x) ||
        !testAxis(start.y, direction.y, minBounds.y, maxBounds.y) ||
        !testAxis(start.z, direction.z, minBounds.z, maxBounds.z)) {
        return false;
    }
    outT = tMin;
    return true;
}

bool ResolveObstacleLineOfSightBlock(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    const Vector3& start,
    const Vector3& target,
    uint32_t ignoredObstacleActorId,
    float padding,
    uint32_t& outOccluderActorId,
    float& outBlockT) {
    float bestT = 1.0f;
    uint32_t bestOccluder = 0;
    for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
        if (obstacle.actorId == ignoredObstacleActorId) {
            continue;
        }
        const Vector3 center = ResolveRailLocal(
            railPath,
            obstacle.desc.spawnDistance,
            obstacle.desc.distanceOffset,
            obstacle.desc.lateralOffset,
            obstacle.desc.verticalOffset);
        const Vector3 extent{
            obstacle.desc.halfExtents.x + padding,
            obstacle.desc.halfExtents.y + padding,
            obstacle.desc.halfExtents.z + padding,
        };
        float hitT = 0.0f;
        if (SegmentIntersectsAabb(
                start,
                target,
                {center.x - extent.x, center.y - extent.y, center.z - extent.z},
                {center.x + extent.x, center.y + extent.y, center.z + extent.z},
                hitT) &&
            hitT < bestT) {
            bestT = hitT;
            bestOccluder = obstacle.actorId;
        }
    }
    if (bestOccluder == 0) {
        return false;
    }
    outOccluderActorId = bestOccluder;
    outBlockT = bestT;
    return true;
}

Vector3 ClosestPointOnAabb(const Vector3& point, const Vector3& minBounds, const Vector3& maxBounds) {
    return {
        (std::clamp)(point.x, minBounds.x, maxBounds.x),
        (std::clamp)(point.y, minBounds.y, maxBounds.y),
        (std::clamp)(point.z, minBounds.z, maxBounds.z),
    };
}

Vector3 ResolveAabbPushDirection(
    const Vector3& point,
    const Vector3& center,
    const Vector3& minBounds,
    const Vector3& maxBounds,
    const Vector3& fallback) {
    const Vector3 closest = ClosestPointOnAabb(point, minBounds, maxBounds);
    const Vector3 outsideDirection = Subtract(point, closest);
    if (Dot(outsideDirection, outsideDirection) > 0.000001f) {
        return NormalizeOr(outsideDirection, fallback);
    }

    const float dxMin = std::abs(point.x - minBounds.x);
    const float dxMax = std::abs(maxBounds.x - point.x);
    const float dyMin = std::abs(point.y - minBounds.y);
    const float dyMax = std::abs(maxBounds.y - point.y);
    const float dzMin = std::abs(point.z - minBounds.z);
    const float dzMax = std::abs(maxBounds.z - point.z);
    float best = dxMin;
    Vector3 direction{-1.0f, 0.0f, 0.0f};
    if (dxMax < best) {
        best = dxMax;
        direction = {1.0f, 0.0f, 0.0f};
    }
    if (dyMin < best) {
        best = dyMin;
        direction = {0.0f, -1.0f, 0.0f};
    }
    if (dyMax < best) {
        best = dyMax;
        direction = {0.0f, 1.0f, 0.0f};
    }
    if (dzMin < best) {
        best = dzMin;
        direction = {0.0f, 0.0f, -1.0f};
    }
    if (dzMax < best) {
        direction = {0.0f, 0.0f, 1.0f};
    }

    const Vector3 fromCenter = Subtract(point, center);
    if (Dot(fromCenter, fallback) > 0.0f) {
        direction = NormalizeOr(Add(direction, Scale(fallback, 0.35f)), direction);
    }
    return direction;
}

RailCameraDirectorMode ClassifyMode(const std::string& mode) {
    if (ContainsInsensitive(mode, "aim")) {
        return RailCameraDirectorMode::AimFocus;
    }
    if (ContainsInsensitive(mode, "event")) {
        return RailCameraDirectorMode::EventAccent;
    }
    if (ContainsInsensitive(mode, "boss")) {
        return RailCameraDirectorMode::Boss;
    }
    if (ContainsInsensitive(mode, "tunnel") || ContainsInsensitive(mode, "obstacle")) {
        return RailCameraDirectorMode::Tunnel;
    }
    if (ContainsInsensitive(mode, "high speed") || ContainsInsensitive(mode, "escape")) {
        return RailCameraDirectorMode::HighSpeed;
    }
    if (ContainsInsensitive(mode, "setpiece") || ContainsInsensitive(mode, "falling")) {
        return RailCameraDirectorMode::Setpiece;
    }
    if (ContainsInsensitive(mode, "combat") ||
        ContainsInsensitive(mode, "contact") ||
        ContainsInsensitive(mode, "crossfire") ||
        ContainsInsensitive(mode, "encounter framing") ||
        ContainsInsensitive(mode, "authored encounter") ||
        ContainsInsensitive(mode, "readability") ||
        ContainsInsensitive(mode, "composition safety") ||
        ContainsInsensitive(mode, "line-of-sight") ||
        ContainsInsensitive(mode, "camera collision") ||
        ContainsInsensitive(mode, "segment transition")) {
        return RailCameraDirectorMode::Combat;
    }
    if (ContainsInsensitive(mode, "recovery")) {
        return RailCameraDirectorMode::Recovery;
    }
    if (mode != "Default") {
        return RailCameraDirectorMode::Cinematic;
    }
    return RailCameraDirectorMode::Chase;
}
} // namespace

const char* ToRailCameraDirectorModeString(RailCameraDirectorMode mode) {
    switch (mode) {
    case RailCameraDirectorMode::Chase:
        return "Chase";
    case RailCameraDirectorMode::Combat:
        return "Combat";
    case RailCameraDirectorMode::AimFocus:
        return "Aim Focus";
    case RailCameraDirectorMode::HighSpeed:
        return "High Speed";
    case RailCameraDirectorMode::Tunnel:
        return "Tunnel";
    case RailCameraDirectorMode::Boss:
        return "Boss";
    case RailCameraDirectorMode::Setpiece:
        return "Setpiece";
    case RailCameraDirectorMode::Cinematic:
        return "Cinematic";
    case RailCameraDirectorMode::EventAccent:
        return "Event Accent";
    case RailCameraDirectorMode::Recovery:
        return "Recovery";
    }
    return "Chase";
}

const char* ToRailCameraLookAtPolicyString(RailCameraLookAtPolicy policy) {
    switch (policy) {
    case RailCameraLookAtPolicy::RailLookAhead:
        return "Rail Look-Ahead";
    case RailCameraLookAtPolicy::LockToken:
        return "Lock Token";
    case RailCameraLookAtPolicy::ThreatCenter:
        return "Threat Center";
    case RailCameraLookAtPolicy::BossThreat:
        return "Boss Threat";
    case RailCameraLookAtPolicy::Obstacle:
        return "Obstacle";
    }
    return "Rail Look-Ahead";
}

bool RailCameraComfortSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({stableAngularVelocityDeg, stableAngularAccelerationDeg,
        stableFovChangeRateDeg, stableRollDeg, stableShakeAmount,
        hardTransitionAngularVelocityDeg, hardTransitionFovChangeRateDeg, hardTransitionRollDeg}) &&
        hardTransitionAngularVelocityDeg >= stableAngularVelocityDeg &&
        hardTransitionFovChangeRateDeg >= stableFovChangeRateDeg &&
        hardTransitionRollDeg >= stableRollDeg,
        errorMessage, "Camera comfort limits must be finite/nonnegative; hard limits must be at least stable limits.");
}

bool RailCameraAimFocusSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({blendInRate, blendOutRate, maxReticleVelocityForFullFocus,
        lookAheadBoost, backDistanceBoost}) &&
        InRange(fovOffsetDeg, -178.0f, 178.0f) && InRange(maxLockFovOffsetDeg, -178.0f, 178.0f) &&
        UnitInterval({rollSuppression, shakeSuppression, lateralSuppression}),
        errorMessage, "Aim-focus rates/offsets must be finite and suppression must be in [0,1].");
}

bool RailCameraLookAtSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({blendRate, releaseBlendRate, maxForwardDistance,
        lockTokenWeight, enemyWeight, bossWeight, obstacleWeight, maxTargetOffset}) &&
        InRange(minForwardDistance, -100000.0f, maxForwardDistance) &&
        minForwardDistance < maxForwardDistance && UnitInterval({centerRetention}),
        errorMessage, "Look-at settings require ordered finite distances, nonnegative weights and retention in [0,1].");
}

bool RailCameraCompositionSafetySettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(InRange(aimableZoneWidth, 0.01f, 1.0f) && InRange(aimableZoneHeight, 0.01f, 1.0f) &&
        InRange(readabilityZoneWidth, aimableZoneWidth, 1.0f) &&
        InRange(readabilityZoneHeight, aimableZoneHeight, 1.0f) &&
        NonNegative({minForwardDistance, maxForwardDistance, blendInRate, blendOutRate,
            maxTargetCorrection, fovExpandDeg, lockTokenWeight, bossWeight, enemyWeight, obstacleWeight}) &&
        minForwardDistance < maxForwardDistance && InRange(maxFovDeg, 1.0f, 179.0f) &&
        UnitInterval({correctionGain, fireBlockRisk}),
        errorMessage, "Composition zones must be nested in (0,1], distances ordered, FOV valid and strengths in [0,1].");
}

bool RailCameraLineOfSightSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({minForwardDistance, maxForwardDistance, obstaclePadding, fovExpandDeg}) &&
        minForwardDistance < maxForwardDistance && UnitInterval({targetReleaseStrength}) &&
        InRange(maxFovDeg, 1.0f, 179.0f),
        errorMessage, "Line-of-sight settings require ordered distances, nonnegative padding, valid FOV and strength in [0,1].");
}

bool RailCameraCollisionProtectionSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({obstaclePadding, minClearance, nearClipClearanceMultiplier,
        maxPushDistance, fovExpandDeg}) && UnitInterval({targetCompensation}) &&
        InRange(maxFovDeg, 1.0f, 179.0f),
        errorMessage, "Camera collision clearances must be finite/nonnegative, FOV valid and compensation in [0,1].");
}

bool RailCameraSegmentTransitionSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(InRange(minDuration, 0.01f, 30.0f) &&
        InRange(duration, minDuration, 30.0f) && InRange(highSpeedDuration, minDuration, 30.0f) &&
        InRange(bossDuration, minDuration, 30.0f) && InRange(tunnelDuration, minDuration, 30.0f) &&
        NonNegative({maxPositionBlendDistance, maxTargetBlendDistance, enemyFireHold}) &&
        UnitInterval({rollBlendStrength, fovBlendStrength, shakeDampen}) &&
        InRange(comfortGraceMultiplier, 1.0f, 100.0f),
        errorMessage, "Transition durations must respect a positive minimum, blend strengths [0,1] and grace >= 1.");
}

bool RailCameraEncounterFramingSettings::Validate(std::string* errorMessage) const {
    using namespace gameplay::settings;
    return Result(NonNegative({blendInRate, blendOutRate, waveHoldDuration, bossHoldDuration,
        obstacleHoldDuration, maxForwardDistance, minActiveEnemyFocus, enemyCountForFullWide,
        enemySpreadForFullWide, fovExpandDeg, bossFovExpandDeg, singleThreatFovTightenDeg,
        singleThreatBackDistancePullIn, singleThreatLookAheadReduction, lookAheadBoost,
        backDistanceBoost, fireHoldDuration}) &&
        InRange(minForwardDistance, -100000.0f, maxForwardDistance) && minForwardDistance < maxForwardDistance &&
        enemyCountForFullWide >= minActiveEnemyFocus && enemyCountForFullWide > 0.0f &&
        enemySpreadForFullWide > 0.0f && InRange(maxFovDeg, 1.0f, 179.0f) &&
        UnitInterval({bossFocusBoost, lateralDampen, rollDampen}),
        errorMessage, "Encounter framing requires ordered distances/counts, positive spread, valid FOV and strengths [0,1].");
}

// 設定変更は検証後に反映する。毎フレームの調整でカメラの平滑化履歴は消さない。
bool RailCameraDirector::ConfigureComfort(const RailCameraComfortSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    comfortSettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureAimFocus(const RailCameraAimFocusSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    aimFocusSettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureLookAt(const RailCameraLookAtSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    lookAtSettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureCompositionSafety(
    const RailCameraCompositionSafetySettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    compositionSafetySettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureLineOfSight(
    const RailCameraLineOfSightSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    lineOfSightSettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureCollisionProtection(
    const RailCameraCollisionProtectionSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    collisionProtectionSettings_ = settings;
    return true;
}

bool RailCameraDirector::ConfigureSegmentTransition(
    const RailCameraSegmentTransitionSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    segmentTransitionSettings_ = settings;
    if (!settings.enabled) {
        segmentTransitionElapsed_ = segmentTransitionDuration_ = 0.0f;
        hasSegmentTransitionStartFrame_ = false;
    }
    return true;
}

bool RailCameraDirector::ConfigureEncounterFraming(
    const RailCameraEncounterFramingSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    encounterFramingSettings_ = settings;
    const float maxHold = (std::max)({settings.waveHoldDuration, settings.bossHoldDuration, settings.obstacleHoldDuration});
    encounterFramingHoldRemaining_ = settings.enabled ? (std::min)(encounterFramingHoldRemaining_, maxHold) : 0.0f;
    encounterFireHoldRemaining_ = settings.enabled ? (std::min)(encounterFireHoldRemaining_, settings.fireHoldDuration) : 0.0f;
    return true;
}

void RailCameraDirector::Reset() {
    vehicleFramingPullback_ = 0.0f;
    vehicleFramingLift_ = 0.0f;
    smoothedRig_ = {};
    lastFrame_ = {};
    hasSmoothedRig_ = false;
    hasPreviousComfortFrame_ = false;
    previousPosition_ = {};
    previousForward_ = {0.0f, 0.0f, 1.0f};
    previousFovY_ = 0.30f * kPi;
    previousRoll_ = 0.0f;
    previousAngularVelocityDeg_ = 0.0f;
    aimFocusBlend_ = 0.0f;
    encounterFramingBlend_ = 0.0f;
    encounterFramingHoldRemaining_ = 0.0f;
    encounterFireHoldRemaining_ = 0.0f;
    encounterFramingReason_ = "no encounter";
    lookAtBlend_ = 0.0f;
    smoothedLookAtTarget_ = {};
    hasSmoothedLookAtTarget_ = false;
    segmentTransitionStartFrame_ = {};
    previousTransitionFrame_ = {};
    previousTransitionCameraAnchor_ = {};
    previousTransitionTargetAnchor_ = {};
    segmentTransitionCameraAnchor_ = {};
    segmentTransitionTargetAnchor_ = {};
    hasSegmentTransitionStartFrame_ = false;
    currentSectionSignature_.clear();
    previousSectionSignature_.clear();
    segmentTransitionElapsed_ = 0.0f;
    segmentTransitionDuration_ = 0.0f;
    fovKick_ = 0.0f;
    rollKick_ = 0.0f;
    shakeTime_ = 0.0f;
    shakeAmplitude_ = 0.0f;
    directorTime_ = 0.0f;
}

void RailCameraDirector::NotifyCourseEvents(const std::vector<CourseEventMarker>& events) {
    for (const CourseEventMarker& event : events) {
        if (encounterFramingSettings_.enabled) {
            const std::string eventName = event.id.empty() ? event.type : event.id;
            if (event.type == "enemy_wave") {
                encounterFramingHoldRemaining_ = (std::max)(
                    encounterFramingHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.waveHoldDuration));
                encounterFireHoldRemaining_ = (std::max)(
                    encounterFireHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.fireHoldDuration));
                encounterFramingReason_ = "enemy wave: " + eventName;
            } else if (event.type == "boss") {
                encounterFramingHoldRemaining_ = (std::max)(
                    encounterFramingHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.bossHoldDuration));
                encounterFireHoldRemaining_ = (std::max)(
                    encounterFireHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.fireHoldDuration));
                encounterFramingReason_ = "boss entry: " + eventName;
            } else if (event.type == "boss_phase") {
                encounterFramingHoldRemaining_ = (std::max)(
                    encounterFramingHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.bossHoldDuration * 0.62f));
                encounterFireHoldRemaining_ = (std::max)(
                    encounterFireHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.fireHoldDuration));
                encounterFramingReason_ = "boss phase: " + eventName;
            } else if (event.type == "obstacle") {
                encounterFramingHoldRemaining_ = (std::max)(
                    encounterFramingHoldRemaining_,
                    (std::max)(0.0f, encounterFramingSettings_.obstacleHoldDuration));
                encounterFramingReason_ = "obstacle reveal: " + eventName;
            }
        }

        if (event.type == "boss") {
            fovKick_ -= Degrees(5.0f);
            rollKick_ += Degrees(2.0f);
            shakeTime_ = (std::max)(shakeTime_, 1.25f);
            shakeAmplitude_ = (std::max)(shakeAmplitude_, 1.6f);
        } else if (event.type == "boss_phase") {
            fovKick_ += Degrees(3.0f);
            rollKick_ -= Degrees(3.0f);
            shakeTime_ = (std::max)(shakeTime_, 0.85f);
            shakeAmplitude_ = (std::max)(shakeAmplitude_, 1.25f);
        } else if (event.type == "setpiece") {
            fovKick_ += Degrees(4.0f);
            rollKick_ += Degrees(1.6f);
            shakeTime_ = (std::max)(shakeTime_, 0.7f);
            shakeAmplitude_ = (std::max)(shakeAmplitude_, 0.85f);
        } else if (event.type == "obstacle") {
            shakeTime_ = (std::max)(shakeTime_, 0.45f);
            shakeAmplitude_ = (std::max)(shakeAmplitude_, 0.55f);
        } else if (event.type == "checkpoint") {
            fovKick_ -= Degrees(2.0f);
        }
    }
}

void RailCameraDirector::AddFeedbackImpulse(float shakeAmplitude, float fovKick, float rollKick) {
    if (shakeAmplitude > 0.0f) {
        shakeTime_ = (std::max)(shakeTime_, 0.22f + shakeAmplitude * 0.18f);
        shakeAmplitude_ = (std::max)(shakeAmplitude_, shakeAmplitude);
    }
    fovKick_ += fovKick;
    rollKick_ += rollKick;
}

RailCameraDirectorFrame RailCameraDirector::Evaluate(const RailCameraDirectorFrameInput& input) {
    const drone_perf::Scope profile(drone_perf::Stage::Camera);
    RailCameraDirectorFrame frame{};
    if (input.course == nullptr || input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        lastFrame_ = frame;
        return frame;
    }

    directorTime_ += (std::max)(0.0f, input.deltaTime);
    BeginSegmentTransitionIfNeeded(input);
    CourseCameraKey targetRig = input.course->EvaluateCamera(input.distance);
    frame.mode = "Default";
    ApplySectionDirecting(targetRig, input.section, frame.mode);
    ApplyCinematicShotDirecting(
        targetRig,
        input.course,
        input.distance,
        input.preferredCinematicShotId,
        input.preferredCinematicShotWeight,
        frame.mode,
        frame);
    ApplyEventDirecting(targetRig, input.deltaTime, frame.mode);
    ApplyEncounterFramingRules(targetRig, input, frame.mode, frame);
    if (input.authoredEncounterCompositionActive) {
        const float authoredWeight = (std::clamp)(
            input.authoredEncounterCompositionWeight, 0.0f, 1.0f);
        targetRig.fovY = (std::clamp)(
            targetRig.fovY +
                input.authoredEncounterFovOffsetRadians,
            Degrees(36.0f), Degrees(78.0f));
        targetRig.backDistance = (std::max)(
            0.25f,
            targetRig.backDistance +
                input.authoredEncounterBackDistanceOffset);
        frame.authoredEncounterCompositionActive = true;
        frame.authoredEncounterCompositionWeight = authoredWeight;
        AppendMode(frame.mode, "Authored Encounter Beat");
    }
    ApplyAimFocusStabilization(targetRig, input, frame.mode, frame);
    frame.rig = SmoothRig(targetRig, input.deltaTime);

    const RailPathSample cameraSample = input.railPath->Evaluate(input.distance);
    const RailPathSample lookSample = input.railPath->Evaluate(input.distance + frame.rig.lookAheadDistance);
    frame.position = Add(
        Add(
            Add(cameraSample.position, Scale(cameraSample.up, frame.rig.verticalOffset)),
            Scale(cameraSample.right, frame.rig.lateralOffset)),
        Scale(cameraSample.tangent, -frame.rig.backDistance));
    frame.target = Add(
        Add(lookSample.position, Scale(lookSample.up, frame.rig.lookUpOffset)),
        Scale(lookSample.tangent, frame.rig.lookForwardOffset));
    if (input.mountedCameraActive) {
        const Vector3 desiredMountPosition = Add(
            input.mountedCameraAnchor,
            input.mountedCameraOffset);
        Vector3 correction = Subtract(desiredMountPosition, frame.position);
        const float correctionLength = Length(correction);
        const float maximumCorrection = (std::max)(
            0.0f, input.mountedMaximumCorrection);
        if (maximumCorrection > 0.0f &&
            correctionLength > maximumCorrection) {
            correction = Scale(
                correction,
                maximumCorrection / correctionLength);
        }
        frame.position = Add(
            frame.position,
            Scale(correction, (std::clamp)(
                input.mountedCameraBlend, 0.0f, 1.0f)));
        frame.target = Add(frame.target, input.mountedTargetOffset);
        frame.rig.roll += input.mountedRollOffsetRadians;
    }
    if (input.cameraInertiaActive) {
        frame.position = Add(frame.position, input.cameraInertiaGameplayPositionOffset);
        frame.target = Add(frame.target, input.cameraInertiaGameplayTargetOffset);
        frame.rig.roll += input.cameraInertiaRollOffsetRadians;
        frame.rig.fovY = (std::clamp)(
            frame.rig.fovY + input.cameraInertiaFovOffsetRadians,
            Degrees(36.0f), Degrees(78.0f));
    }
    frame.baseTarget = frame.target;
    UpdateLookAtTarget(frame, input, cameraSample);
    if (input.authoredEncounterCompositionActive &&
        input.authoredEncounterHasFocusWorld) {
        const float focusWeight = (std::clamp)(
            input.authoredEncounterFocusWeight, 0.0f, 1.0f);
        frame.target = Add(
            Scale(frame.target, 1.0f - focusWeight),
            Scale(input.authoredEncounterFocusWorld, focusWeight));
        frame.threatCenter = input.authoredEncounterFocusWorld;
        frame.authoredEncounterFocusWeight = focusWeight;
    }
    ApplyCompositionSafety(frame, input, cameraSample, frame.mode);
    ApplySegmentTransitionPolish(frame, input, frame.mode);
    previousTransitionFrame_ = frame;
    previousTransitionCameraAnchor_ = cameraSample;
    previousTransitionTargetAnchor_ = lookSample;
    ApplyVehicleFraming(frame, input, cameraSample);
    // Collision and sight checks must inspect the corrected/transitioned pose.
    ApplyCameraCollisionProtection(frame, input, cameraSample, frame.mode);
    ApplyLineOfSightSafety(frame, input, frame.mode);

    frame.gameplayPosition = frame.position;
    frame.gameplayTarget = frame.target;
    frame.gameplayForward = NormalizeOr(
        Subtract(frame.gameplayTarget, frame.gameplayPosition),
        cameraSample.tangent);
    frame.gameplayUp = RotateAroundAxis(
        cameraSample.up,
        frame.gameplayForward,
        frame.rig.roll);

    if (input.cameraInertiaActive) {
        frame.position = Add(frame.position, input.cameraInertiaPresentationPositionOffset);
        frame.target = Add(frame.target, input.cameraInertiaPresentationTargetOffset);
    }

    const float cameraShakeScale = 1.0f - aimFocusBlend_ * aimFocusSettings_.shakeSuppression;
    if (shakeTime_ > 0.0f && shakeAmplitude_ > 0.0f) {
        const float envelope = (std::clamp)(shakeTime_, 0.0f, 1.0f);
        const float phaseA = std::sin(directorTime_ * 38.0f);
        const float phaseB = std::sin(directorTime_ * 57.0f + 1.7f);
        const float effectiveShake = shakeAmplitude_ * (std::clamp)(cameraShakeScale, 0.0f, 1.0f);
        const Vector3 offset = Add(
            Scale(cameraSample.right, phaseA * effectiveShake * 0.12f * envelope),
            Scale(cameraSample.up, phaseB * effectiveShake * 0.08f * envelope));
        frame.position = Add(frame.position, offset);
        frame.target = Add(frame.target, Scale(offset, 0.35f));
        frame.shakeAmount = effectiveShake * envelope;
    }
    frame.shakeAmount *= (std::clamp)(cameraShakeScale, 0.0f, 1.0f);
    if (frame.shakeAmount > 0.0f) {
        const float phaseA = std::sin(directorTime_ * 23.0f + 0.4f);
        const float phaseB = std::sin(directorTime_ * 31.0f + 2.2f);
        const Vector3 offset = Add(
            Scale(cameraSample.right, phaseA * frame.shakeAmount * 0.045f),
            Scale(cameraSample.up, phaseB * frame.shakeAmount * 0.030f));
        frame.position = Add(frame.position, offset);
        frame.target = Add(frame.target, Scale(offset, 0.25f));
    }

    frame.forward = NormalizeOr(Subtract(frame.target, frame.position), cameraSample.tangent);
    frame.up = RotateAroundAxis(cameraSample.up, frame.forward, frame.rig.roll);
    frame.fovY = frame.rig.fovY;
    frame.modeKind = ClassifyMode(frame.mode);
    frame.railSpeed = input.railSpeed > 0.0f ? input.railSpeed : cameraSample.speed;
    MeasureVehicleFraming(frame, input);
    UpdateComfortMetrics(frame, input);
    lastFrame_ = frame;
    return frame;
}

void RailCameraDirector::MeasureVehicleFraming(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input) const {
    if (!input.vehicleFramingActive) return;
    const float aspect = input.viewportHeight > 0 && input.viewportWidth > 0
        ? static_cast<float>(input.viewportWidth) / input.viewportHeight
        : 16.0f / 9.0f;
    float left = 1.0e6f;
    float right = -1.0e6f;
    frame.vehicleScreenTop = 1.0f;
    for (const Vector3& corner : input.vehicleBoundsCorners) {
        const auto projected = ProjectCompositionPoint(frame, corner, aspect);
        if (!projected.inFront || projected.forwardDistance <= input.nearClipDistance) {
            frame.vehicleFramingSafe = false;
            return;
        }
        left = (std::min)(left, projected.normalized.x);
        right = (std::max)(right, projected.normalized.x);
        frame.vehicleScreenTop = (std::min)(frame.vehicleScreenTop,
            (1.0f - projected.normalized.y) * 0.5f);
    }
    frame.vehicleScreenWidth = (right - left) * 0.5f;
    // Allow a small presentation-shake margin beyond the correction target.
    frame.vehicleFramingSafe = frame.vehicleScreenTop >= 0.62f &&
        frame.vehicleScreenWidth <= 0.36f;
}

void RailCameraDirector::ApplyVehicleFraming(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    const RailPathSample& cameraSample) {
    if (!input.vehicleFramingActive) {
        vehicleFramingPullback_ = 0.0f;
        vehicleFramingLift_ = 0.0f;
        return;
    }
    const Vector3 forward = NormalizeOr(
        Subtract(frame.target, frame.position), cameraSample.tangent);
    const Vector3 rolledUp = RotateAroundAxis(cameraSample.up, forward, frame.rig.roll);
    const Vector3 right = NormalizeOr(Cross(rolledUp, forward), cameraSample.right);
    const Vector3 up = NormalizeOr(Cross(forward, right), rolledUp);
    const float aspect = input.viewportWidth > 0 && input.viewportHeight > 0
        ? static_cast<float>(input.viewportWidth) / input.viewportHeight
        : 16.0f / 9.0f;
    const float tanY = std::tan((std::max)(0.05f, frame.rig.fovY) * 0.5f);
    const float tanX = tanY * (std::max)(0.25f, aspect);
    // Move along the view axes without changing aim direction or zooming FOV.
    // A bounded search keeps the body below 65% of the screen and under 34%
    // of its width. The authored rig remains responsible for normal framing.
    const auto fitsWidth = [&](float pullback) {
        float left = 1.0e6f;
        float rightEdge = -1.0e6f;
        for (const Vector3& corner : input.vehicleBoundsCorners) {
            const Vector3 delta = Subtract(corner, frame.position);
            const float depth = Dot(delta, forward) + pullback;
            if (depth <= (std::max)(1.0f, input.nearClipDistance * 2.0f)) {
                return false;
            }
            const float x = Dot(delta, right) / (depth * tanX);
            left = (std::min)(left, x);
            rightEdge = (std::max)(rightEdge, x);
        }
        return (rightEdge - left) * 0.5f <= 0.34f;
    };
    float pullback = 0.0f;
    if (!fitsWidth(0.0f)) {
        float low = 0.0f;
        float high = 12.0f;
        // Sub-millimetre search avoids visible quarter-unit camera steps.
        for (int iteration = 0; iteration < 14; ++iteration) {
            const float mid = (low + high) * 0.5f;
            if (fitsWidth(mid)) high = mid;
            else low = mid;
        }
        pullback = high;
    }
    const float decay = std::exp(-(std::max)(0.0f, input.deltaTime) * 4.0f);
    // Resolve occlusion immediately; return to the authored rig gradually.
    vehicleFramingPullback_ = (std::max)(pullback, vehicleFramingPullback_ * decay);
    float lift = 0.0f;
    for (const Vector3& corner : input.vehicleBoundsCorners) {
        const Vector3 delta = Subtract(corner, frame.position);
        const float depth = Dot(delta, forward) + vehicleFramingPullback_;
        lift = (std::max)(lift, Dot(delta, up) + 0.30f * tanY * depth);
    }
    vehicleFramingLift_ = (std::max)(
        (std::clamp)(lift, 0.0f, 8.0f), vehicleFramingLift_ * decay);
    const Vector3 correction = Add(
        Scale(forward, -vehicleFramingPullback_), Scale(up, vehicleFramingLift_));
    frame.position = Add(frame.position, correction);
    frame.target = Add(frame.target, correction);
    frame.baseTarget = Add(frame.baseTarget, correction);
    frame.forward = forward;
    frame.up = rolledUp;
    frame.vehicleFramingPullback = vehicleFramingPullback_;
    frame.vehicleFramingLift = vehicleFramingLift_;
}

CourseCameraKey RailCameraDirector::SmoothRig(const CourseCameraKey& target, float deltaTime) {
    if (!hasSmoothedRig_) {
        smoothedRig_ = target;
        hasSmoothedRig_ = true;
        return smoothedRig_;
    }

    const float t = 1.0f - std::exp(-(std::max)(0.0f, deltaTime) * 7.5f);
    smoothedRig_ = LerpRig(smoothedRig_, target, (std::clamp)(t, 0.0f, 1.0f));
    smoothedRig_.distance = target.distance;
    return smoothedRig_;
}

float RailCameraDirector::UpdateAimFocusBlend(const RailCameraDirectorFrameInput& input) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    const bool wantsFocus =
        aimFocusSettings_.enabled &&
        (input.lockHeld || input.lockTokenCount > 0 || input.lockPressed);
    const float rate = wantsFocus ? aimFocusSettings_.blendInRate : aimFocusSettings_.blendOutRate;
    const float target = wantsFocus ? 1.0f : 0.0f;
    const float t = 1.0f - std::exp(-dt * (std::max)(0.0f, rate));
    aimFocusBlend_ = Lerp(aimFocusBlend_, target, (std::clamp)(t, 0.0f, 1.0f));
    if (input.lockReleased && input.lockTokenCount <= 0) {
        aimFocusBlend_ *= 0.72f;
    }
    aimFocusBlend_ = (std::clamp)(aimFocusBlend_, 0.0f, 1.0f);
    return aimFocusBlend_;
}

void RailCameraDirector::ApplyEncounterFramingRules(
    CourseCameraKey& rig,
    const RailCameraDirectorFrameInput& input,
    std::string& mode,
    RailCameraDirectorFrame& frame) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    encounterFramingHoldRemaining_ = (std::max)(0.0f, encounterFramingHoldRemaining_ - dt);
    encounterFireHoldRemaining_ = (std::max)(0.0f, encounterFireHoldRemaining_ - dt);

    frame.encounterFramingActive = false;
    frame.encounterFramingBlend = encounterFramingBlend_;
    frame.encounterFramingFovOffsetDeg = 0.0f;
    frame.encounterFramingThreatSpread = 0.0f;
    frame.encounterFramingSingleThreatFocus = 0.0f;
    frame.encounterFramingRemaining = encounterFramingHoldRemaining_;
    frame.encounterFramingEnemyCount = 0;
    frame.encounterFramingBossCount = 0;
    frame.encounterFramingReason = "no encounter framing";

    if (!encounterFramingSettings_.enabled) {
        const float releaseT =
            1.0f - std::exp(-dt * (std::max)(0.0f, encounterFramingSettings_.blendOutRate));
        encounterFramingBlend_ = Lerp(encounterFramingBlend_, 0.0f, (std::clamp)(releaseT, 0.0f, 1.0f));
        frame.encounterFramingBlend = encounterFramingBlend_;
        frame.encounterFramingReason = "encounter framing disabled";
        return;
    }

    float minLateral = 9999.0f;
    float maxLateral = -9999.0f;
    float minVertical = 9999.0f;
    float maxVertical = -9999.0f;
    float closestForward = 9999.0f;
    bool hasRuntimeThreat = false;

    if (input.spawnRuntime != nullptr) {
        for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
            const float actorDistance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
            const float forward = actorDistance - input.distance;
            if (forward < encounterFramingSettings_.minForwardDistance ||
                forward > encounterFramingSettings_.maxForwardDistance) {
                continue;
            }

            hasRuntimeThreat = true;
            ++frame.encounterFramingEnemyCount;
            if (RoleLooksBoss(enemy.desc.role)) {
                ++frame.encounterFramingBossCount;
            }
            minLateral = (std::min)(minLateral, enemy.desc.lateralOffset);
            maxLateral = (std::max)(maxLateral, enemy.desc.lateralOffset);
            minVertical = (std::min)(minVertical, enemy.desc.verticalOffset);
            maxVertical = (std::max)(maxVertical, enemy.desc.verticalOffset);
            closestForward = (std::min)(closestForward, forward);
        }
    }

    const float lateralSpread = hasRuntimeThreat ? (maxLateral - minLateral) : 0.0f;
    const float verticalSpread = hasRuntimeThreat ? (maxVertical - minVertical) : 0.0f;
    const float spread = std::sqrt(lateralSpread * lateralSpread + verticalSpread * verticalSpread);
    frame.encounterFramingThreatSpread = spread;

    const float enemyPressure = frame.encounterFramingEnemyCount > 0
        ? (std::clamp)(
              (static_cast<float>(frame.encounterFramingEnemyCount) -
                  (std::max)(0.0f, encounterFramingSettings_.minActiveEnemyFocus)) /
                  (std::max)(
                      1.0f,
                      encounterFramingSettings_.enemyCountForFullWide -
                          encounterFramingSettings_.minActiveEnemyFocus),
              0.0f,
              1.0f)
        : 0.0f;
    const float spreadPressure = (std::clamp)(
        spread / (std::max)(1.0f, encounterFramingSettings_.enemySpreadForFullWide),
        0.0f,
        1.0f);
    const float bossPressure = frame.encounterFramingBossCount > 0
        ? (std::clamp)(encounterFramingSettings_.bossFocusBoost, 0.0f, 1.0f)
        : 0.0f;
    const float entryPressure = encounterFramingHoldRemaining_ > 0.0f ? 0.55f : 0.0f;
    const float singleThreatPressure =
        frame.encounterFramingEnemyCount == 1 &&
        frame.encounterFramingBossCount == 0 &&
        spread <= encounterFramingSettings_.enemySpreadForFullWide * 0.25f
        ? 1.0f
        : 0.0f;
    frame.encounterFramingSingleThreatFocus = singleThreatPressure;
    float targetBlend = (std::max)((std::max)(enemyPressure, spreadPressure), bossPressure);
    targetBlend = (std::max)(targetBlend, entryPressure);
    targetBlend = (std::max)(targetBlend, singleThreatPressure);

    if (!hasRuntimeThreat && encounterFramingHoldRemaining_ <= 0.0f) {
        targetBlend = 0.0f;
    }

    const float rate = targetBlend > encounterFramingBlend_
        ? encounterFramingSettings_.blendInRate
        : encounterFramingSettings_.blendOutRate;
    const float blendT = 1.0f - std::exp(-dt * (std::max)(0.0f, rate));
    encounterFramingBlend_ = Lerp(encounterFramingBlend_, targetBlend, (std::clamp)(blendT, 0.0f, 1.0f));
    encounterFramingBlend_ = (std::clamp)(encounterFramingBlend_, 0.0f, 1.0f);

    frame.encounterFramingActive = encounterFramingBlend_ > 0.015f;
    frame.encounterFramingBlend = encounterFramingBlend_;
    frame.encounterFramingRemaining = encounterFramingHoldRemaining_;
    if (!frame.encounterFramingActive) {
        frame.encounterFramingReason = "no encounter framing";
        return;
    }

    const bool bossFocus = frame.encounterFramingBossCount > 0;
    const float widePressure = (std::max)(enemyPressure, spreadPressure);
    const float fovExpand =
        (encounterFramingSettings_.fovExpandDeg * widePressure +
            encounterFramingSettings_.bossFovExpandDeg * bossPressure +
            encounterFramingSettings_.fovExpandDeg * 0.35f * entryPressure -
            encounterFramingSettings_.singleThreatFovTightenDeg *
                singleThreatPressure) *
        encounterFramingBlend_;
    rig.fovY = (std::clamp)(
        rig.fovY + Degrees(fovExpand),
        Degrees(34.0f),
        Degrees((std::max)(40.0f, encounterFramingSettings_.maxFovDeg)));
    const float lookAheadOffset =
        (encounterFramingSettings_.lookAheadBoost *
             (widePressure + bossPressure + entryPressure * 0.35f) -
         encounterFramingSettings_.singleThreatLookAheadReduction *
             singleThreatPressure) *
        encounterFramingBlend_;
    const float backDistanceOffset =
        (encounterFramingSettings_.backDistanceBoost *
             (widePressure + bossPressure + entryPressure * 0.35f) -
         encounterFramingSettings_.singleThreatBackDistancePullIn *
             singleThreatPressure) *
        encounterFramingBlend_;
    rig.lookAheadDistance = (std::max)(
        0.0f, rig.lookAheadDistance + lookAheadOffset);
    rig.backDistance = (std::max)(
        0.25f, rig.backDistance + backDistanceOffset);
    rig.lateralOffset *= 1.0f -
        (std::clamp)(encounterFramingSettings_.lateralDampen * encounterFramingBlend_, 0.0f, 0.95f);
    rig.roll *= 1.0f -
        (std::clamp)(encounterFramingSettings_.rollDampen * encounterFramingBlend_, 0.0f, 0.95f);

    frame.encounterFramingFovOffsetDeg = fovExpand;
    if (encounterFramingHoldRemaining_ > 0.0f) {
        frame.encounterFramingReason = encounterFramingReason_;
    } else if (bossFocus) {
        frame.encounterFramingReason = "boss threat framing";
    } else if (singleThreatPressure > 0.0f) {
        frame.encounterFramingReason = "single threat hero framing";
    } else if (spreadPressure >= enemyPressure) {
        frame.encounterFramingReason = "wide enemy spread";
    } else {
        frame.encounterFramingReason = "active enemy count";
    }
    if (closestForward < 6.0f && hasRuntimeThreat) {
        frame.encounterFramingReason += " / close threat";
    }
    AppendMode(mode, "Encounter Framing");
}

void RailCameraDirector::ApplyAimFocusStabilization(
    CourseCameraKey& rig,
    const RailCameraDirectorFrameInput& input,
    std::string& mode,
    RailCameraDirectorFrame& frame) {
    const float blend = UpdateAimFocusBlend(input);
    const int maxLocks = (std::max)(1, input.maxLockCount);
    const float tokenRatio = (std::clamp)(
        static_cast<float>((std::max)(0, input.lockTokenCount)) / static_cast<float>(maxLocks),
        0.0f,
        1.0f);
    const float reticleSpeed = Length2(input.reticleVelocity);
    const float velocityFocus = 1.0f - (std::clamp)(
        reticleSpeed / (std::max)(1.0f, aimFocusSettings_.maxReticleVelocityForFullFocus),
        0.0f,
        1.0f);
    const float lockIntent = input.lockHeld ? 1.0f : (input.lockTokenCount > 0 ? 0.62f : 0.0f);
    const float strength = (std::clamp)(
        blend * (0.60f + tokenRatio * 0.30f + velocityFocus * 0.10f) * lockIntent,
        0.0f,
        1.0f);

    if (strength <= 0.001f) {
        frame.aimFocusBlend = blend;
        frame.aimFocusStrength = 0.0f;
        frame.lockCameraStabilized = false;
        return;
    }

    AppendMode(mode, "Aim Focus");
    const float fovOffset = Degrees(
        aimFocusSettings_.fovOffsetDeg * strength +
        aimFocusSettings_.maxLockFovOffsetDeg * strength * tokenRatio);
    rig.fovY = (std::clamp)(rig.fovY + fovOffset, Degrees(38.0f), Degrees(72.0f));
    rig.roll *= 1.0f - (std::clamp)(aimFocusSettings_.rollSuppression * strength, 0.0f, 0.96f);
    rig.lateralOffset *= 1.0f - (std::clamp)(aimFocusSettings_.lateralSuppression * strength, 0.0f, 0.70f);
    rig.lookAheadDistance += aimFocusSettings_.lookAheadBoost * strength;
    rig.backDistance += aimFocusSettings_.backDistanceBoost * strength;
    frame.shakeAmount *= 1.0f - (std::clamp)(aimFocusSettings_.shakeSuppression * strength, 0.0f, 0.95f);
    frame.aimFocusBlend = blend;
    frame.aimFocusStrength = strength;
    frame.lockCameraStabilized = true;
}

void RailCameraDirector::UpdateLookAtTarget(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    const RailPathSample& cameraSample) {
    frame.baseTarget = frame.target;
    frame.threatCenter = frame.target;
    frame.lookAtBlend = 0.0f;
    frame.lookAtWeight = 0.0f;
    frame.lookAtCandidateCount = 0;
    frame.lookAtPolicy = RailCameraLookAtPolicy::RailLookAhead;
    frame.lookAtReason = "rail look-ahead";

    const bool canUseRuntime =
        lookAtSettings_.enabled &&
        input.spawnRuntime != nullptr &&
        input.railPath != nullptr &&
        input.railPath->Length() > 0.0f;
    if (!canUseRuntime) {
        lookAtBlend_ = 0.0f;
        smoothedLookAtTarget_ = frame.target;
        hasSmoothedLookAtTarget_ = true;
        return;
    }

    Vector3 weightedCenter{};
    float totalWeight = 0.0f;
    RailCameraLookAtPolicy bestPolicy = RailCameraLookAtPolicy::RailLookAhead;
    std::string bestReason = "rail look-ahead";
    float bestPriority = 0.0f;

    const auto addCandidate = [&](const Vector3& point, float weight, RailCameraLookAtPolicy policy, const char* reason) {
        if (weight <= 0.0f) {
            return;
        }
        weightedCenter = Add(weightedCenter, Scale(point, weight));
        totalWeight += weight;
        ++frame.lookAtCandidateCount;
        if (weight > bestPriority) {
            bestPriority = weight;
            bestPolicy = policy;
            bestReason = reason;
        }
    };

    const auto actorInRange = [&](float actorDistance) {
        const float forward = actorDistance - input.distance;
        return forward >= lookAtSettings_.minForwardDistance &&
            forward <= lookAtSettings_.maxForwardDistance;
    };

    if (input.lockTokens != nullptr && input.lockTokens->size() > 0) {
        int tokenIndex = 0;
        for (const RailLockToken& token : *input.lockTokens) {
            const float tokenWeight = lookAtSettings_.lockTokenWeight + static_cast<float>(tokenIndex) * 0.12f;
            bool resolved = false;
            if (token.target.kind == RailLockTargetKind::Enemy) {
                for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
                    if (enemy.actorId != token.target.actorId) {
                        continue;
                    }
                    const float distance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
                    if (actorInRange(distance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                enemy.desc.spawnDistance,
                                enemy.desc.distanceOffset,
                                enemy.desc.lateralOffset,
                                enemy.desc.verticalOffset),
                            tokenWeight * (RoleLooksBoss(enemy.desc.role) ? 1.35f : 1.0f),
                            RoleLooksBoss(enemy.desc.role) ? RailCameraLookAtPolicy::BossThreat : RailCameraLookAtPolicy::LockToken,
                            RoleLooksBoss(enemy.desc.role) ? "locked boss token" : "locked target token");
                    }
                    resolved = true;
                    break;
                }
            } else {
                for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
                    if (obstacle.actorId != token.target.actorId) {
                        continue;
                    }
                    const float distance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
                    if (actorInRange(distance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                obstacle.desc.spawnDistance,
                                obstacle.desc.distanceOffset,
                                obstacle.desc.lateralOffset,
                                obstacle.desc.verticalOffset),
                            tokenWeight * 0.84f,
                            RailCameraLookAtPolicy::LockToken,
                            "locked obstacle token");
                    }
                    resolved = true;
                    break;
                }
            }
            if (resolved) {
                ++tokenIndex;
            }
        }
    }

    if (totalWeight <= 0.001f) {
        for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
            const float distance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
            if (!actorInRange(distance)) {
                continue;
            }
            const bool boss = RoleLooksBoss(enemy.desc.role);
            const float distanceWeight = 1.0f -
                (std::clamp)((distance - input.distance) / (std::max)(1.0f, lookAtSettings_.maxForwardDistance), 0.0f, 1.0f);
            const float weight = (boss ? lookAtSettings_.bossWeight : lookAtSettings_.enemyWeight) *
                (0.46f + distanceWeight * 0.54f) *
                (enemy.age < 0.6f ? 1.20f : 1.0f);
            addCandidate(
                ResolveRailLocal(
                    *input.railPath,
                    enemy.desc.spawnDistance,
                    enemy.desc.distanceOffset,
                    enemy.desc.lateralOffset,
                    enemy.desc.verticalOffset),
                weight,
                boss ? RailCameraLookAtPolicy::BossThreat : RailCameraLookAtPolicy::ThreatCenter,
                boss ? "boss threat center" : "enemy threat center");
        }
    }

    if (totalWeight <= 0.001f) {
        for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
            if (!obstacle.desc.breakable) {
                continue;
            }
            const float distance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
            if (!actorInRange(distance)) {
                continue;
            }
            addCandidate(
                ResolveRailLocal(
                    *input.railPath,
                    obstacle.desc.spawnDistance,
                    obstacle.desc.distanceOffset,
                    obstacle.desc.lateralOffset,
                    obstacle.desc.verticalOffset),
                lookAtSettings_.obstacleWeight,
                RailCameraLookAtPolicy::Obstacle,
                "obstacle look-at");
        }
    }

    const bool hasThreat = totalWeight > 0.001f;
    const float dt = (std::max)(0.0f, input.deltaTime);
    const float targetBlend = hasThreat ? 1.0f : 0.0f;
    const float rate = hasThreat ? lookAtSettings_.blendRate : lookAtSettings_.releaseBlendRate;
    const float blendT = 1.0f - std::exp(-dt * (std::max)(0.0f, rate));
    lookAtBlend_ = Lerp(lookAtBlend_, targetBlend, (std::clamp)(blendT, 0.0f, 1.0f));

    Vector3 desiredTarget = frame.baseTarget;
    if (hasThreat) {
        frame.threatCenter = Scale(weightedCenter, 1.0f / totalWeight);
        const Vector3 offset = Subtract(frame.threatCenter, frame.baseTarget);
        const float offsetLength = Length(offset);
        const float maxOffset = (std::max)(0.0f, lookAtSettings_.maxTargetOffset);
        const Vector3 clampedThreat =
            offsetLength > maxOffset && offsetLength > 0.0001f
                ? Add(frame.baseTarget, Scale(offset, maxOffset / offsetLength))
                : frame.threatCenter;
        desiredTarget = LerpVector3(
            frame.baseTarget,
            clampedThreat,
            1.0f - (std::clamp)(lookAtSettings_.centerRetention, 0.0f, 1.0f));
        frame.lookAtPolicy = bestPolicy;
        frame.lookAtReason = bestReason;
        frame.lookAtWeight = totalWeight;
    }

    if (!hasSmoothedLookAtTarget_) {
        smoothedLookAtTarget_ = desiredTarget;
        hasSmoothedLookAtTarget_ = true;
    } else {
        smoothedLookAtTarget_ = LerpVector3(
            smoothedLookAtTarget_,
            desiredTarget,
            (std::clamp)(blendT, 0.0f, 1.0f));
    }

    frame.lookAtBlend = lookAtBlend_;
    frame.target = LerpVector3(
        frame.baseTarget,
        smoothedLookAtTarget_,
        (std::clamp)(lookAtBlend_, 0.0f, 1.0f));
    frame.forward = NormalizeOr(Subtract(frame.target, frame.position), cameraSample.tangent);
}

void RailCameraDirector::ApplyCompositionSafety(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    const RailPathSample& cameraSample,
    std::string& mode) {
    frame.compositionSafetyBlend = compositionSafetyBlend_;
    frame.compositionRisk = 0.0f;
    frame.compositionFovOffsetDeg = 0.0f;
    frame.compositionCorrection = {};
    frame.compositionCandidateCount = 0;
    frame.compositionOutOfAimableCount = 0;
    frame.compositionOutOfReadabilityCount = 0;
    frame.compositionSafeForAiming = true;
    frame.compositionReason = "no composition pressure";

    const bool canEvaluate =
        compositionSafetySettings_.enabled &&
        input.spawnRuntime != nullptr &&
        input.railPath != nullptr &&
        input.railPath->Length() > 0.0f;
    const float dt = (std::max)(0.0f, input.deltaTime);
    if (!canEvaluate) {
        const float releaseT =
            1.0f - std::exp(-dt * (std::max)(0.0f, compositionSafetySettings_.blendOutRate));
        compositionSafetyBlend_ = Lerp(compositionSafetyBlend_, 0.0f, (std::clamp)(releaseT, 0.0f, 1.0f));
        smoothedCompositionCorrection_ =
            LerpVector2(smoothedCompositionCorrection_, {}, (std::clamp)(releaseT, 0.0f, 1.0f));
        frame.compositionSafetyBlend = compositionSafetyBlend_;
        return;
    }

    struct CompositionCandidate {
        Vector3 point{};
        float weight = 0.0f;
        const char* reason = "candidate";
    };

    std::vector<CompositionCandidate> candidates;
    candidates.reserve(input.spawnRuntime->Enemies().size() + input.spawnRuntime->Obstacles().size());
    const auto inForwardRange = [&](float actorDistance) {
        const float forward = actorDistance - input.distance;
        return forward >= compositionSafetySettings_.minForwardDistance &&
            forward <= compositionSafetySettings_.maxForwardDistance;
    };
    const auto addCandidate = [&](const Vector3& point, float weight, const char* reason) {
        if (weight <= 0.0f) {
            return;
        }
        candidates.push_back({point, weight, reason});
    };

    if (input.lockTokens != nullptr) {
        for (const RailLockToken& token : *input.lockTokens) {
            if (token.target.kind == RailLockTargetKind::Enemy) {
                for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
                    if (enemy.actorId != token.target.actorId) {
                        continue;
                    }
                    const float actorDistance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
                    if (inForwardRange(actorDistance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                enemy.desc.spawnDistance,
                                enemy.desc.distanceOffset,
                                enemy.desc.lateralOffset,
                                enemy.desc.verticalOffset),
                            compositionSafetySettings_.lockTokenWeight *
                                (RoleLooksBoss(enemy.desc.role) ? 1.35f : 1.0f),
                            RoleLooksBoss(enemy.desc.role) ? "locked boss outside aimable zone" : "locked target outside aimable zone");
                    }
                    break;
                }
            } else {
                for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
                    if (obstacle.actorId != token.target.actorId) {
                        continue;
                    }
                    const float actorDistance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
                    if (inForwardRange(actorDistance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                obstacle.desc.spawnDistance,
                                obstacle.desc.distanceOffset,
                                obstacle.desc.lateralOffset,
                                obstacle.desc.verticalOffset),
                            compositionSafetySettings_.lockTokenWeight * 0.72f,
                            "locked obstacle outside aimable zone");
                    }
                    break;
                }
            }
        }
    }

    for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
        const float actorDistance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
        if (!inForwardRange(actorDistance)) {
            continue;
        }
        const bool boss = RoleLooksBoss(enemy.desc.role);
        const float distanceRatio = (std::clamp)(
            (actorDistance - input.distance) / (std::max)(1.0f, compositionSafetySettings_.maxForwardDistance),
            0.0f,
            1.0f);
        addCandidate(
            ResolveRailLocal(
                *input.railPath,
                enemy.desc.spawnDistance,
                enemy.desc.distanceOffset,
                enemy.desc.lateralOffset,
                enemy.desc.verticalOffset),
            (boss ? compositionSafetySettings_.bossWeight : compositionSafetySettings_.enemyWeight) *
                (1.0f - distanceRatio * 0.42f),
            boss ? "boss outside aimable zone" : "enemy outside aimable zone");
    }

    for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
        if (!obstacle.desc.breakable) {
            continue;
        }
        const float actorDistance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
        if (!inForwardRange(actorDistance)) {
            continue;
        }
        addCandidate(
            ResolveRailLocal(
                *input.railPath,
                obstacle.desc.spawnDistance,
                obstacle.desc.distanceOffset,
                obstacle.desc.lateralOffset,
                obstacle.desc.verticalOffset),
            compositionSafetySettings_.obstacleWeight,
            "obstacle outside aimable zone");
    }

    const float aspectRatio = input.viewportHeight > 0
        ? static_cast<float>((std::max)(1u, input.viewportWidth)) / static_cast<float>(input.viewportHeight)
        : 16.0f / 9.0f;
    const float aimX = (std::clamp)(compositionSafetySettings_.aimableZoneWidth, 0.10f, 1.0f);
    const float aimY = (std::clamp)(compositionSafetySettings_.aimableZoneHeight, 0.10f, 1.0f);
    const float readabilityX = (std::clamp)(
        compositionSafetySettings_.readabilityZoneWidth,
        (std::min)(1.0f, aimX + 0.02f),
        1.0f);
    const float readabilityY = (std::clamp)(
        compositionSafetySettings_.readabilityZoneHeight,
        (std::min)(1.0f, aimY + 0.02f),
        1.0f);

    Vector2 weightedCorrection{};
    float weightedRisk = 0.0f;
    float totalWeight = 0.0f;
    const char* strongestReason = "composition pressure";
    float strongestRisk = 0.0f;

    for (const CompositionCandidate& candidate : candidates) {
        const RailCameraCompositionProjection projection =
            ProjectCompositionPoint(frame, candidate.point, aspectRatio);
        if (!projection.inFront) {
            continue;
        }

        ++frame.compositionCandidateCount;
        const float absX = std::abs(projection.normalized.x);
        const float absY = std::abs(projection.normalized.y);
        const float dx =
            projection.normalized.x > aimX ? projection.normalized.x - aimX :
            projection.normalized.x < -aimX ? projection.normalized.x + aimX :
            0.0f;
        const float dy =
            projection.normalized.y > aimY ? projection.normalized.y - aimY :
            projection.normalized.y < -aimY ? projection.normalized.y + aimY :
            0.0f;
        const bool outOfAimable = dx != 0.0f || dy != 0.0f;
        const bool outOfReadability = absX > readabilityX || absY > readabilityY;
        if (outOfAimable) {
            ++frame.compositionOutOfAimableCount;
        }
        if (outOfReadability) {
            ++frame.compositionOutOfReadabilityCount;
        }

        const float rawRisk = Length2({dx, dy}) + (outOfReadability ? 0.32f : 0.0f);
        const float weighted = (std::max)(0.0f, candidate.weight);
        weightedCorrection = Add2(weightedCorrection, Scale2({dx, dy}, weighted));
        weightedRisk += rawRisk * weighted;
        totalWeight += weighted;
        if (rawRisk * weighted > strongestRisk) {
            strongestRisk = rawRisk * weighted;
            strongestReason = candidate.reason;
        }
    }

    if (frame.compositionCandidateCount <= 0 || totalWeight <= 0.001f) {
        const float releaseT =
            1.0f - std::exp(-dt * (std::max)(0.0f, compositionSafetySettings_.blendOutRate));
        compositionSafetyBlend_ = Lerp(compositionSafetyBlend_, 0.0f, (std::clamp)(releaseT, 0.0f, 1.0f));
        smoothedCompositionCorrection_ =
            LerpVector2(smoothedCompositionCorrection_, {}, (std::clamp)(releaseT, 0.0f, 1.0f));
        frame.compositionSafetyBlend = compositionSafetyBlend_;
        frame.compositionReason = "no visible composition candidates";
        return;
    }

    const float risk = (std::clamp)(weightedRisk / totalWeight, 0.0f, 1.5f);
    const Vector2 desiredCorrection = Scale2(
        weightedCorrection,
        compositionSafetySettings_.correctionGain / (std::max)(0.001f, totalWeight));
    const float targetBlend = risk > 0.015f ? 1.0f : 0.0f;
    const float blendRate = targetBlend > compositionSafetyBlend_
        ? compositionSafetySettings_.blendInRate
        : compositionSafetySettings_.blendOutRate;
    const float blendT = 1.0f - std::exp(-dt * (std::max)(0.0f, blendRate));
    compositionSafetyBlend_ = Lerp(compositionSafetyBlend_, targetBlend, (std::clamp)(blendT, 0.0f, 1.0f));
    smoothedCompositionCorrection_ = LerpVector2(
        smoothedCompositionCorrection_,
        desiredCorrection,
        (std::clamp)(blendT, 0.0f, 1.0f));

    const float correctionLength = Length2(smoothedCompositionCorrection_);
    if (correctionLength > 1.0f) {
        smoothedCompositionCorrection_ = Scale2(smoothedCompositionCorrection_, 1.0f / correctionLength);
    }

    const Vector3 forward = NormalizeOr(Subtract(frame.target, frame.position), cameraSample.tangent);
    const Vector3 right = NormalizeOr(Cross(frame.up, forward), cameraSample.right);
    const Vector3 up = NormalizeOr(Cross(forward, right), cameraSample.up);
    const float worldScale = (std::max)(0.0f, compositionSafetySettings_.maxTargetCorrection) *
        (std::clamp)(compositionSafetyBlend_, 0.0f, 1.0f);
    const Vector3 worldCorrection = Add(
        Scale(right, smoothedCompositionCorrection_.x * worldScale),
        Scale(up, smoothedCompositionCorrection_.y * worldScale));

    frame.target = Add(frame.target, worldCorrection);
    frame.forward = NormalizeOr(Subtract(frame.target, frame.position), cameraSample.tangent);
    const float fovRisk = (std::clamp)(risk * 1.35f, 0.0f, 1.0f);
    const float fovOffsetDeg =
        compositionSafetySettings_.fovExpandDeg * fovRisk * (std::clamp)(compositionSafetyBlend_, 0.0f, 1.0f);
    frame.rig.fovY = (std::clamp)(
        frame.rig.fovY + Degrees(fovOffsetDeg),
        Degrees(34.0f),
        Degrees((std::max)(40.0f, compositionSafetySettings_.maxFovDeg)));

    frame.compositionSafetyBlend = compositionSafetyBlend_;
    frame.compositionRisk = risk;
    frame.compositionFovOffsetDeg = fovOffsetDeg;
    frame.compositionCorrection = smoothedCompositionCorrection_;
    frame.compositionSafeForAiming =
        frame.compositionOutOfReadabilityCount == 0 &&
        risk < compositionSafetySettings_.fireBlockRisk;
    frame.compositionReason = strongestReason;
    if (compositionSafetyBlend_ > 0.04f && risk > 0.015f) {
        AppendMode(mode, "Composition Safety");
    }
}

void RailCameraDirector::ApplyLineOfSightSafety(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    std::string& mode) {
    frame.lineOfSightFovOffsetDeg = 0.0f;
    frame.lineOfSightCandidateCount = 0;
    frame.lineOfSightBlockedCount = 0;
    frame.lineOfSightOccluderActorId = 0;
    frame.lineOfSightSafeForAiming = true;
    frame.lineOfSightReason = "clear";

    if (!lineOfSightSettings_.enabled ||
        input.spawnRuntime == nullptr ||
        input.railPath == nullptr ||
        input.railPath->Length() <= 0.0f) {
        frame.lineOfSightReason = lineOfSightSettings_.enabled ? "runtime unavailable" : "line-of-sight disabled";
        return;
    }

    struct LineOfSightCandidate {
        Vector3 point{};
        float weight = 0.0f;
        uint32_t ignoredObstacleActorId = 0;
        const char* reason = "target occluded";
    };

    std::vector<LineOfSightCandidate> candidates;
    candidates.reserve(input.spawnRuntime->Enemies().size() + input.spawnRuntime->Obstacles().size() + 2);
    const auto inForwardRange = [&](float actorDistance) {
        const float forward = actorDistance - input.distance;
        return forward >= lineOfSightSettings_.minForwardDistance &&
            forward <= lineOfSightSettings_.maxForwardDistance;
    };
    const auto addCandidate = [&](const Vector3& point, float weight, uint32_t ignoredObstacleActorId, const char* reason) {
        if (weight <= 0.0f) {
            return;
        }
        candidates.push_back({point, weight, ignoredObstacleActorId, reason});
    };

    if (input.lockTokens != nullptr) {
        for (const RailLockToken& token : *input.lockTokens) {
            if (token.target.kind == RailLockTargetKind::Enemy) {
                for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
                    if (enemy.actorId != token.target.actorId) {
                        continue;
                    }
                    const float actorDistance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
                    if (inForwardRange(actorDistance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                enemy.desc.spawnDistance,
                                enemy.desc.distanceOffset,
                                enemy.desc.lateralOffset,
                                enemy.desc.verticalOffset),
                            RoleLooksBoss(enemy.desc.role) ? 5.0f : 4.0f,
                            0,
                            RoleLooksBoss(enemy.desc.role) ? "locked boss occluded" : "locked target occluded");
                    }
                    break;
                }
            } else {
                for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
                    if (obstacle.actorId != token.target.actorId) {
                        continue;
                    }
                    const float actorDistance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
                    if (inForwardRange(actorDistance)) {
                        addCandidate(
                            ResolveRailLocal(
                                *input.railPath,
                                obstacle.desc.spawnDistance,
                                obstacle.desc.distanceOffset,
                                obstacle.desc.lateralOffset,
                                obstacle.desc.verticalOffset),
                            3.2f,
                            obstacle.actorId,
                            "locked obstacle occluded");
                    }
                    break;
                }
            }
        }
    }

    for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
        const float actorDistance = ActorDistance(enemy.desc.spawnDistance, enemy.desc.distanceOffset);
        if (!inForwardRange(actorDistance)) {
            continue;
        }
        const bool boss = RoleLooksBoss(enemy.desc.role);
        addCandidate(
            ResolveRailLocal(
                *input.railPath,
                enemy.desc.spawnDistance,
                enemy.desc.distanceOffset,
                enemy.desc.lateralOffset,
                enemy.desc.verticalOffset),
            boss ? 3.2f : 1.0f,
            0,
            boss ? "boss threat occluded" : "enemy threat occluded");
    }

    for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
        if (!obstacle.desc.breakable) {
            continue;
        }
        const float actorDistance = ActorDistance(obstacle.desc.spawnDistance, obstacle.desc.distanceOffset);
        if (!inForwardRange(actorDistance)) {
            continue;
        }
        addCandidate(
            ResolveRailLocal(
                *input.railPath,
                obstacle.desc.spawnDistance,
                obstacle.desc.distanceOffset,
                obstacle.desc.lateralOffset,
                obstacle.desc.verticalOffset),
            0.42f,
            obstacle.actorId,
            "obstacle target occluded");
    }

    if (frame.lookAtWeight > 0.001f) {
        addCandidate(frame.threatCenter, (std::min)(4.0f, frame.lookAtWeight), 0, "threat center occluded");
    }

    float weightedBlocked = 0.0f;
    float totalWeight = 0.0f;
    float strongestBlocked = 0.0f;
    const char* strongestReason = "line-of-sight blocked";
    uint32_t strongestOccluder = 0;
    const float padding = (std::max)(0.0f, lineOfSightSettings_.obstaclePadding);

    for (const LineOfSightCandidate& candidate : candidates) {
        ++frame.lineOfSightCandidateCount;
        totalWeight += candidate.weight;
        uint32_t occluder = 0;
        float blockT = 0.0f;
        if (!ResolveObstacleLineOfSightBlock(
                *input.spawnRuntime,
                *input.railPath,
                frame.position,
                candidate.point,
                candidate.ignoredObstacleActorId,
                padding,
                occluder,
                blockT)) {
            continue;
        }

        ++frame.lineOfSightBlockedCount;
        const float blockWeight = candidate.weight * (1.0f + (1.0f - blockT) * 0.35f);
        weightedBlocked += blockWeight;
        if (blockWeight > strongestBlocked) {
            strongestBlocked = blockWeight;
            strongestReason = candidate.reason;
            strongestOccluder = occluder;
        }
    }

    if (frame.lineOfSightCandidateCount <= 0 || totalWeight <= 0.001f) {
        frame.lineOfSightReason = "no line-of-sight candidates";
        return;
    }
    if (frame.lineOfSightBlockedCount <= 0) {
        frame.lineOfSightReason = "clear";
        return;
    }

    const float blockedRatio = (std::clamp)(weightedBlocked / (std::max)(0.001f, totalWeight), 0.0f, 1.0f);
    frame.lineOfSightSafeForAiming = false;
    frame.lineOfSightOccluderActorId = strongestOccluder;
    frame.lineOfSightReason = strongestReason;

    if (lineOfSightSettings_.preferBaseTargetWhenOccluded) {
        const float release = (std::clamp)(lineOfSightSettings_.targetReleaseStrength * blockedRatio, 0.0f, 0.95f);
        frame.target = LerpVector3(frame.target, frame.baseTarget, release);
        frame.forward = NormalizeOr(Subtract(frame.target, frame.position), frame.forward);
    }

    const float fovOffsetDeg = lineOfSightSettings_.fovExpandDeg * blockedRatio;
    frame.rig.fovY = (std::clamp)(
        frame.rig.fovY + Degrees(fovOffsetDeg),
        Degrees(34.0f),
        Degrees((std::max)(40.0f, lineOfSightSettings_.maxFovDeg)));
    frame.lineOfSightFovOffsetDeg = fovOffsetDeg;
    AppendMode(mode, "Line-of-Sight Safety");
}

void RailCameraDirector::ApplyCameraCollisionProtection(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    const RailPathSample& cameraSample,
    std::string& mode) {
    frame.cameraCollisionPushDistance = 0.0f;
    frame.cameraCollisionClosestDistance = 9999.0f;
    frame.cameraCollisionFovOffsetDeg = 0.0f;
    frame.cameraCollisionObstacleCount = 0;
    frame.cameraCollisionObstacleActorId = 0;
    frame.cameraCollisionSafe = true;
    frame.cameraCollisionReason = "clear";

    if (!collisionProtectionSettings_.enabled ||
        input.spawnRuntime == nullptr ||
        input.railPath == nullptr ||
        input.railPath->Length() <= 0.0f) {
        frame.cameraCollisionReason =
            collisionProtectionSettings_.enabled ? "runtime unavailable" : "camera collision disabled";
        return;
    }

    const float padding = (std::max)(0.0f, collisionProtectionSettings_.obstaclePadding);
    const float requiredClearance = (std::max)(
        collisionProtectionSettings_.minClearance,
        (std::max)(0.01f, input.nearClipDistance) * collisionProtectionSettings_.nearClipClearanceMultiplier);
    const Vector3 fallbackDirection = NormalizeOr(
        Add(Scale(frame.forward, -0.55f), Scale(cameraSample.up, 0.45f)),
        cameraSample.up);

    Vector3 accumulatedPush{};
    float largestDeficit = 0.0f;
    float closestDistance = 9999.0f;
    uint32_t closestObstacle = 0;
    int unsafeCount = 0;

    for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
        ++frame.cameraCollisionObstacleCount;
        const Vector3 center = ResolveRailLocal(
            *input.railPath,
            obstacle.desc.spawnDistance,
            obstacle.desc.distanceOffset,
            obstacle.desc.lateralOffset,
            obstacle.desc.verticalOffset);
        const Vector3 extent{
            obstacle.desc.halfExtents.x + padding,
            obstacle.desc.halfExtents.y + padding,
            obstacle.desc.halfExtents.z + padding,
        };
        const Vector3 minBounds{center.x - extent.x, center.y - extent.y, center.z - extent.z};
        const Vector3 maxBounds{center.x + extent.x, center.y + extent.y, center.z + extent.z};
        const Vector3 closest = ClosestPointOnAabb(frame.position, minBounds, maxBounds);
        const float distance = Length(Subtract(frame.position, closest));
        if (distance < closestDistance) {
            closestDistance = distance;
            closestObstacle = obstacle.actorId;
        }
        if (distance >= requiredClearance) {
            continue;
        }

        ++unsafeCount;
        const float deficit = requiredClearance - distance;
        largestDeficit = (std::max)(largestDeficit, deficit);
        const Vector3 direction = ResolveAabbPushDirection(
            frame.position,
            center,
            minBounds,
            maxBounds,
            fallbackDirection);
        accumulatedPush = Add(accumulatedPush, Scale(direction, deficit));
    }

    frame.cameraCollisionClosestDistance = closestDistance;
    frame.cameraCollisionObstacleActorId = closestObstacle;
    if (unsafeCount <= 0) {
        return;
    }

    const float pushLength = Length(accumulatedPush);
    if (pushLength <= 0.0001f) {
        frame.cameraCollisionReason = "unsafe but no push direction";
        frame.cameraCollisionSafe = false;
        return;
    }

    const float maxPush = (std::max)(0.0f, collisionProtectionSettings_.maxPushDistance);
    const float pushScale = pushLength > maxPush && pushLength > 0.0001f ? maxPush / pushLength : 1.0f;
    const Vector3 push = Scale(accumulatedPush, pushScale);
    const float appliedPush = Length(push);
    frame.position = Add(frame.position, push);
    frame.target = Add(
        frame.target,
        Scale(push, (std::clamp)(collisionProtectionSettings_.targetCompensation, 0.0f, 1.0f)));
    frame.forward = NormalizeOr(Subtract(frame.target, frame.position), cameraSample.tangent);

    const float risk = (std::clamp)(largestDeficit / (std::max)(0.001f, requiredClearance), 0.0f, 1.0f);
    const float fovOffsetDeg = collisionProtectionSettings_.fovExpandDeg * risk;
    frame.rig.fovY = (std::clamp)(
        frame.rig.fovY + Degrees(fovOffsetDeg),
        Degrees(34.0f),
        Degrees((std::max)(40.0f, collisionProtectionSettings_.maxFovDeg)));
    frame.cameraCollisionPushDistance = appliedPush;
    frame.cameraCollisionFovOffsetDeg = fovOffsetDeg;
    frame.cameraCollisionSafe = false;
    frame.cameraCollisionReason = "camera near obstacle";
    AppendMode(mode, "Camera Collision");
}

void RailCameraDirector::BeginSegmentTransitionIfNeeded(const RailCameraDirectorFrameInput& input) {
    const std::string nextSignature = SectionSignature(input.section);
    if (currentSectionSignature_.empty()) {
        currentSectionSignature_ = nextSignature;
        previousSectionSignature_ = nextSignature;
        return;
    }
    if (nextSignature == currentSectionSignature_) {
        return;
    }

    previousSectionSignature_ = currentSectionSignature_;
    currentSectionSignature_ = nextSignature;
    segmentTransitionElapsed_ = 0.0f;

    float duration = segmentTransitionSettings_.duration;
    const std::string key = input.section != nullptr ? input.section->name + " " + input.section->category : "";
    if (ContainsInsensitive(key, "boss")) {
        duration = segmentTransitionSettings_.bossDuration;
    } else if (ContainsInsensitive(key, "tunnel") || ContainsInsensitive(key, "obstacle")) {
        duration = segmentTransitionSettings_.tunnelDuration;
    } else if (ContainsInsensitive(key, "high speed") || ContainsInsensitive(key, "escape")) {
        duration = segmentTransitionSettings_.highSpeedDuration;
    }
    segmentTransitionDuration_ = (std::max)(segmentTransitionSettings_.minDuration, duration);

    if (lastFrame_.mode.empty()) {
        hasSegmentTransitionStartFrame_ = false;
    } else {
        segmentTransitionStartFrame_ = previousTransitionFrame_;
        segmentTransitionCameraAnchor_ = previousTransitionCameraAnchor_;
        segmentTransitionTargetAnchor_ = previousTransitionTargetAnchor_;
        hasSegmentTransitionStartFrame_ = true;
    }
}

void RailCameraDirector::ApplySegmentTransitionPolish(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input,
    std::string& mode) {
    frame.previousSectionName = previousSectionSignature_.empty() ? "-" : previousSectionSignature_;
    frame.currentSectionName = SectionDisplayName(input.section);
    frame.segmentTransitionActive = false;
    frame.segmentTransitionBlend = 1.0f;
    frame.segmentTransitionRemaining = 0.0f;
    frame.segmentTransitionReason = "stable section";

    if (!segmentTransitionSettings_.enabled ||
        segmentTransitionDuration_ <= 0.0f ||
        segmentTransitionElapsed_ >= segmentTransitionDuration_ ||
        !hasSegmentTransitionStartFrame_) {
        return;
    }

    const float dt = (std::max)(0.0f, input.deltaTime);
    segmentTransitionElapsed_ = (std::min)(segmentTransitionDuration_, segmentTransitionElapsed_ + dt);
    const float linearT = segmentTransitionDuration_ > 0.0001f
        ? segmentTransitionElapsed_ / segmentTransitionDuration_
        : 1.0f;
    const float blend = SmoothStep(linearT);
    const float positionBlendLimit = (std::max)(0.0f, segmentTransitionSettings_.maxPositionBlendDistance);
    const float targetBlendLimit = (std::max)(0.0f, segmentTransitionSettings_.maxTargetBlendDistance);

    const auto blendVectorWithLimit = [](const Vector3& from, const Vector3& to, float t, float maxDistance) {
        const Vector3 delta = Subtract(to, from);
        const float distance = Length(delta);
        if (maxDistance > 0.0f && distance > maxDistance && distance > 0.0001f) {
            const Vector3 limited = Add(from, Scale(delta, maxDistance / distance));
            return LerpVector3(limited, to, t);
        }
        return LerpVector3(from, to, t);
    };

    // Blend compositions in moving rail frames. Blending from a frozen world
    // position briefly stops the camera, then accelerates it to catch the cart.
    const auto transport = [](const Vector3& point, const RailPathSample& from, const RailPathSample& to) {
        const Vector3 offset = Subtract(point, from.position);
        return Add(to.position, Add(Add(
            Scale(to.right, Dot(offset, from.right)),
            Scale(to.up, Dot(offset, from.up))),
            Scale(to.tangent, Dot(offset, from.tangent))));
    };
    const auto cameraAnchor = input.railPath->Evaluate(input.distance);
    const auto targetAnchor = input.railPath->Evaluate(input.distance + frame.rig.lookAheadDistance);
    frame.position = blendVectorWithLimit(
        transport(segmentTransitionStartFrame_.position, segmentTransitionCameraAnchor_, cameraAnchor),
        frame.position,
        blend,
        positionBlendLimit);
    frame.target = blendVectorWithLimit(
        transport(segmentTransitionStartFrame_.target, segmentTransitionTargetAnchor_, targetAnchor),
        frame.target,
        blend,
        targetBlendLimit);
    frame.rig.fovY = Lerp(
        segmentTransitionStartFrame_.rig.fovY,
        frame.rig.fovY,
        Lerp(blend, 1.0f, 1.0f - (std::clamp)(segmentTransitionSettings_.fovBlendStrength, 0.0f, 1.0f)));
    frame.rig.roll = Lerp(
        segmentTransitionStartFrame_.rig.roll,
        frame.rig.roll,
        Lerp(blend, 1.0f, 1.0f - (std::clamp)(segmentTransitionSettings_.rollBlendStrength, 0.0f, 1.0f)));
    frame.shakeAmount *= 1.0f - (1.0f - blend) *
        (std::clamp)(segmentTransitionSettings_.shakeDampen, 0.0f, 1.0f);

    frame.segmentTransitionActive = segmentTransitionElapsed_ < segmentTransitionDuration_;
    frame.segmentTransitionBlend = blend;
    frame.segmentTransitionRemaining = (std::max)(0.0f, segmentTransitionDuration_ - segmentTransitionElapsed_);
    frame.segmentTransitionReason = "section transition polish";
    AppendMode(mode, "Segment Transition");
}

void RailCameraDirector::ApplySectionDirecting(
    CourseCameraKey& rig,
    const CourseSection* section,
    std::string& mode) const {
    if (section == nullptr) {
        return;
    }

    const std::string key = section->name + " " + section->category;
    if (Contains(key, "Tunnel") || Contains(key, "Obstacle")) {
        rig.backDistance *= 0.82f;
        rig.lookAheadDistance *= 0.78f;
        rig.fovY += Degrees(5.0f);
        mode = "Tunnel Compression";
    } else if (Contains(key, "Boss")) {
        rig.backDistance += 8.0f;
        rig.verticalOffset += 2.4f;
        rig.lookAheadDistance += 10.0f;
        rig.fovY -= Degrees(3.0f);
        mode = "Boss Stage";
    } else if (Contains(key, "Escape") || Contains(key, "High Speed")) {
        rig.backDistance *= 0.88f;
        rig.lookAheadDistance += 14.0f;
        rig.fovY += Degrees(7.0f);
        mode = "High Speed";
    } else if (Contains(key, "Setpiece") || Contains(key, "Falling")) {
        rig.verticalOffset += 2.0f;
        rig.lateralOffset += std::sin(directorTime_ * 0.8f) * 1.6f;
        rig.lookAheadDistance += 8.0f;
        mode = "Setpiece";
    }
}

void RailCameraDirector::ApplyCinematicShotDirecting(
    CourseCameraKey& rig,
    const CourseAsset* course,
    float distance,
    std::string_view preferredShotId,
    float preferredShotWeight,
    std::string& mode,
    RailCameraDirectorFrame& frame) const {
    if (course == nullptr) {
        return;
    }

    CourseCameraShotState shotState = course->EvaluateCinematicCameraShot(
        distance, preferredShotId);
    if (shotState.weight <= 0.0f && !preferredShotId.empty()) {
        shotState = course->EvaluateCinematicCameraShot(distance);
    }
    if (shotState.weight <= 0.0f) {
        return;
    }

    const CourseCinematicCameraShot& shot = shotState.shot;
    float w = (std::clamp)(shotState.weight, 0.0f, 1.0f);
    if (!preferredShotId.empty() && shot.id == preferredShotId) {
        w = (std::min)(w, (std::clamp)(preferredShotWeight, 0.0f, 1.0f));
    }
    frame.cinematicShotWeight = w;
    frame.cinematicShotId = shot.id.empty() ? "-" : shot.id;
    frame.cinematicShotPresetId = shotState.presetId.empty() ? "-" : shotState.presetId;
    frame.cinematicShotBlendAssetId = shotState.blendAssetId.empty() ? "-" : shotState.blendAssetId;
    frame.cinematicShotBlendCurve = shotState.blendCurve.empty() ? "-" : shotState.blendCurve;
    rig.backDistance += shot.backDistanceOffset * w;
    rig.verticalOffset += shot.verticalOffset * w;
    rig.lateralOffset += shot.lateralOffset * w;
    rig.lookAheadDistance += shot.lookAheadOffset * w;
    rig.lookUpOffset += shot.lookUpOffset * w;
    rig.lookForwardOffset += shot.lookForwardOffset * w;
    rig.fovY += shot.fovOffset * w;
    rig.roll += shot.rollOffset * w;
    frame.shakeAmount = (std::max)(frame.shakeAmount, shot.shakeAmount * w);

    if (!shot.mode.empty()) {
        mode = mode == "Default" ? shot.mode : mode + " + " + shot.mode;
    }
}

void RailCameraDirector::ApplyEventDirecting(CourseCameraKey& rig, float deltaTime, std::string& mode) {
    const float dt = (std::max)(0.0f, deltaTime);
    if (shakeTime_ > 0.0f) {
        shakeTime_ = (std::max)(0.0f, shakeTime_ - dt);
        if (shakeTime_ <= 0.0f) {
            shakeAmplitude_ = 0.0f;
        }
    }

    if (std::abs(fovKick_) > 0.0001f || std::abs(rollKick_) > 0.0001f) {
        mode = mode == "Default" ? "Event Accent" : mode + " + Event";
    }

    rig.fovY = (std::clamp)(rig.fovY + fovKick_, Degrees(36.0f), Degrees(78.0f));
    rig.roll += rollKick_;

    const float decay = std::exp(-dt * 3.0f);
    fovKick_ *= decay;
    rollKick_ *= decay;
}

void RailCameraDirector::UpdateComfortMetrics(
    RailCameraDirectorFrame& frame,
    const RailCameraDirectorFrameInput& input) {
    const float dt = (std::max)(0.0001f, input.deltaTime);
    frame.rollDeg = RadiansToDegrees(frame.rig.roll);
    if (!hasPreviousComfortFrame_) {
        frame.angularVelocityDeg = 0.0f;
        frame.angularAccelerationDeg = 0.0f;
        frame.fovChangeRateDeg = 0.0f;
        frame.linearSpeed = (std::max)(0.0f, frame.railSpeed);
        frame.stabilityScore = 1.0f;
        frame.stableForAiming = true;
        frame.hardTransition = false;
        frame.allowEnemyFire = true;
        frame.comfortReason = comfortSettings_.enabled ? "initial frame" : "comfort metrics disabled";
    } else {
        const float forwardDot = (std::clamp)(Dot(previousForward_, frame.forward), -1.0f, 1.0f);
        // acos(dot) loses precision near parallel vectors. At high frame rates
        // this quantizes slow camera motion into spurious acceleration spikes,
        // repeatedly restarting otherwise readable attack warnings.
        const float forwardSin = Length(Cross(previousForward_, frame.forward));
        frame.angularVelocityDeg = RadiansToDegrees(std::atan2(forwardSin, forwardDot)) / dt;
        frame.angularAccelerationDeg =
            std::abs(frame.angularVelocityDeg - previousAngularVelocityDeg_) / dt;
        frame.fovChangeRateDeg = std::abs(RadiansToDegrees(frame.fovY - previousFovY_)) / dt;
        frame.linearSpeed = Length(Subtract(frame.position, previousPosition_)) / dt;

        const float transitionGrace = frame.segmentTransitionActive
            ? (std::max)(1.0f, segmentTransitionSettings_.comfortGraceMultiplier)
            : 1.0f;
        const float stableAngularVelocity = comfortSettings_.stableAngularVelocityDeg * transitionGrace;
        const float stableAngularAcceleration = comfortSettings_.stableAngularAccelerationDeg * transitionGrace;
        const float stableFovChange = comfortSettings_.stableFovChangeRateDeg * transitionGrace;
        const float hardAngularVelocity = comfortSettings_.hardTransitionAngularVelocityDeg * transitionGrace;
        const float hardFovChange = comfortSettings_.hardTransitionFovChangeRateDeg * transitionGrace;
        const float hardRoll = comfortSettings_.hardTransitionRollDeg * transitionGrace;

        const float angularRatio = frame.angularVelocityDeg /
            (std::max)(1.0f, stableAngularVelocity);
        const float accelerationRatio = frame.angularAccelerationDeg /
            (std::max)(1.0f, stableAngularAcceleration);
        const float fovRatio = frame.fovChangeRateDeg /
            (std::max)(1.0f, stableFovChange);
        const float rollRatio = std::abs(frame.rollDeg) /
            (std::max)(1.0f, comfortSettings_.stableRollDeg);
        const float shakeRatio = frame.shakeAmount /
            (std::max)(0.01f, comfortSettings_.stableShakeAmount);
        const float worstRatio = (std::max)(
            (std::max)(angularRatio, accelerationRatio),
            (std::max)((std::max)(fovRatio, rollRatio), shakeRatio));
        frame.stabilityScore = (std::clamp)(1.0f / (std::max)(1.0f, worstRatio), 0.0f, 1.0f);

        frame.hardTransition =
            comfortSettings_.enabled &&
            (frame.angularVelocityDeg > hardAngularVelocity ||
                frame.fovChangeRateDeg > hardFovChange ||
                std::abs(frame.rollDeg) > hardRoll);
        frame.stableForAiming =
            !comfortSettings_.enabled ||
            (!frame.hardTransition &&
                frame.angularVelocityDeg <= stableAngularVelocity &&
                frame.angularAccelerationDeg <= stableAngularAcceleration &&
                frame.fovChangeRateDeg <= stableFovChange &&
                std::abs(frame.rollDeg) <= comfortSettings_.stableRollDeg &&
                frame.shakeAmount <= comfortSettings_.stableShakeAmount);

        // Setpieces and event accents still contain interactive encounters.
        // Their measured motion, framing and transition gates below determine
        // readiness; a mode label must not suppress an entire enemy lifetime.
        const bool modeBlocksFire =
            frame.modeKind == RailCameraDirectorMode::Cinematic;
        frame.allowEnemyFire = frame.stableForAiming && !frame.hardTransition && !modeBlocksFire;

        if (!comfortSettings_.enabled) {
            frame.comfortReason = "comfort metrics disabled";
        } else if (frame.hardTransition) {
            frame.comfortReason = "hard transition";
        } else if (modeBlocksFire) {
            frame.comfortReason = "camera mode blocks enemy fire";
        } else if (frame.angularVelocityDeg > stableAngularVelocity) {
            frame.comfortReason = "angular velocity high";
        } else if (frame.angularAccelerationDeg > stableAngularAcceleration) {
            frame.comfortReason = "angular acceleration high";
        } else if (frame.fovChangeRateDeg > stableFovChange) {
            frame.comfortReason = "fov change high";
        } else if (std::abs(frame.rollDeg) > comfortSettings_.stableRollDeg) {
            frame.comfortReason = "roll high";
        } else if (frame.shakeAmount > comfortSettings_.stableShakeAmount) {
            frame.comfortReason = "shake high";
        } else {
            frame.comfortReason = "stable";
        }
    }

    if (!frame.compositionSafeForAiming) {
        frame.allowEnemyFire = false;
        if (frame.comfortReason == "stable" || frame.comfortReason == "initial frame") {
            frame.comfortReason = "composition safety: " + frame.compositionReason;
        }
    }
    if (!frame.vehicleFramingSafe) {
        frame.allowEnemyFire = false;
        frame.comfortReason = "vehicle obscures combat view";
    }
    if (lineOfSightSettings_.blockEnemyFireWhenOccluded && !frame.lineOfSightSafeForAiming) {
        frame.allowEnemyFire = false;
        if (frame.comfortReason == "stable" || frame.comfortReason == "initial frame") {
            frame.comfortReason = "line-of-sight: " + frame.lineOfSightReason;
        }
    }
    if (collisionProtectionSettings_.blockEnemyFireWhenUnsafe && !frame.cameraCollisionSafe) {
        frame.allowEnemyFire = false;
        if (frame.comfortReason == "stable" || frame.comfortReason == "initial frame") {
            frame.comfortReason = "camera collision: " + frame.cameraCollisionReason;
        }
    }
    if (frame.segmentTransitionActive &&
        segmentTransitionElapsed_ <= (std::max)(0.0f, segmentTransitionSettings_.enemyFireHold)) {
        frame.allowEnemyFire = false;
        if (frame.comfortReason == "stable" || frame.comfortReason == "initial frame") {
            frame.comfortReason = "segment transition grace";
        }
    }
    if (encounterFramingSettings_.enabled && encounterFireHoldRemaining_ > 0.0f) {
        frame.allowEnemyFire = false;
        if (frame.comfortReason == "stable" || frame.comfortReason == "initial frame") {
            frame.comfortReason = "encounter framing entry";
        }
    }

    hasPreviousComfortFrame_ = true;
    previousPosition_ = frame.position;
    previousForward_ = frame.forward;
    previousFovY_ = frame.fovY;
    previousRoll_ = frame.rig.roll;
    previousAngularVelocityDeg_ = frame.angularVelocityDeg;
}
