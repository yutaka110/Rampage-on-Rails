#include "EnemyTargetingSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 Unit(Vector3 v) {
    const float length = std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return length > 0.0001f ? Vector3{v.x/length,v.y/length,v.z/length} : Vector3{0,0,-1};
}
}

Vector3 ResolveTurretAimDirection(const CourseEnemyActor& actor) {
    const auto& target = actor.targetingState;
    return Unit({target.targetLateralOffset-actor.desc.lateralOffset,
        target.targetVerticalOffset-actor.desc.verticalOffset-0.1598f*actor.desc.radius,
        target.targetDistance-actor.desc.spawnDistance-actor.desc.distanceOffset});
}

Vector3 ResolveTurretMuzzleRailPosition(const CourseEnemyActor& actor, float barrelSide) {
    const Vector3 forward = ResolveTurretAimDirection(actor);
    const Vector3 right = Unit({-forward.z,0,forward.x});
    const Vector3 up{right.y*forward.z-right.z*forward.y,
        right.z*forward.x-right.x*forward.z,right.x*forward.y-right.y*forward.x};
    const float r = actor.desc.radius;
    return {actor.desc.lateralOffset+r*(forward.x*0.8272f+right.x*barrelSide*0.2538f+up.x*0.1598f),
        actor.desc.verticalOffset+r*(forward.y*0.8272f+right.y*barrelSide*0.2538f+up.y*0.1598f),
        actor.desc.spawnDistance+actor.desc.distanceOffset+r*(forward.z*0.8272f+right.z*barrelSide*0.2538f+up.z*0.1598f)};
}

void EnemyTargetingSystem::Reset() {
    frame_ = {};
    previousPlayerDistance_ = 0.0f;
    previousPlayerLateralOffset_ = 0.0f;
    previousPlayerVerticalOffset_ = 4.0f;
    hasPreviousPlayerSample_ = false;
    revision_ = 0;
}

void EnemyTargetingSystem::Update(CourseSpawnRuntime& runtime,
    const EnemyTargetingFrameInput& input) {
    runtime.UpdateEnemyTargeting(*this, input);
}

void EnemyTargetingSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyTargetingFrameInput& input) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    float forwardVelocity = 0.0f;
    float lateralVelocity = 0.0f;
    float verticalVelocity = 0.0f;
    if (hasPreviousPlayerSample_ && dt > 0.000001f) {
        forwardVelocity =
            (input.playerDistance - previousPlayerDistance_) / dt;
        lateralVelocity =
            (input.playerLateralOffset - previousPlayerLateralOffset_) / dt;
        verticalVelocity =
            (input.playerVerticalOffset - previousPlayerVerticalOffset_) / dt;
    }
    forwardVelocity = (std::clamp)(forwardVelocity, -120.0f, 120.0f);
    lateralVelocity = (std::clamp)(lateralVelocity, -40.0f, 40.0f);
    verticalVelocity = (std::clamp)(verticalVelocity, -40.0f, 40.0f);
    previousPlayerDistance_ = input.playerDistance;
    previousPlayerLateralOffset_ = input.playerLateralOffset;
    previousPlayerVerticalOffset_ = input.playerVerticalOffset;
    hasPreviousPlayerSample_ = true;

    frame_ = {};
    for (CourseEnemyActor& actor : actors) {
        const bool turret = actor.desc.meshId == "combat_turret";
        const Vector3 previousAim = actor.targetingState.turretAimDirection;
        if (turret && actor.combatState.canBeTargeted) {
            if (!actor.attackState.tokenReserved || !actor.targetingState.solutionLocked) {
                actor.targetingState.targetDistance = input.playerDistance;
                actor.targetingState.targetLateralOffset = input.playerLateralOffset;
                actor.targetingState.targetVerticalOffset = input.playerVerticalOffset;
            }
            const Vector3 desired = ResolveTurretAimDirection(actor);
            const float blend = 1.0f-std::exp(-8.0f*dt);
            actor.targetingState.turretAimDirection = Unit({
                previousAim.x+(desired.x-previousAim.x)*blend,
                previousAim.y+(desired.y-previousAim.y)*blend,
                previousAim.z+(desired.z-previousAim.z)*blend});
        }
        if (!actor.behaviorDefinition.commercialBehavior ||
            !actor.attackState.tokenReserved ||
            actor.attackState.intentSequence == 0) {
            continue;
        }
        ++frame_.activeReservations;
        EnemyTargetingRuntimeState& state = actor.targetingState;
        if (!state.solutionLocked ||
            state.attackIntentSequence != actor.attackState.intentSequence ||
            state.attackTokenId != actor.attackState.tokenId) {
            LockSolution(
                actor,
                input,
                forwardVelocity,
                lateralVelocity,
                verticalVelocity);
            ++frame_.solutionsLockedThisFrame;
        }
        if (actor.desc.projectileDefinition.trajectory ==
            EnemyProjectileTrajectory::Predictive) {
            ++frame_.predictiveSolutions;
        } else if (actor.desc.projectileDefinition.trajectory ==
                   EnemyProjectileTrajectory::Homing) {
            ++frame_.homingSolutions;
        }
    }
    frame_.revision = revision_;
}

void EnemyTargetingSystem::LockSolution(
    CourseEnemyActor& actor,
    const EnemyTargetingFrameInput& input,
    float playerForwardVelocity,
    float playerLateralVelocity,
    float playerVerticalVelocity) {
    const EnemyProjectileDefinitionAsset& definition =
        actor.desc.projectileDefinition;
    EnemyTargetingRuntimeState& state = actor.targetingState;
    const Vector3 previousAim = state.turretAimDirection;
    state = {};
    state.turretAimDirection = previousAim;
    state.attackIntentSequence = actor.attackState.intentSequence;
    state.attackTokenId = actor.attackState.tokenId;
    state.originDistance = actor.desc.spawnDistance + actor.desc.distanceOffset -
        actor.desc.radius * 1.5f;
    state.originLateralOffset = actor.desc.lateralOffset;
    state.originVerticalOffset = actor.desc.verticalOffset;
    state.playerForwardVelocity = playerForwardVelocity;
    state.playerLateralVelocity = playerLateralVelocity;
    state.playerVerticalVelocity = playerVerticalVelocity;
    const float distanceToPlayer = (std::max)(
        0.0f, state.originDistance - input.playerDistance);
    const float speed = (std::max)(1.0f, definition.initialSpeed);
    state.predictedFlightSeconds = (std::clamp)(
        distanceToPlayer / speed,
        0.0f,
        (std::max)(0.0f, definition.maximumPredictionSeconds));
    const bool predictive =
        definition.trajectory == EnemyProjectileTrajectory::Predictive ||
        definition.trajectory == EnemyProjectileTrajectory::Homing;
    const float predictionSeconds = predictive
        ? state.predictedFlightSeconds * definition.predictionScale
        : 0.0f;
    state.targetDistance = input.playerDistance +
        playerForwardVelocity * predictionSeconds;
    state.targetLateralOffset = input.playerLateralOffset +
        playerLateralVelocity * predictionSeconds;
    state.targetVerticalOffset = input.playerVerticalOffset +
        playerVerticalVelocity * predictionSeconds;
    if (actor.desc.meshId == "combat_turret") {
        const Vector3 muzzle = ResolveTurretMuzzleRailPosition(actor,0.0f);
        state.originDistance = muzzle.z;
        state.originLateralOffset = muzzle.x;
        state.originVerticalOffset = muzzle.y;
    }
    state.initialized = true;
    state.solutionLocked = true;
    state.revision = ++revision_;
}
