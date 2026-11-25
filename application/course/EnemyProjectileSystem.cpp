#include "EnemyProjectileSystem.h"

#include "CourseSpawnRuntime.h"
#include "RailRearPursuitRules.h"
#include "RailGatlingAttack.h"
#include "RailGroundBatteryAttack.h"
#include "RailMothershipBattle.h"

#include <algorithm>
#include <cmath>
#include <optional>
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

void IntegrateProjectile(EnemyProjectileRuntimeState& projectile, float deltaTime,
    std::optional<float> verticalGravity) noexcept;

class EnemyProjectileFlight {
public:
    virtual ~EnemyProjectileFlight() = default;

    virtual RailVector AimAtLaunch(RailVector direction, const CourseEnemyActor&,
        EnemyProjectileRuntimeState&) const noexcept {
        return direction;
    }

    // A trajectory preview has no live steering target. Return whether a live
    // step applied guidance so the simulation can keep its existing counters.
    virtual bool Advance(EnemyProjectileRuntimeState& projectile, float deltaTime,
        const EnemyProjectileFrameInput* target) const noexcept = 0;
};

class StraightProjectileFlight final : public EnemyProjectileFlight {
    bool Advance(EnemyProjectileRuntimeState& projectile, float deltaTime,
        const EnemyProjectileFrameInput*) const noexcept override {
        IntegrateProjectile(projectile, deltaTime, std::nullopt);
        return false;
    }
};

class ArcProjectileFlight final : public EnemyProjectileFlight {
    RailVector AimAtLaunch(RailVector direction, const CourseEnemyActor& actor,
        EnemyProjectileRuntimeState& projectile) const noexcept override {
        const float flight = (std::max)(0.1f, actor.targetingState.predictedFlightSeconds);
        direction.vertical += 0.5f * projectile.arcGravity * flight * flight;
        return direction;
    }

    bool Advance(EnemyProjectileRuntimeState& projectile, float deltaTime,
        const EnemyProjectileFrameInput*) const noexcept override {
        IntegrateProjectile(projectile, deltaTime, projectile.arcGravity);
        return false;
    }
};

class HomingProjectileFlight final : public EnemyProjectileFlight {
    RailVector AimAtLaunch(RailVector direction, const CourseEnemyActor& actor,
        EnemyProjectileRuntimeState& projectile) const noexcept override {
        projectile.automaticForwardIntercept = actor.targetingState.compensatesForwardTravel;
        return direction;
    }

    bool Advance(EnemyProjectileRuntimeState& projectile, float deltaTime,
        const EnemyProjectileFrameInput* target) const noexcept override {
        const bool guided = target != nullptr && projectile.homingTurnRateRadians > 0.0f;
        if (guided) {
            const RailVector velocity{projectile.forwardSpeed, projectile.lateralSpeed, projectile.verticalSpeed};
            const float speed = std::sqrt((std::max)(0.0f, LengthSquared(velocity)));
            const RailVector desired{
                (projectile.automaticForwardIntercept ? projectile.lockedTargetDistance - projectile.spawnDistance :
                    target->playerDistance) - projectile.distanceOffset,
                target->playerLateralOffset - projectile.lateralOffset,
                target->playerVerticalOffset - projectile.verticalOffset};
            const RailVector direction = TurnTowards(NormalizeOr(velocity, {-1.0f, 0.0f, 0.0f}),
                desired, projectile.homingTurnRateRadians * deltaTime);
            projectile.forwardSpeed = direction.forward * speed;
            projectile.lateralSpeed = direction.lateral * speed;
            projectile.verticalSpeed = direction.vertical * speed;
        }
        IntegrateProjectile(projectile, deltaTime, std::nullopt);
        return guided;
    }
};

