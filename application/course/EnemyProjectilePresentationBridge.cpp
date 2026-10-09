#include "EnemyProjectilePresentationBridge.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace {

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(Vector3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

Vector3 NormalizeOr(Vector3 value, Vector3 fallback) noexcept {
    const float length = Length(value);
    return length > 0.00001f
        ? Scale(value, 1.0f / length)
        : fallback;
}

Vector3 ResolveRailLocal(
    const RailPath& railPath,
    float distance,
    float lateral,
    float vertical) noexcept {
    return ResolveEnemyProjectileWorldPosition(railPath,{lateral,vertical,distance});
}

EnemyProjectilePresentation BuildPresentation(
    const EnemyProjectileRuntimeState& projectile,
    const RailPath& railPath,
    Vector3 playerWorld,
    float playerDistance,
    const EnemyProjectilePresentationSettings& settings,
    bool spawnedThisFrame) {
    EnemyProjectilePresentation output{};
    const float distance = projectile.spawnDistance + projectile.distanceOffset;
    const float previousDistance =
        projectile.spawnDistance + projectile.previousDistanceOffset;
    const RailPathSample sample = railPath.Evaluate(distance);
    output.projectileId = projectile.projectileId;
    output.ownerActorId = projectile.ownerActorId;
    output.attackIntentSequence = projectile.attackIntentSequence;
    output.attackTokenId = projectile.attackTokenId;
    output.trajectory = projectile.trajectory;
    output.definitionId = projectile.definitionId;
    output.trailEffectId = projectile.trailEffectId;
    output.impactEffectId = projectile.impactEffectId;
    output.defenseResponses = projectile.defenseResponses;
    output.worldPosition = ResolveRailLocal(
        railPath,
        distance,
        projectile.lateralOffset,
        projectile.verticalOffset);
    output.previousWorldPosition = ResolveRailLocal(
        railPath,
        previousDistance,
        projectile.previousLateralOffset,
        projectile.previousVerticalOffset);
    const Vector3 fallbackDirection = projectile.forwardSpeed < 0.0f
        ? Scale(sample.tangent, -1.0f)
        : sample.tangent;
    output.motionDirection = NormalizeOr(
        Subtract(output.worldPosition, output.previousWorldPosition),
        fallbackDirection);
    output.railRight = sample.right;
    output.railUp = sample.up;
    output.color = projectile.color;
    output.collisionRadius = (std::max)(0.02f, projectile.radius);
    output.normalizedAge = projectile.lifetime > 0.0f
        ? (std::clamp)(projectile.age / projectile.lifetime, 0.0f, 1.0f)
        : 1.0f;
    output.distanceToPlayer = Length(Subtract(output.worldPosition, playerWorld));
    output.forwardDistanceToPlayer = distance - playerDistance;
    const float lateralDistance = std::sqrt(
        projectile.lateralOffset * projectile.lateralOffset +
        projectile.verticalOffset * projectile.verticalOffset);
    output.threat = output.forwardDistanceToPlayer >= -2.0f &&
        output.forwardDistanceToPlayer <=
            (std::max)(1.0f, settings.threatForwardDistance) &&
        lateralDistance <= (std::max)(1.0f, settings.threatLateralDistance);
    output.spawnedThisFrame = spawnedThisFrame;
    return output;
}

} // namespace

void EnemyProjectilePresentationBridge::Reset() {
    frame_ = {};
    tracked_.clear();
    revision_ = 0;
}

