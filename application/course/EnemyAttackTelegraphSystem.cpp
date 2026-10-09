#include "EnemyAttackTelegraphSystem.h"
#include "../diagnostics/DronePerformanceProfile.h"

#include "CourseAsset.h"
#include "RailAimState.h"
#include "RailWorldRaycast.h"
#include "../terrain/RailPath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <unordered_set>
#include <utility>

namespace {
constexpr float kEpsilon = 0.00001f;
constexpr float kTau = 6.28318530717958647692f;

float Clamp01(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
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

float LengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kEpsilon) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

Vector2 NormalizeOr(const Vector2& value, const Vector2& fallback) {
    const float lengthSquared = value.x * value.x + value.y * value.y;
    if (!std::isfinite(lengthSquared) || lengthSquared <= kEpsilon) {
        return fallback;
    }
    const float invLength = 1.0f / std::sqrt(lengthSquared);
    return {value.x * invLength, value.y * invLength};
}

Vector3 EnemyWorldPosition(const CourseEnemyActor& enemy, const RailPath& railPath) {
    const RailPathSample sample = railPath.Evaluate(
        enemy.desc.spawnDistance + enemy.desc.distanceOffset);
    return Add(
        Add(sample.position, Scale(sample.right, enemy.desc.lateralOffset)),
        Scale(sample.up, enemy.desc.verticalOffset));
}

struct ProjectedThreat {
    Vector2 rawScreen{};
    Vector2 screen{};
    Vector2 directionFromCenter{};
    float depth = 0.0f;
    bool onScreen = false;
    bool behind = false;
};

ProjectedThreat ProjectThreat(
    const Vector3& world,
    const Matrix4x4& matrix,
    uint32_t width,
    uint32_t height,
    float safeAreaPixels) {
    const float clipX = world.x * matrix.m[0][0] + world.y * matrix.m[1][0] +
        world.z * matrix.m[2][0] + matrix.m[3][0];
    const float clipY = world.x * matrix.m[0][1] + world.y * matrix.m[1][1] +
        world.z * matrix.m[2][1] + matrix.m[3][1];
    const float clipZ = world.x * matrix.m[0][2] + world.y * matrix.m[1][2] +
        world.z * matrix.m[2][2] + matrix.m[3][2];
    const float clipW = world.x * matrix.m[0][3] + world.y * matrix.m[1][3] +
        world.z * matrix.m[2][3] + matrix.m[3][3];

    ProjectedThreat result{};
    result.behind = clipW <= kEpsilon;
    const float safeW = std::abs(clipW) > kEpsilon ? std::abs(clipW) : 1.0f;
    float ndcX = clipX / safeW;
    float ndcY = clipY / safeW;
    if (result.behind) {
        ndcX = -ndcX;
        ndcY = -ndcY;
    }
    result.depth = clipW > kEpsilon ? clipZ / clipW : -1.0f;
    result.rawScreen = {
        (ndcX * 0.5f + 0.5f) * static_cast<float>(width),
        (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height)};

    const Vector2 center{
        static_cast<float>(width) * 0.5f,
        static_cast<float>(height) * 0.5f};
    Vector2 fromCenter{
        result.rawScreen.x - center.x,
        result.rawScreen.y - center.y};
    if (result.behind &&
        std::abs(fromCenter.x) <= kEpsilon &&
        std::abs(fromCenter.y) <= kEpsilon) {
        fromCenter = {0.0f, 1.0f};
    }
    result.directionFromCenter = NormalizeOr(fromCenter, {0.0f, -1.0f});

    const float margin = (std::clamp)(
        safeAreaPixels,
        0.0f,
        (std::max)(0.0f, (std::min)(static_cast<float>(width), static_cast<float>(height)) * 0.45f));
    const float minX = margin;
    const float maxX = (std::max)(margin, static_cast<float>(width) - margin);
    const float minY = margin;
    const float maxY = (std::max)(margin, static_cast<float>(height) - margin);
    const bool withinViewport = !result.behind &&
        result.depth >= 0.0f && result.depth <= 1.0f &&
        result.rawScreen.x >= 0.0f && result.rawScreen.x <= static_cast<float>(width) &&
        result.rawScreen.y >= 0.0f && result.rawScreen.y <= static_cast<float>(height);
    result.onScreen = withinViewport &&
        result.rawScreen.x >= minX && result.rawScreen.x <= maxX &&
        result.rawScreen.y >= minY && result.rawScreen.y <= maxY;
    if (result.onScreen) {
        result.screen = result.rawScreen;
        return result;
    }

    const float halfWidth = (std::max)(1.0f, center.x - margin);
    const float halfHeight = (std::max)(1.0f, center.y - margin);
    const float xScale = std::abs(result.directionFromCenter.x) > kEpsilon
        ? halfWidth / std::abs(result.directionFromCenter.x)
        : (std::numeric_limits<float>::max)();
    const float yScale = std::abs(result.directionFromCenter.y) > kEpsilon
        ? halfHeight / std::abs(result.directionFromCenter.y)
        : (std::numeric_limits<float>::max)();
    const float edgeScale = (std::min)(xScale, yScale);
    result.screen = {
        center.x + result.directionFromCenter.x * edgeScale,
        center.y + result.directionFromCenter.y * edgeScale};
    return result;
}

float PatternSeverity(CourseEnemyFirePattern pattern) {
    switch (pattern) {
    case CourseEnemyFirePattern::Single: return 0.34f;
    case CourseEnemyFirePattern::Twin: return 0.50f;
    case CourseEnemyFirePattern::Spread: return 0.70f;
    case CourseEnemyFirePattern::BossArc: return 1.00f;
    }
    return 0.34f;
}

bool IsVisibilityWarmup(const CourseEnemyActor& enemy) {
    return enemy.fireSafetyReason == "visible time warming";
}

bool IsOccluded(
    const EnemyAttackTelegraphFrameInput& input,
    const CourseEnemyActor& enemy,
    const Vector3& targetPosition) {
    const Vector3 toTarget = Subtract(targetPosition, input.cameraPosition);
    const float distanceSquared = LengthSquared(toTarget);
    if (distanceSquared <= kEpsilon) {
        return false;
    }
    const float distance = std::sqrt(distanceSquared);
    RailAimState visibilityAim{};
    visibilityAim.worldRayOrigin = input.cameraPosition;
    visibilityAim.worldRayDirection = NormalizeOr(toTarget, {0.0f, 0.0f, 1.0f});
    visibilityAim.maxDistance = distance + (std::max)(0.05f, enemy.desc.radius);
    visibilityAim.aimDistance = visibilityAim.maxDistance;
    visibilityAim.worldAimPoint = Add(
        visibilityAim.worldRayOrigin,
        Scale(visibilityAim.worldRayDirection, visibilityAim.maxDistance));
    visibilityAim.valid = true;

    RailWorldRaycastInput query{};
    query.aim = &visibilityAim;
    query.railPath = input.railPath;
    query.spawnRuntime = input.spawnRuntime;
    query.course = input.course;
    query.terrainSettings = input.terrainSettings;
    query.terrainEdits = input.terrainEdits;
    query.terrainPreview = input.terrainPreview;
    query.playerDistance = input.playerDistance;
    query.includeVisualColumns = true;
    const RailAimHit hit = RailWorldRaycast::Query(query);
    return hit.hit &&
        !(hit.kind == RailAimHitKind::Enemy && hit.actorId == enemy.actorId);
}

bool HigherPriority(
    const EnemyAttackTelegraphCue& left,
    const EnemyAttackTelegraphCue& right) {
    if (std::abs(left.priority - right.priority) > 0.00001f) {
        return left.priority > right.priority;
    }
    if (std::abs(left.timeToFire - right.timeToFire) > 0.00001f) {
        return left.timeToFire < right.timeToFire;
    }
    return left.actorId < right.actorId;
}
} // namespace

