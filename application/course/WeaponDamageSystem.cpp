#include "WeaponDamageSystem.h"

#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>

namespace {
bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float LengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

bool ValidRequest(const WeaponHitRequest& request) {
    if (request.shotId == 0 || request.hitKind == RailAimHitKind::None ||
        !Finite(request.rayOrigin) || !Finite(request.rayDirection) ||
        !Finite(request.hitPoint) || !Finite(request.hitNormal) ||
        !std::isfinite(request.hitDistance) || request.hitDistance < 0.0f ||
        !std::isfinite(request.baseDamage) || request.baseDamage < 0.0f) {
        return false;
    }
    const float directionLengthSquared = LengthSquared(request.rayDirection);
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 0.000001f) {
        return false;
    }
    const float normalLengthSquared = LengthSquared(request.hitNormal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.000001f) {
        return false;
    }
    const float inverseDirectionLength = 1.0f / std::sqrt(directionLengthSquared);
    const Vector3 expectedHitPoint{
        request.rayOrigin.x + request.rayDirection.x * inverseDirectionLength * request.hitDistance,
        request.rayOrigin.y + request.rayDirection.y * inverseDirectionLength * request.hitDistance,
        request.rayOrigin.z + request.rayDirection.z * inverseDirectionLength * request.hitDistance};
    const Vector3 hitPointError{
        expectedHitPoint.x - request.hitPoint.x,
        expectedHitPoint.y - request.hitPoint.y,
        expectedHitPoint.z - request.hitPoint.z};
    const float allowedError = (std::max)(0.05f, request.hitDistance * 0.002f);
    if (LengthSquared(hitPointError) > allowedError * allowedError) {
        return false;
    }
    if ((request.hitKind == RailAimHitKind::Enemy ||
         request.hitKind == RailAimHitKind::Obstacle) &&
        request.targetActorId == 0) {
        return false;
    }
    return true;
}

void ApplyHitPoints(float& hitPoints, DamageResult& result) {
    result.hitPointsBefore = (std::max)(hitPoints, 0.0f);
    result.appliedDamage = (std::min)(result.requestedDamage, result.hitPointsBefore);
    hitPoints = (std::max)(0.0f, result.hitPointsBefore - result.appliedDamage);
    result.remainingHitPoints = hitPoints;
    result.damageApplied = result.appliedDamage > 0.0f;
    result.destroyed = result.hitPointsBefore > 0.0f && hitPoints <= 0.0f;
}
} // namespace

void CourseActorDamageReceiver::Reset() {
    processedShotOrder_.clear();
    processedShotIds_.clear();
    processedRequestCount_ = 0;
    duplicateRequestCount_ = 0;
}

bool CourseActorDamageReceiver::RememberShot(uint64_t shotId) {
    if (processedShotIds_.contains(shotId)) {
        return false;
    }
    processedShotIds_.insert(shotId);
    processedShotOrder_.push_back(shotId);
    while (processedShotOrder_.size() > kShotHistoryCapacity) {
        processedShotIds_.erase(processedShotOrder_.front());
        processedShotOrder_.pop_front();
    }
    return true;
}

DamageResult CourseActorDamageReceiver::Apply(CourseSpawnRuntime& runtime,
    const CourseAsset* course,
    const WeaponHitRequest& request) {
    return runtime.ApplyWeaponHit(*this, course, request);
}

