#include "EnemyProjectileSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
struct RailVector final {
    float forward = 0.0f;
    float lateral = 0.0f;
    float vertical = 0.0f;
};

float LengthSquared(RailVector value) noexcept {
    return value.forward * value.forward +
        value.lateral * value.lateral +
        value.vertical * value.vertical;
}

RailVector Scale(RailVector value, float scale) noexcept {
    return {
        value.forward * scale,
        value.lateral * scale,
        value.vertical * scale};
}

RailVector NormalizeOr(RailVector value, RailVector fallback) noexcept {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

float Dot(RailVector a, RailVector b) noexcept {
    return a.forward * b.forward +
        a.lateral * b.lateral +
        a.vertical * b.vertical;
}

RailVector TurnTowards(
    RailVector current,
    RailVector desired,
    float maximumRadians) noexcept {
    current = NormalizeOr(current, {-1.0f, 0.0f, 0.0f});
    desired = NormalizeOr(desired, current);
    const float angle = std::acos((std::clamp)(Dot(current, desired), -1.0f, 1.0f));
    if (angle <= 0.00001f || maximumRadians >= angle) return desired;
    const float blend = (std::clamp)(maximumRadians / angle, 0.0f, 1.0f);
    return NormalizeOr({
        current.forward + (desired.forward - current.forward) * blend,
        current.lateral + (desired.lateral - current.lateral) * blend,
        current.vertical + (desired.vertical - current.vertical) * blend}, current);
}
} // namespace

EnemyProjectileRuntimeState ResolveEnemyProjectileLaunch(const CourseEnemyActor& actor, int index) {
    return ResolveEnemyProjectileScheduledLaunch(actor,index,0.0f).projectile;
}

EnemyProjectileLaunchSolution ResolveEnemyProjectileScheduledLaunch(
        const CourseEnemyActor& actor, int index, float launchDelaySeconds) {
    const EnemyProjectileDefinitionAsset& definition =
        actor.desc.projectileDefinition;
    const int projectileCount = (std::max)(1, actor.desc.bulletCount);
    const float centerLane = static_cast<float>(projectileCount - 1) * 0.5f;
    const bool hasTarget = actor.targetingState.solutionLocked &&
        actor.targetingState.attackIntentSequence ==
            actor.attackState.intentSequence &&
        actor.targetingState.attackTokenId == actor.attackState.tokenId;

    const float lane=static_cast<float>(index)-centerLane;
    EnemyProjectileLaunchSolution solution;
    auto& projectile = solution.projectile;
    if (!std::isfinite(launchDelaySeconds) || launchDelaySeconds < 0.0f) {
        projectile.active = false;
        return solution;
    }
    projectile.ownerActorId = actor.actorId;
    projectile.attackIntentSequence = actor.attackState.intentSequence;
    projectile.attackTokenId = actor.attackState.tokenId;
    projectile.deterministicSeed = actor.attackState.deterministicSeed ^
        (static_cast<uint64_t>(index + 1) * 0x9e3779b97f4a7c15ULL);
    projectile.definitionId = definition.id;
    projectile.sourceRole = actor.desc.role;
    projectile.trajectory = definition.trajectory;
    projectile.spawnDistance = 0.0f;
    const Vector3 muzzle=ResolveEnemyProjectileMuzzleRailPosition(actor,index);
    projectile.distanceOffset=muzzle.z;
    projectile.lateralOffset=muzzle.x;
    projectile.verticalOffset=muzzle.y;
    projectile.previousDistanceOffset = projectile.distanceOffset;
    projectile.previousLateralOffset = projectile.lateralOffset;
    projectile.previousVerticalOffset = projectile.verticalOffset;
    projectile.acceleration = definition.acceleration;
    projectile.maximumSpeed = (std::max)(
        definition.initialSpeed, definition.maximumSpeed);
    projectile.homingTurnRateRadians = definition.homingTurnRateRadians;
    projectile.arcGravity = definition.arcGravity;
    projectile.radius = definition.radius;
    projectile.lifetime = definition.lifetime;
    projectile.damage = definition.damage;
    projectile.color = definition.color;
    projectile.trailEffectId = definition.trailEffectId;
    projectile.impactEffectId = definition.impactEffectId;
    projectile.defenseResponses = definition.defenseResponses;
    projectile.shootDownHitPoints = definition.shootDownHitPoints;
    projectile.shootDownMaximumHitPoints = definition.shootDownHitPoints;
    projectile.shootDownRadiusScale = definition.shootDownRadiusScale;
    projectile.shootDownEnabled = HasDefenseResponse(
        definition.defenseResponses,
        EnemyAttackDefenseResponse::ShootDown);

    if (hasTarget) {
        const auto aim=ResolveEnemyProjectileAimSolution(actor,
            {projectile.lateralOffset,projectile.verticalOffset,projectile.distanceOffset},launchDelaySeconds);
        if(!aim.reachable) {
            projectile.initialized=true;
            projectile.active=false;
            return solution;
        }
        solution.flightSeconds = actor.targetingState.compensatesForwardTravel ? aim.flightSeconds : 0.0f;
        projectile.distanceOffset = aim.muzzleRail.z;
        projectile.lateralOffset = aim.muzzleRail.x;
        projectile.verticalOffset = aim.muzzleRail.y;
        projectile.previousDistanceOffset = projectile.distanceOffset;
        projectile.previousLateralOffset = projectile.lateralOffset;
        projectile.previousVerticalOffset = projectile.verticalOffset;
        const Vector3 target=aim.targetRail;
        projectile.lockedTargetDistance =
            target.z;
        projectile.lockedTargetLateralOffset =
            target.x;
        projectile.lockedTargetVerticalOffset =
            target.y;
        RailVector direction{
            projectile.lockedTargetDistance - projectile.distanceOffset,
            projectile.lockedTargetLateralOffset - projectile.lateralOffset,
            projectile.lockedTargetVerticalOffset - projectile.verticalOffset};
        if (definition.trajectory == EnemyProjectileTrajectory::Arc) {
            const float flight = (std::max)(
                0.1f, actor.targetingState.predictedFlightSeconds);
            direction.vertical +=
                0.5f * definition.arcGravity * flight * flight;
        }
        direction = NormalizeOr(direction, {-1.0f, 0.0f, 0.0f});
        projectile.forwardSpeed = direction.forward * definition.initialSpeed;
        projectile.lateralSpeed = direction.lateral * definition.initialSpeed +
            lane * actor.desc.bulletLateralSpreadSpeed;
        projectile.verticalSpeed = direction.vertical * definition.initialSpeed +
            std::abs(lane) * actor.desc.bulletVerticalSpreadSpeed;
    } else {
        projectile.forwardSpeed = -actor.desc.bulletSpeed;
        projectile.lateralSpeed =
            lane * actor.desc.bulletLateralSpreadSpeed;
        projectile.verticalSpeed =
            std::abs(lane) * actor.desc.bulletVerticalSpreadSpeed;
        projectile.radius = actor.desc.bulletRadius;
        projectile.lifetime = actor.desc.bulletLifetime;
        projectile.damage = actor.desc.bulletDamage;
        projectile.color = actor.desc.bulletColor;
    }
    projectile.initialized = true;
    projectile.active = true;
    return solution;
}