EnemyAttackTelegraphReadabilityStyle ResolveEnemyAttackTelegraphReadabilityStyle(
    EnemyAttackTelegraphPhase phase,
    float pulse) noexcept {
    const float boundedPulse = Clamp01(pulse);
    EnemyAttackTelegraphReadabilityStyle style{};
    switch (phase) {
    case EnemyAttackTelegraphPhase::Warming:
        style.label = reinterpret_cast<const char*>(u8"\u4e88\u544a");
        style.primaryColor = {1.0f, 0.82f, 0.16f, 0.74f};
        style.glowAlpha = 0.16f;
        style.markerScale = 0.92f;
        style.tier = 0;
        break;
    case EnemyAttackTelegraphPhase::Tracking:
        style.label = reinterpret_cast<const char*>(u8"\u4e88\u544a");
        style.primaryColor = {1.0f, 0.48f, 0.04f, 0.88f};
        style.glowAlpha = 0.24f;
        style.markerScale = 1.0f;
        style.tier = 1;
        break;
    case EnemyAttackTelegraphPhase::Imminent:
        style.label = reinterpret_cast<const char*>(u8"\u5371\u967a");
        style.primaryColor = {
            1.0f,
            0.10f + boundedPulse * 0.06f,
            0.02f,
            0.94f + boundedPulse * 0.06f};
        style.glowAlpha = 0.38f;
        style.markerScale = 1.12f;
        style.tier = 2;
        break;
    case EnemyAttackTelegraphPhase::Fired:
        style.label = reinterpret_cast<const char*>(u8"\u767a\u5c04");
        style.primaryColor = {1.0f, 0.96f, 0.78f, 1.0f};
        style.glowAlpha = 0.48f;
        style.markerScale = 1.60f;
        style.tier = 3;
        style.showCountdown = false;
        break;
    case EnemyAttackTelegraphPhase::None:
        style.label = "";
        style.primaryColor = {1.0f, 0.48f, 0.04f, 0.0f};
        style.glowAlpha = 0.0f;
        style.markerScale = 0.0f;
        style.tier = 0;
        style.showCountdown = false;
        break;
    }
    return style;
}