DamageResult CourseActorDamageReceiver::ApplyActors(std::span<CourseEnemyActor> actors, std::span<CourseObstacleActor> obstacles,
        CourseSpawnRuntime& runtime,
        const CourseAsset* course,
        const WeaponHitRequest& request) {
    DamageResult result{};
    result.shotId = request.shotId;
    result.targetActorId = request.targetActorId;
    result.hitKind = request.hitKind;
    result.damageType = request.damageType;
    result.requestedDamage = request.baseDamage;

    if (!ValidRequest(request)) {
        result.rejectReason = DamageRejectReason::InvalidRequest;
        return result;
    }
    if (!RememberShot(request.shotId)) {
        ++duplicateRequestCount_;
        result.rejectReason = DamageRejectReason::DuplicateShot;
        result.duplicate = true;
        return result;
    }

    ++processedRequestCount_;
    result.requestAccepted = true;
    switch (request.hitKind) {
    case RailAimHitKind::Enemy:
        for (CourseEnemyActor& enemy : actors) {
            if (enemy.actorId != request.targetActorId) {
                continue;
            }
            result.targetResolved = true;
            const float currentHitPoints = enemy.combatState.initialized
                ? enemy.combatState.currentHitPoints
                : enemy.desc.hitPoints;
            if (currentHitPoints <= 0.0f) {
                result.rejectReason = DamageRejectReason::TargetAlreadyDestroyed;
                result.remainingHitPoints = 0.0f;
                return result;
            }
            if (enemy.combatState.initialized &&
                !enemy.combatState.canReceiveDamage) {
                result.rejectReason = DamageRejectReason::TargetNotDamageable;
                result.hitPointsBefore = currentHitPoints;
                result.remainingHitPoints = currentHitPoints;
                return result;
            }
            if (enemy.entranceExitState.initialized &&
                !enemy.entranceExitState.targetable) {
                result.rejectReason = DamageRejectReason::TargetNotDamageable;
                result.hitPointsBefore = currentHitPoints;
                result.remainingHitPoints = currentHitPoints;
                return result;
            }
            if (enemy.combatState.initialized) {
                ApplyHitPoints(enemy.combatState.currentHitPoints, result);
                enemy.desc.hitPoints = enemy.combatState.currentHitPoints;
            } else {
                ApplyHitPoints(enemy.desc.hitPoints, result);
            }
            return result;
        }
        result.rejectReason = DamageRejectReason::TargetNotFound;
        return result;

    case RailAimHitKind::Obstacle:
        for (CourseObstacleActor& obstacle : obstacles) {
            if (obstacle.actorId != request.targetActorId) {
                continue;
            }
            result.targetResolved = true;
            result.hitPointsBefore = (std::max)(obstacle.desc.hitPoints, 0.0f);
            result.remainingHitPoints = result.hitPointsBefore;
            if (!obstacle.desc.breakable) {
                result.blocked = true;
                result.rejectReason = DamageRejectReason::Indestructible;
                return result;
            }
            if (obstacle.desc.hitPoints <= 0.0f) {
                result.rejectReason = DamageRejectReason::TargetAlreadyDestroyed;
                return result;
            }
            ApplyHitPoints(obstacle.desc.hitPoints, result);
            return result;
        }
        result.rejectReason = DamageRejectReason::TargetNotFound;
        return result;

    case RailAimHitKind::TerrainPlacement:
        if (course == nullptr || request.sourceIndex >= course->terrainPlacements.size()) {
            result.rejectReason = DamageRejectReason::TargetNotFound;
            return result;
        }
        if (course->terrainPlacements[request.sourceIndex].layer !=
                CourseTerrainLayer::GameplayCollision ||
            course->terrainPlacements[request.sourceIndex].collisionMode ==
                CourseTerrainCollisionMode::None) {
            result.rejectReason = DamageRejectReason::TargetNotFound;
            return result;
        }
        result.targetResolved = true;
        result.blocked = true;
        result.rejectReason = DamageRejectReason::Indestructible;
        return result;

    case RailAimHitKind::ProceduralTerrain:
        result.targetResolved = true;
        result.blocked = true;
        result.rejectReason = DamageRejectReason::Indestructible;
        return result;

    case RailAimHitKind::None:
        result.rejectReason = DamageRejectReason::UnsupportedHitKind;
        return result;
    }

    result.rejectReason = DamageRejectReason::UnsupportedHitKind;
    return result;
}

const char* ToDamageRejectReasonString(DamageRejectReason reason) {
    switch (reason) {
    case DamageRejectReason::None: return "None";
    case DamageRejectReason::InvalidRequest: return "Invalid Request";
    case DamageRejectReason::DuplicateShot: return "Duplicate Shot";
    case DamageRejectReason::UnsupportedHitKind: return "Unsupported Hit Kind";
    case DamageRejectReason::TargetNotFound: return "Target Not Found";
    case DamageRejectReason::TargetAlreadyDestroyed: return "Target Already Destroyed";
    case DamageRejectReason::TargetNotDamageable: return "Target Not Damageable";
    case DamageRejectReason::Indestructible: return "Indestructible";
    }
    return "Unknown";
}