void AdvanceEnemyProjectileTrajectory(EnemyProjectileRuntimeState& projectile,float deltaTime) {
    const float dt=(std::max)(0.0f,deltaTime);
    RailVector velocity{projectile.forwardSpeed,projectile.lateralSpeed,projectile.verticalSpeed};
    float speed=std::sqrt((std::max)(0.0f,LengthSquared(velocity)));
    const RailVector direction=NormalizeOr(velocity,{-1.0f,0.0f,0.0f});
    speed = (std::clamp)(
        speed + projectile.acceleration * dt,
        0.0f,
        (std::max)(1.0f, projectile.maximumSpeed));
    velocity = Scale(direction, speed);
    if (projectile.trajectory == EnemyProjectileTrajectory::Arc) {
        velocity.vertical -= projectile.arcGravity * dt;
    }
    projectile.forwardSpeed = velocity.forward;
    projectile.lateralSpeed = velocity.lateral;
    projectile.verticalSpeed = velocity.vertical;
    projectile.distanceOffset += projectile.forwardSpeed * dt;
    projectile.lateralOffset += projectile.lateralSpeed * dt;
    projectile.verticalOffset += projectile.verticalSpeed * dt;
    projectile.age += dt;
}

void EnemyProjectileSystem::Reset() {
    frame_ = {};
    pendingSpawned_ = 0;
    nextProjectileId_ = 1;
    revision_ = 0;
}

void EnemyProjectileSystem::RebuildFromProjectiles(
    std::vector<EnemyProjectileRuntimeState>& projectiles) {
    frame_ = {};
    pendingSpawned_ = 0;
    nextProjectileId_ = 1;
    for (EnemyProjectileRuntimeState& projectile : projectiles) {
        InitializeLegacy(projectile);
        nextProjectileId_ = (std::max)(
            nextProjectileId_, projectile.projectileId + 1);
    }
    frame_.revision = ++revision_;
}