std::string FormatEnemyAttackCountdown(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return {};
    const float roundedUp = std::ceil(
        (std::clamp)(seconds, 0.0f, 9.9f) * 10.0f) / 10.0f;
    const float readable = (std::max)(0.1f, roundedUp);
    char text[16]{};
    std::snprintf(text, sizeof(text), "%.1fs", readable);
    return text;
}

void EnemyAttackTelegraphSystem::Reset() {
    trackedActors_.clear();
    frame_ = {};
    elapsedTime_ = 0.0f;
    revision_ = 0;
}

void EnemyAttackTelegraphSystem::CancelActor(uint32_t actorId) {
    trackedActors_.erase(actorId);
    std::erase_if(frame_.cues,[actorId](const auto& cue) { return cue.actorId == actorId; });
    std::erase_if(frame_.events,[actorId](const auto& event) { return event.actorId == actorId; });
}

void EnemyAttackTelegraphSystem::Update(
    const EnemyAttackTelegraphFrameInput& input) {
    const drone_perf::Scope profile(drone_perf::Stage::Telegraph);
    frame_ = {};
    const float dt = (std::clamp)(input.deltaTime, 0.0f, 0.1f);
    elapsedTime_ += dt;
    frame_.revision = ++revision_;
    if (!input.settings.enabled || input.spawnRuntime == nullptr ||
        input.railPath == nullptr || input.viewProjection == nullptr ||
        input.railPath->Length() <= 0.0f || input.viewportWidth == 0 ||
        input.viewportHeight == 0) {
        trackedActors_.clear();
        return;
    }

    const float leadSeconds = (std::max)(0.05f, input.settings.leadSeconds);
    const float imminentSeconds = (std::clamp)(
        input.settings.imminentSeconds, 0.01f, leadSeconds);
    std::unordered_set<uint32_t> activeActorIds;
    std::vector<EnemyAttackTelegraphCue> candidates;
    candidates.reserve(input.spawnRuntime->Enemies().size());

    for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
        if (enemy.actorId == 0 || enemy.desc.hitPoints <= 0.0f ||
            enemy.behaviorState.engagementBandExitRequested ||
            (enemy.entranceExitState.initialized &&
             (enemy.entranceExitState.exitRequested ||
              enemy.entranceExitState.phase == EnemyEntranceExitPhase::Exiting ||
              enemy.entranceExitState.phase == EnemyEntranceExitPhase::Exited)) ||
            (enemy.combatState.initialized &&
             !enemy.combatState.canTelegraph)) {
            continue;
        }
        ++frame_.stats.activeEnemies;
        activeActorIds.insert(enemy.actorId);
        auto [trackedIt, newlyTracked] =
            trackedActors_.try_emplace(enemy.actorId);
        TrackedActor& tracked = trackedIt->second;
        if (newlyTracked) {
            // A system reset or late HUD attachment must not replay an old shot
            // merely because this actor already has a non-zero sequence.
            tracked.lastFireSequence = enemy.fireSequence;
            tracked.lastNotifiedFireSequence = enemy.fireSequence;
        }
        tracked.firedFlashRemaining = (std::max)(
            0.0f, tracked.firedFlashRemaining - dt);
        const bool firedThisFrame = enemy.bulletsEmittedThisFrame > 0 ||
            (!newlyTracked && enemy.fireSequence > tracked.lastFireSequence);
        if (firedThisFrame) {
            tracked.firedFlashRemaining = (std::max)(
                0.01f, input.settings.firedFlashSeconds);
        }
        tracked.lastFireSequence = enemy.fireSequence;
        const bool firedFlash = tracked.firedFlashRemaining > 0.0f;

        const bool warming = IsVisibilityWarmup(enemy);
        const bool behaviorDriven = enemy.behaviorState.initialized &&
            enemy.behaviorDefinition.commercialBehavior;
        if(behaviorDriven && !firedFlash &&
            (!enemy.targetingState.forwardInterceptReachable ||
             !CanEnemyProjectileVolleyReachTarget(enemy))) continue;
        // A choreographed pass owns its full charge interval; the default HUD
        // lead window must not hide the cue and deadlock presentation admission.
        const float actorLeadSeconds = behaviorDriven
            ? (std::max)(leadSeconds, enemy.behaviorDefinition.attackLeadSeconds)
            : leadSeconds;
        if (behaviorDriven && !firedFlash &&
            (!enemy.behaviorState.attackIntentActive ||
             !enemy.attackState.tokenReserved ||
             enemy.attackState.intentSequence !=
                enemy.behaviorState.attackIntentSequence)) {
            continue;
        }
        float timeToFire = behaviorDriven
            ? (std::max)(0.0f, enemy.behaviorState.attackTimeRemaining)
            : (std::max)(0.0f, enemy.fireTimer);
        if (warming) {
            const float remainingWarmup = (std::max)(
                0.0f,
                input.spawnRuntime->FireSafetySettings().minVisibleBeforeFire -
                    enemy.fireVisibleTime);
            timeToFire = (std::max)(timeToFire, remainingWarmup);
        }
        if (behaviorDriven && !firedFlash && !CanEnemyProjectileVolleyReachTarget(enemy,timeToFire)) {
            input.spawnRuntime->DeferEnemyAttackForUnreachableLaunch(enemy.actorId);
            continue;
        }
        const bool countdownReadable = behaviorDriven
            ? enemy.behaviorState.attackIntentActive && enemy.fireEnvironmentReady && input.cameraAllowsAttack
            : input.cameraAllowsAttack && (enemy.fireSafetyAllowed ||
                !input.spawnRuntime->FireSafetySettings().enabled);
        if (!firedFlash &&
            (!countdownReadable || timeToFire <= 0.0f || timeToFire > actorLeadSeconds)) {
            if (behaviorDriven && (!countdownReadable || timeToFire <= 0.0f)) {
                input.spawnRuntime->InvalidateEnemyAttackWarning(enemy.actorId);
            }
            continue;
        }

        EnemyAttackTelegraphCue cue{};
        cue.actorId = enemy.actorId;
        cue.fireSequence = enemy.fireSequence;
        cue.attackIntentSequence = behaviorDriven
            ? enemy.behaviorState.attackIntentSequence
            : 0;
        cue.attackTokenId = behaviorDriven
            ? enemy.attackState.tokenId
            : 0;
        if (behaviorDriven && enemy.targetingState.solutionLocked &&
            enemy.targetingState.attackIntentSequence ==
                enemy.attackState.intentSequence &&
            enemy.targetingState.attackTokenId == enemy.attackState.tokenId) {
            cue.targetRailDistance = enemy.targetingState.targetDistance;
            cue.targetLateralOffset =
                enemy.targetingState.targetLateralOffset;
            cue.targetVerticalOffset =
                enemy.targetingState.targetVerticalOffset;
            cue.predictedFlightSeconds =
                enemy.targetingState.predictedFlightSeconds;
            cue.hasLockedTarget = true;
            if(enemy.desc.projectileDefinition.trajectory==EnemyProjectileTrajectory::Direct &&
                enemy.desc.projectileDefinition.acceleration==0.0f) {
                const int count=(std::clamp)(enemy.desc.bulletCount,1,8);
                cue.projectileLaunches.reserve(count);
                cue.projectileFlightSeconds.reserve(count);
                for(int index=0;index<count;++index) {
                    auto launch = ResolveEnemyProjectileScheduledLaunch(enemy,index,firedFlash ? 0.0f : timeToFire);
                    cue.projectileFlightSeconds.push_back(launch.flightSeconds);
                    cue.projectileLaunches.push_back(std::move(launch.projectile));
                }
                if (!cue.projectileLaunches.empty()) {
                    const auto& first = cue.projectileLaunches.front();
                    cue.targetRailDistance = first.lockedTargetDistance;
                    cue.targetLateralOffset = first.lockedTargetLateralOffset;
                    cue.targetVerticalOffset = first.lockedTargetVerticalOffset;
                    cue.predictedFlightSeconds = cue.projectileFlightSeconds.front();
                }
            }
        }
        cue.attackPattern = enemy.desc.firePattern;
        cue.worldPosition = EnemyWorldPosition(enemy, *input.railPath);
        cue.timeToFire = timeToFire;
        cue.projectileCount = (std::max)(1, enemy.desc.bulletCount);
        cue.projectileTrajectory =
            enemy.desc.projectileDefinition.trajectory;
        cue.phase = firedFlash
            ? EnemyAttackTelegraphPhase::Fired
            : (timeToFire <= imminentSeconds
                ? EnemyAttackTelegraphPhase::Imminent
                : (warming
                    ? EnemyAttackTelegraphPhase::Warming
                    : EnemyAttackTelegraphPhase::Tracking));
        cue.urgency = firedFlash
            ? 1.0f
            : 1.0f - Clamp01(timeToFire / actorLeadSeconds);
        cue.severity = Clamp01(
            PatternSeverity(enemy.desc.firePattern) +
            Clamp01(enemy.desc.bulletDamage / 40.0f) * 0.12f +
            Clamp01(static_cast<float>(cue.projectileCount - 1) / 6.0f) * 0.10f);
        const ProjectedThreat projected = ProjectThreat(
            cue.worldPosition,
            *input.viewProjection,
            input.viewportWidth,
            input.viewportHeight,
            input.settings.safeAreaPixels);
        cue.screenPosition = projected.screen;
        cue.directionFromCenter = projected.directionFromCenter;
        cue.onScreen = projected.onScreen;
        cue.behindCamera = projected.behind;
        if (!cue.onScreen) {
            input.spawnRuntime->InvalidateEnemyAttackWarning(enemy.actorId);
            continue;
        }
        const auto sample = input.railPath->Evaluate(enemy.desc.spawnDistance + enemy.desc.distanceOffset);
        const auto edge = ProjectThreat(Add(cue.worldPosition, Scale(sample.up, enemy.desc.radius)),
            *input.viewProjection, input.viewportWidth, input.viewportHeight, 0.0f);
        const float dx = edge.rawScreen.x - projected.rawScreen.x;
        const float dy = edge.rawScreen.y - projected.rawScreen.y;
        cue.bodyRadiusPixels = std::sqrt(dx * dx + dy * dy);
        cue.priority = cue.severity * 0.54f + cue.urgency * 0.46f +
            (cue.onScreen ? 0.0f : input.settings.offscreenPriorityBonus);
        cue.pulse = 0.5f + 0.5f * std::sin(
            elapsedTime_ * kTau * (1.5f + cue.urgency * 3.5f) +
            static_cast<float>(cue.actorId % 7u) * 0.37f);
        candidates.push_back(cue);
    }

    for (auto it = trackedActors_.begin(); it != trackedActors_.end();) {
        if (!activeActorIds.contains(it->first)) {
            it = trackedActors_.erase(it);
        } else {
            ++it;
        }
    }

    frame_.stats.candidateCues = static_cast<uint32_t>(candidates.size());
    std::stable_sort(candidates.begin(), candidates.end(), HigherPriority);
    const uint32_t maximumVisible = (std::max)(1u, input.settings.maximumVisibleCues);
    uint32_t visibilityBudget = input.settings.maximumVisibilityQueries;
    std::unordered_set<uint32_t> presentedActorIds;

    for (EnemyAttackTelegraphCue& cue : candidates) {
        if (frame_.cues.size() >= maximumVisible) {
            ++frame_.stats.prioritySuppressed;
            input.spawnRuntime->InvalidateEnemyAttackWarning(cue.actorId);
            continue;
        }
        const CourseEnemyActor* enemy = nullptr;
        for (const CourseEnemyActor& candidateEnemy : input.spawnRuntime->Enemies()) {
            if (candidateEnemy.actorId == cue.actorId) {
                enemy = &candidateEnemy;
                break;
            }
        }
        if (input.settings.requireWorldVisibility && enemy != nullptr) {
            if (visibilityBudget > 0) {
                --visibilityBudget;
                ++frame_.stats.visibilityQueries;
                cue.visibilityTested = true;
                cue.occluded = IsOccluded(input, *enemy, cue.worldPosition);
            } else {
                ++frame_.stats.visibilityBudgetExhausted;
                input.spawnRuntime->InvalidateEnemyAttackWarning(cue.actorId);
                continue;
            }
        }
        if (cue.occluded) {
            ++frame_.stats.occludedCues;
            if (input.settings.suppressOccluded) {
                input.spawnRuntime->InvalidateEnemyAttackWarning(cue.actorId);
                continue;
            }
        }

        TrackedActor& tracked = trackedActors_[cue.actorId];
        cue.newlyPresented = !tracked.wasPresented;
        if (cue.phase == EnemyAttackTelegraphPhase::Fired &&
            cue.fireSequence > tracked.lastNotifiedFireSequence) {
            frame_.events.push_back({
                cue.actorId,
                cue.fireSequence,
                EnemyAttackTelegraphEventKind::Fired,
                cue.severity,
                !cue.onScreen});
            tracked.lastNotifiedFireSequence = cue.fireSequence;
        } else if (cue.phase == EnemyAttackTelegraphPhase::Imminent &&
                   tracked.lastPresentedPhase != EnemyAttackTelegraphPhase::Imminent) {
            frame_.events.push_back({
                cue.actorId,
                cue.fireSequence,
                EnemyAttackTelegraphEventKind::Imminent,
                cue.severity,
                !cue.onScreen});
        } else if (cue.newlyPresented) {
            frame_.events.push_back({
                cue.actorId,
                cue.fireSequence,
                EnemyAttackTelegraphEventKind::Acquired,
                cue.severity,
                !cue.onScreen});
        }
        tracked.lastPresentedPhase = cue.phase;
        presentedActorIds.insert(cue.actorId);
        frame_.highestPriority = (std::max)(frame_.highestPriority, cue.priority);
        if (cue.onScreen) {
            ++frame_.stats.onScreenCues;
        } else {
            ++frame_.stats.offscreenCues;
        }
        frame_.cues.push_back(cue);
    }

    for (auto& [actorId, tracked] : trackedActors_) {
        tracked.wasPresented = presentedActorIds.contains(actorId);
        if (!tracked.wasPresented) {
            tracked.lastPresentedPhase = EnemyAttackTelegraphPhase::None;
        }
    }
    frame_.stats.visibleCues = static_cast<uint32_t>(frame_.cues.size());
}

const char* ToEnemyAttackTelegraphPhaseString(
    EnemyAttackTelegraphPhase phase) {
    switch (phase) {
    case EnemyAttackTelegraphPhase::None: return "None";
    case EnemyAttackTelegraphPhase::Warming: return "Warming";
    case EnemyAttackTelegraphPhase::Tracking: return "Tracking";
    case EnemyAttackTelegraphPhase::Imminent: return "Imminent";
    case EnemyAttackTelegraphPhase::Fired: return "Fired";
    }
    return "Unknown";
}

const char* ToEnemyAttackTelegraphEventKindString(
    EnemyAttackTelegraphEventKind kind) {
    switch (kind) {
    case EnemyAttackTelegraphEventKind::Acquired: return "Acquired";
    case EnemyAttackTelegraphEventKind::Imminent: return "Imminent";
    case EnemyAttackTelegraphEventKind::Fired: return "Fired";
    }
    return "Unknown";
}