void EnemyProjectilePresentationBridge::Update(
    const EnemyProjectilePresentationInput& input) {
    frame_ = {};
    if (!input.settings.enabled || input.runtime == nullptr ||
        input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        tracked_.clear();
        frame_.revision = ++revision_;
        return;
    }

    const Vector3 playerWorld = ResolveRailLocal(
        *input.railPath,
        input.playerDistance,
        input.playerLateralOffset,
        input.playerVerticalOffset);
    const size_t maximumProjectiles = (std::max)(
        static_cast<size_t>(1),
        input.settings.maximumVisibleProjectiles);
    std::unordered_map<uint64_t, EnemyProjectilePresentation> nextTracked;
    nextTracked.reserve((std::min)(
        maximumProjectiles,
        input.runtime->Bullets().size()));
    std::unordered_set<uint64_t> currentIds;
    currentIds.reserve(input.runtime->Bullets().size());

    for (const EnemyProjectileRuntimeState& projectile :
         input.runtime->Bullets()) {
        if (!projectile.active || projectile.age >= projectile.lifetime ||
            projectile.projectileId == 0) {
            continue;
        }
        ++frame_.activeRuntimeProjectiles;
        currentIds.insert(projectile.projectileId);
        const bool spawned = tracked_.find(projectile.projectileId) == tracked_.end();
        EnemyProjectilePresentation visual = BuildPresentation(
            projectile,
            *input.railPath,
            playerWorld,
            input.playerDistance,
            input.settings,
            spawned);
        const Vector3 eventPosition = visual.worldPosition;
        const Vector3 eventMotionDirection = visual.motionDirection;
        if (frame_.projectiles.size() < maximumProjectiles) {
            frame_.projectiles.push_back(visual);
        } else {
            ++frame_.droppedProjectiles;
        }
        nextTracked.emplace(projectile.projectileId, std::move(visual));
        if (spawned && input.gameplayActive) {
            EnemyProjectilePresentationEvent event{};
            event.kind = EnemyProjectilePresentationEventKind::Spawned;
            event.projectileId = projectile.projectileId;
            event.ownerActorId = projectile.ownerActorId;
            event.trajectory = projectile.trajectory;
            event.worldPosition = eventPosition;
            event.motionDirection = eventMotionDirection;
            event.color = projectile.color;
            event.effectId = projectile.trailEffectId;
            PushEvent(std::move(event), input.settings.maximumEventsPerFrame);
        }
    }

    std::unordered_set<uint64_t> impactedIds;
    impactedIds.reserve(input.playerDamageResults.size());
    if (input.gameplayActive) {
        for (const PlayerDamageResult& result : input.playerDamageResults) {
            if (!result.accepted ||
                result.request.kind != PlayerHitKind::EnemyProjectile ||
                result.request.sourceProjectileId == 0) {
                continue;
            }
            impactedIds.insert(result.request.sourceProjectileId);
            EnemyProjectilePresentationEvent event{};
            event.kind = EnemyProjectilePresentationEventKind::Impacted;
            event.projectileId = result.request.sourceProjectileId;
            event.ownerActorId = result.request.sourceActorId;
            event.worldPosition = ResolveRailLocal(
                *input.railPath,
                result.request.railDistance,
                result.request.lateralOffset,
                result.request.verticalOffset);
            event.effectId = result.request.impactEffectId;
            event.lethal = result.lethal;
            const auto tracked = tracked_.find(event.projectileId);
            if (tracked != tracked_.end()) {
                event.trajectory = tracked->second.trajectory;
                event.motionDirection = tracked->second.motionDirection;
                event.color = tracked->second.color;
            }
            PushEvent(std::move(event), input.settings.maximumEventsPerFrame);
        }

        for (const EnemyProjectileShootDownResult& result :
             input.shootDownResults) {
            if (!result.accepted || !result.destroyed ||
                result.projectileId == 0) {
                continue;
            }
            impactedIds.insert(result.projectileId);
            EnemyProjectilePresentationEvent event{};
            event.kind = EnemyProjectilePresentationEventKind::Intercepted;
            event.projectileId = result.projectileId;
            event.ownerActorId = result.ownerActorId;
            event.worldPosition = result.worldHitPoint;
            const auto tracked = tracked_.find(event.projectileId);
            if (tracked != tracked_.end()) {
                event.trajectory = tracked->second.trajectory;
                event.motionDirection = tracked->second.motionDirection;
                event.color = tracked->second.color;
                event.effectId = tracked->second.impactEffectId;
            }
            PushEvent(std::move(event), input.settings.maximumEventsPerFrame);
        }
    }

    if (input.gameplayActive) {
        for (const auto& [projectileId, previous] : tracked_) {
            if (currentIds.contains(projectileId) ||
                impactedIds.contains(projectileId)) {
                continue;
            }
            EnemyProjectilePresentationEvent event{};
            event.kind = EnemyProjectilePresentationEventKind::Expired;
            event.projectileId = projectileId;
            event.ownerActorId = previous.ownerActorId;
            event.trajectory = previous.trajectory;
            event.worldPosition = previous.worldPosition;
            event.motionDirection = previous.motionDirection;
            event.color = previous.color;
            PushEvent(std::move(event), input.settings.maximumEventsPerFrame);
        }
    }

    std::sort(
        frame_.projectiles.begin(),
        frame_.projectiles.end(),
        [](const EnemyProjectilePresentation& left,
           const EnemyProjectilePresentation& right) {
            if (left.threat != right.threat) return left.threat > right.threat;
            if (left.distanceToPlayer != right.distanceToPlayer) {
                return left.distanceToPlayer < right.distanceToPlayer;
            }
            return left.projectileId < right.projectileId;
        });
    tracked_ = std::move(nextTracked);
    frame_.sourceProjectileRevision =
        input.runtime->EnemyProjectiles().Frame().revision;
    frame_.revision = ++revision_;
}

const EnemyProjectilePresentation*
EnemyProjectilePresentationBridge::FindProjectile(
    uint64_t projectileId) const noexcept {
    const auto found = tracked_.find(projectileId);
    return found != tracked_.end() ? &found->second : nullptr;
}

void EnemyProjectilePresentationBridge::PushEvent(
    EnemyProjectilePresentationEvent event,
    size_t maximumEvents) {
    if (frame_.events.size() >= maximumEvents) {
        ++frame_.droppedEvents;
        return;
    }
    frame_.events.push_back(std::move(event));
}

const char* ToString(EnemyProjectilePresentationEventKind kind) noexcept {
    switch (kind) {
    case EnemyProjectilePresentationEventKind::Spawned: return "Spawned";
    case EnemyProjectilePresentationEventKind::Impacted: return "Impacted";
    case EnemyProjectilePresentationEventKind::Intercepted: return "Intercepted";
    case EnemyProjectilePresentationEventKind::Expired: return "Expired";
    }
    return "Unknown";
}