const EnemyProjectileFlight& ProjectileFlightFor(EnemyProjectileTrajectory trajectory) noexcept {
    static const StraightProjectileFlight straight;
    static const ArcProjectileFlight arc;
    static const HomingProjectileFlight homing;
    switch (trajectory) {
    case EnemyProjectileTrajectory::Direct:
    case EnemyProjectileTrajectory::Predictive: return straight;
    case EnemyProjectileTrajectory::Homing: return homing;
    case EnemyProjectileTrajectory::Arc: return arc;
    }
    return straight;
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

    // Each predictive battery barrel solves its own crossing trajectory.
    const float lane=(RailGroundBattery::IsPredictive(actor.desc.actorAssetId) ||
        RailGatling::IsWeapon(actor.desc.actorAssetId) || actor.desc.actorAssetId=="rear_fang")
        ? 0.0f : static_cast<float>(index)-centerLane;
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
    const Vector3 muzzle=ResolveEnemyProjectileMuzzleRailPosition(actor,index,launchDelaySeconds);
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
        // Predictive batteries freeze their lateral aim at commitment. Advance
        // only automatic forward travel for later rounds, so moving sideways
        // after the tell remains a valid response to the whole burst.
        const bool delayedBurst=RailGatling::IsWeapon(actor.desc.actorAssetId) &&
            launchDelaySeconds>0 && !actor.targetingState.compensatesForwardTravel;
        if (delayedBurst) {
            projectile.distanceOffset += actor.behaviorState.engagementBandVelocity * launchDelaySeconds;
        }
        projectile.previousDistanceOffset = projectile.distanceOffset;
        projectile.previousLateralOffset = projectile.lateralOffset;
        projectile.previousVerticalOffset = projectile.verticalOffset;
        const Vector3 target=aim.targetRail;
        projectile.lockedTargetDistance =
            target.z + (delayedBurst ?
                actor.targetingState.playerForwardVelocity * launchDelaySeconds : 0.0f);
        projectile.lockedTargetLateralOffset =
            target.x;
        projectile.lockedTargetVerticalOffset =
            target.y;
        RailVector direction{
            projectile.lockedTargetDistance - projectile.distanceOffset,
            projectile.lockedTargetLateralOffset - projectile.lateralOffset,
            projectile.lockedTargetVerticalOffset - projectile.verticalOffset};
        const EnemyProjectileFlight& flight = ProjectileFlightFor(definition.trajectory);
        direction = flight.AimAtLaunch(direction, actor, projectile);
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

namespace {
void IntegrateProjectile(EnemyProjectileRuntimeState& projectile, float deltaTime,
        std::optional<float> verticalGravity) noexcept {
    const float dt=(std::max)(0.0f,deltaTime);
    const int aimSalvo=projectile.definitionId==kFangAimVolleyRound?FangAimSalvo(projectile.sourceRole):-1;
    if ((projectile.sourceRole==kFangCrossfireRole || aimSalvo>=0) &&
        projectile.forwardSpeed>0.0f) {
        // Crossfire joins its warned lane early. The turning volleys arc
        // out from their firing positions before converging near the cart.
        const float remaining=(projectile.lockedTargetDistance-projectile.spawnDistance-projectile.distanceOffset)/
            projectile.forwardSpeed;
        const float alignTime=(std::clamp)(remaining,0.0f,dt);
        projectile.lateralOffset+=projectile.lateralSpeed*alignTime;
        projectile.verticalOffset+=projectile.verticalSpeed*alignTime;
        if(aimSalvo>=0 && alignTime>0) {
            const float duration=(projectile.lockedTargetDistance-projectile.spawnDistance)/projectile.forwardSpeed;
            const auto offset=[duration,aimSalvo](float timeRemaining) {
                const float progress=(std::clamp)(1.0f-timeRemaining/duration,0.0f,1.0f);
                return FangAimVolleyOffset(aimSalvo,progress);
            };
            const auto before=offset(remaining),after=offset(remaining-alignTime);
            projectile.lateralOffset+=after.x-before.x;
            projectile.verticalOffset+=after.y-before.y;
        }
        if(remaining<=dt) {
            projectile.lateralOffset=projectile.lockedTargetLateralOffset;
            projectile.verticalOffset=projectile.lockedTargetVerticalOffset;
            projectile.lateralSpeed=0;projectile.verticalSpeed=0;
        }
        projectile.distanceOffset+=projectile.forwardSpeed*dt;
        projectile.age+=dt;
        return;
    }
    RailVector velocity{projectile.forwardSpeed,projectile.lateralSpeed,projectile.verticalSpeed};
    float speed=std::sqrt((std::max)(0.0f,LengthSquared(velocity)));
    const RailVector direction=NormalizeOr(velocity,{-1.0f,0.0f,0.0f});
    const float initialSpeed=speed;
    speed = (std::clamp)(
        speed + projectile.acceleration * dt,
        0.0f,
        (std::max)(1.0f, projectile.maximumSpeed));
    velocity = Scale(direction, speed);
    if (verticalGravity) velocity.vertical -= *verticalGravity * dt;
    projectile.forwardSpeed = velocity.forward;
    projectile.lateralSpeed = velocity.lateral;
    projectile.verticalSpeed = velocity.vertical;
    float travel=speed*dt;
    if(projectile.automaticForwardIntercept && projectile.acceleration>0.0f) {
        const float accelerated=(std::min)(dt,(std::max)(0.0f,
            projectile.maximumSpeed-initialSpeed)/projectile.acceleration);
        travel=initialSpeed*accelerated+0.5f*projectile.acceleration*accelerated*accelerated+
            speed*(dt-accelerated);
    }
    projectile.distanceOffset += projectile.automaticForwardIntercept ? direction.forward*travel : projectile.forwardSpeed*dt;
    projectile.lateralOffset += projectile.automaticForwardIntercept ? direction.lateral*travel : projectile.lateralSpeed*dt;
    projectile.verticalOffset += projectile.automaticForwardIntercept ? direction.vertical*travel : projectile.verticalSpeed*dt;
    projectile.age += dt;
}
} // namespace

void AdvanceEnemyProjectileTrajectory(EnemyProjectileRuntimeState& projectile, float deltaTime) {
    const EnemyProjectileFlight& flight = ProjectileFlightFor(projectile.trajectory);
    flight.Advance(projectile, deltaTime, nullptr);
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
    const bool gatling=RailGatling::IsWeapon(actor.desc.actorAssetId);
    const bool fang=actor.desc.actorAssetId=="rear_fang";
    std::vector<EnemyProjectileRuntimeState> launches;
    launches.reserve(projectileCount);
    auto burstActor=fang?std::optional<CourseEnemyActor>(actor):std::nullopt;
    if(burstActor)burstActor->weaponMount.scheduledPredictionRequested=false;
    for(int index=0;index<projectileCount;++index) {
        const float delay=gatling ? index*RailGatling::ShotInterval : fang ? index*kFangShotInterval : 0.0f;
        auto projectile=ResolveEnemyProjectileScheduledLaunch(fang && index>0?*burstActor:actor,index,delay).projectile;
        if(!projectile.active)return 0;
        projectile.launchDelayRemaining=delay;
        projectile.active=delay<=0;
        launches.push_back(std::move(projectile));
    }
    for(auto& projectile:launches) {
        projectile.frameEndLaunch=RailGroundBattery::IsPredictive(actor.desc.actorAssetId) ||
            (actor.targetingState.compensatesForwardTravel && !RailMothership::IsCombatActor(actor.desc.actorAssetId) &&
                (projectile.trajectory==EnemyProjectileTrajectory::Predictive || projectile.trajectory==EnemyProjectileTrajectory::Homing));
        projectile.projectileId=nextProjectileId_++;
        if(nextProjectileId_==0) nextProjectileId_=1;
        projectiles.push_back(std::move(projectile));
    }
    pendingSpawned_ += (gatling || fang) ? 1u : static_cast<uint32_t>(projectileCount);
    return static_cast<uint32_t>(projectileCount);
}

void EnemyProjectileSystem::Update(
    std::vector<EnemyProjectileRuntimeState>& projectiles,
    const EnemyProjectileFrameInput& input,
    std::span<const CourseEnemyActor> owners) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    frame_ = {};
    frame_.spawnedThisFrame = pendingSpawned_;
    pendingSpawned_ = 0;
    for (EnemyProjectileRuntimeState& projectile : projectiles) {
        InitializeLegacy(projectile);
        float travelTime=dt;
        if(projectile.launchDelayRemaining>0 && !projectile.hitConsumed) {
            if(dt<=0)continue;
            const auto owner=std::find_if(owners.begin(),owners.end(),[&](const auto& actor) {
                return actor.actorId==projectile.ownerActorId && actor.desc.hitPoints>0 &&
                    (!actor.combatState.initialized || (actor.combatState.phase!=EnemyCombatPhase::Dying &&
                        actor.combatState.phase!=EnemyCombatPhase::Retired));
            });
            if(!input.scheduledLaunchesAllowed || (!owners.empty() && owner==owners.end())) {
                projectile.launchDelayRemaining=0;projectile.active=false;projectile.hitConsumed=true;
                continue;
            }
            // Subtract the wait, then integrate only the remainder. This keeps
            // the stream's spacing identical at 30, 60 and 120 Hz.
            travelTime=(std::max)(0.0f,dt-projectile.launchDelayRemaining);
            projectile.launchDelayRemaining=(std::max)(0.0f,projectile.launchDelayRemaining-dt);
            if(projectile.launchDelayRemaining>0 || dt<=0)continue;
            projectile.active=true;
            ++frame_.spawnedThisFrame;
        }
        if (!projectile.active || projectile.age >= projectile.lifetime) continue;
        ++frame_.activeProjectiles;
        projectile.previousDistanceOffset = projectile.distanceOffset;
        projectile.previousLateralOffset = projectile.lateralOffset;
        projectile.previousVerticalOffset = projectile.verticalOffset;

        if(projectile.frameEndLaunch) {
            if(dt>0)projectile.frameEndLaunch=false;
            continue;
        }

        const EnemyProjectileFlight& flight = ProjectileFlightFor(projectile.trajectory);
        if (flight.Advance(projectile, travelTime, &input)) ++frame_.homingProjectiles;
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
