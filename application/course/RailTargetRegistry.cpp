#include "RailTargetRegistry.h"

#include "CourseSpawnRuntime.h"
#include "../terrain/RailPath.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
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

float EnemyDistance(const CourseEnemyActor& enemy) {
    return enemy.desc.spawnDistance + enemy.desc.distanceOffset;
}

float ObstacleDistance(const CourseObstacleActor& obstacle) {
    return obstacle.desc.spawnDistance + obstacle.desc.distanceOffset;
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

bool ResolveLineOfSightBlock(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    const RailLockTargetHandle& target,
    const Vector3& cameraPosition,
    const Vector3& targetPosition,
    const RailLockSettings& settings,
    uint32_t& outOccluderActorId,
    float& outBlockT) {
    if (!settings.lockLineOfSightEnabled) {
        return false;
    }

    float bestT = 1.0f;
    uint32_t bestOccluder = 0;
    const float padding = (std::max)(0.0f, settings.lockLineOfSightObstaclePadding);
    for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
        if (target.kind == RailLockTargetKind::Obstacle && target.actorId == obstacle.actorId) {
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
                cameraPosition,
                targetPosition,
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
} // namespace

void RailTargetRegistry::Build(const RailTargetRegistryFrameInput& input) {
    anchors_.clear();
    if (input.spawnRuntime == nullptr || input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        return;
    }

    for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
        if (enemy.desc.hitPoints <= 0.0f ||
            (enemy.combatState.initialized &&
             !enemy.combatState.canBeTargeted) ||
            (enemy.entranceExitState.initialized &&
             !enemy.entranceExitState.targetable)) {
            continue;
        }
        const float distance = EnemyDistance(enemy);
        RailLockAnchor anchor{};
        anchor.target.kind = RailLockTargetKind::Enemy;
        anchor.target.actorId = enemy.actorId;
        anchor.target.generationId = enemy.actorId;
        anchor.anchorId = 0;
        anchor.label = enemy.desc.role.empty() ? enemy.desc.waveId : enemy.desc.role;
        anchor.worldPosition = ResolveRailLocal(
            *input.railPath,
            enemy.desc.spawnDistance,
            enemy.desc.distanceOffset,
            enemy.desc.lateralOffset,
            enemy.desc.verticalOffset);
        anchor.screenRadius = (std::max)(input.settings.enemyScreenRadius, enemy.desc.radius * 12.0f);
        anchor.forwardDistance = distance - input.playerDistance;
        if (input.viewRelativeTargeting) {
            const auto delta = Subtract(anchor.worldPosition, input.cameraPosition);
            const auto& f = input.viewForward;
            anchor.forwardDistance = delta.x*f.x + delta.y*f.y + delta.z*f.z;
        }
        anchor.priority = 1.0f;
        const bool weaponPart=RailMothership::PartIndex(enemy.desc.actorAssetId)>=0;
        anchor.maxStack = weaponPart ? (std::min)(4,input.settings.maxLocks) :
            (enemy.desc.actorAssetId=="short_mothership_core" ? input.settings.maxLocks : 1);
        anchor.stackAcquireIntervalSeconds = weaponPart ? 0.22f :
            (enemy.desc.actorAssetId=="short_mothership_core" ? 0.12f : 0.0f);
        anchor.lineOfSightBlocked = ResolveLineOfSightBlock(
            *input.spawnRuntime,
            *input.railPath,
            anchor.target,
            input.cameraPosition,
            anchor.worldPosition,
            input.settings,
            anchor.lineOfSightOccluderActorId,
            anchor.lineOfSightBlockT);
        anchors_.push_back(std::move(anchor));
    }

    for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
        if (!obstacle.desc.breakable) {
            continue;
        }
        const float distance = ObstacleDistance(obstacle);
        RailLockAnchor anchor{};
        anchor.target.kind = RailLockTargetKind::Obstacle;
        anchor.target.actorId = obstacle.actorId;
        anchor.target.generationId = obstacle.actorId;
        anchor.anchorId = 0;
        anchor.label = obstacle.desc.id;
        anchor.worldPosition = ResolveRailLocal(
            *input.railPath,
            obstacle.desc.spawnDistance,
            obstacle.desc.distanceOffset,
            obstacle.desc.lateralOffset,
            obstacle.desc.verticalOffset);
        const float size = (std::max)(obstacle.desc.halfExtents.x, obstacle.desc.halfExtents.y);
        anchor.screenRadius = (std::max)(input.settings.obstacleScreenRadius, size * 8.0f);
        anchor.forwardDistance = distance - input.playerDistance;
        if (input.viewRelativeTargeting) {
            const auto delta = Subtract(anchor.worldPosition, input.cameraPosition);
            const auto& f = input.viewForward;
            anchor.forwardDistance = delta.x*f.x + delta.y*f.y + delta.z*f.z;
        }
        anchor.priority = 0.8f;
        anchor.maxStack = 1;
        anchor.lineOfSightBlocked = ResolveLineOfSightBlock(
            *input.spawnRuntime,
            *input.railPath,
            anchor.target,
            input.cameraPosition,
            anchor.worldPosition,
            input.settings,
            anchor.lineOfSightOccluderActorId,
            anchor.lineOfSightBlockT);
        anchors_.push_back(std::move(anchor));
    }
}