bool EnemyProjectileSystem::SpawnProjectile(EnemyProjectileRuntimeState projectile,
    std::vector<EnemyProjectileRuntimeState>& projectiles) {
    if (projectile.projectileId != 0 && std::any_of(projectiles.begin(), projectiles.end(),
        [&](const auto& existing) { return existing.projectileId == projectile.projectileId; })) return false;
    // Observe imported IDs before allocating, including IDs on legacy inputs.
    if (projectile.projectileId != 0) nextProjectileId_ = (std::max)(nextProjectileId_, projectile.projectileId + 1);
    if (!projectile.initialized) {
        const uint64_t requestedId = projectile.projectileId;
        InitializeLegacy(projectile);
        if (requestedId != 0) projectile.projectileId = requestedId;
    } else if (projectile.projectileId == 0) {
        projectile.projectileId = nextProjectileId_++;
    }
    projectiles.push_back(std::move(projectile));
    ++pendingSpawned_;
    return true;
}

uint32_t EnemyProjectileSystem::SpawnVolley(
    const CourseEnemyActor& actor,
    std::vector<EnemyProjectileRuntimeState>& projectiles) {
    if (actor.desc.hitPoints <= 0.0f || (actor.combatState.initialized &&
        (actor.combatState.phase == EnemyCombatPhase::Dying ||
         actor.combatState.phase == EnemyCombatPhase::Retired))) return 0;
    // Validate the entire volley before allocating an ID or emitting a barrel.
    if(!CanEnemyProjectileVolleyReachTarget(actor)) return 0;
    const int projectileCount=(std::max)(1,actor.desc.bulletCount);
    for(int index=0;index<projectileCount;++index) {
        auto projectile=ResolveEnemyProjectileLaunch(actor,index);
        projectile.projectileId=nextProjectileId_++;
        if(nextProjectileId_==0) nextProjectileId_=1;
        projectiles.push_back(std::move(projectile));
    }
    pendingSpawned_ += static_cast<uint32_t>(projectileCount);
    return static_cast<uint32_t>(projectileCount);
}

void EnemyProjectileSystem::Update(
    std::vector<EnemyProjectileRuntimeState>& projectiles,
    const EnemyProjectileFrameInput& input) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    frame_ = {};
    frame_.spawnedThisFrame = pendingSpawned_;
    pendingSpawned_ = 0;
    for (EnemyProjectileRuntimeState& projectile : projectiles) {
        InitializeLegacy(projectile);
        if (!projectile.active || projectile.age >= projectile.lifetime) continue;
        ++frame_.activeProjectiles;
        projectile.previousDistanceOffset = projectile.distanceOffset;
        projectile.previousLateralOffset = projectile.lateralOffset;
        projectile.previousVerticalOffset = projectile.verticalOffset;

        if (projectile.trajectory == EnemyProjectileTrajectory::Homing &&
            projectile.homingTurnRateRadians > 0.0f) {
            const RailVector velocity{projectile.forwardSpeed,projectile.lateralSpeed,projectile.verticalSpeed};
            const float speed=std::sqrt((std::max)(0.0f,LengthSquared(velocity)));
            RailVector direction=NormalizeOr(velocity,{-1.0f,0.0f,0.0f});
            const RailVector desired{
                input.playerDistance - projectile.distanceOffset,
                input.playerLateralOffset - projectile.lateralOffset,
                input.playerVerticalOffset - projectile.verticalOffset};
            direction = TurnTowards(
                direction,
                desired,
                projectile.homingTurnRateRadians * dt);
            ++frame_.homingProjectiles;
            projectile.forwardSpeed=direction.forward*speed;
            projectile.lateralSpeed=direction.lateral*speed;
            projectile.verticalSpeed=direction.vertical*speed;
        }
        AdvanceEnemyProjectileTrajectory(projectile,dt);
        if (projectile.age >= projectile.lifetime) {
            projectile.active = false;
            ++frame_.expiredThisFrame;
        }
    }
    frame_.revision = ++revision_;
}

void EnemyProjectileSystem::InitializeLegacy(
    EnemyProjectileRuntimeState& projectile) {
    if (projectile.initialized) return;
    projectile.projectileId = nextProjectileId_++;
    if (nextProjectileId_ == 0) nextProjectileId_ = 1;
    projectile.definitionId = "legacy_runtime";
    projectile.trajectory = EnemyProjectileTrajectory::Direct;
    projectile.previousDistanceOffset = projectile.distanceOffset;
    projectile.previousLateralOffset = projectile.lateralOffset;
    projectile.previousVerticalOffset = projectile.verticalOffset;
    projectile.maximumSpeed = (std::max)(
        1.0f,
        std::sqrt(projectile.forwardSpeed * projectile.forwardSpeed +
            projectile.lateralSpeed * projectile.lateralSpeed +
            projectile.verticalSpeed * projectile.verticalSpeed));
    projectile.shootDownMaximumHitPoints = (std::max)(
        projectile.shootDownMaximumHitPoints,
        projectile.shootDownHitPoints);
    projectile.shootDownEnabled = projectile.shootDownEnabled &&
        projectile.shootDownHitPoints > 0.0f;
    projectile.initialized = true;
    projectile.active = true;
}
