#include "RailShooterHudRuntimeModel.h"
#include "RailRouteHazards.h"

#include "CourseGameplayWaveRuntimeBridge.h"
#include "EnemyEncounterReadabilityDirector.h"
#include "GrazeScoreSystem.h"
#include "RailVehicleMovementSystem.h"
#include "WeaponFireSystem.h"
#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>

namespace {
float Ratio(float value, float maximum) noexcept {
    return maximum > 0.0f
        ? (std::clamp)(value / maximum, 0.0f, 1.0f)
        : 0.0f;
}

RailShooterHudWeaponSnapshot BuildWeaponSnapshot(
    const WeaponDefinition* definition,
    const WeaponRuntimeState* state) {
    RailShooterHudWeaponSnapshot result{};
    if (definition == nullptr || state == nullptr ||
        definition->weaponId != state->weaponId) {
        return result;
    }
    result.available = true;
    result.weaponId = definition->weaponId;
    result.unlimitedAmmo = definition->magazineCapacity == 0;
    result.reloading = state->reloading;
    result.overheated = state->overheated;
    result.ammoInMagazine = state->ammoInMagazine;
    result.magazineCapacity = definition->magazineCapacity;
    result.reserveAmmo = state->reserveAmmo;
    result.ammoNormalized = result.unlimitedAmmo
        ? 1.0f
        : Ratio(static_cast<float>(state->ammoInMagazine),
                static_cast<float>(definition->magazineCapacity));
    result.heatNormalized = Ratio(state->heat, definition->heatCapacity);
    result.cooldownNormalized = Ratio(
        state->cooldownRemaining,
        definition->shotInterval);
    result.reloadNormalized = state->reloading
        ? Ratio(state->reloadRemaining, definition->reloadDuration)
        : 0.0f;
    return result;
}
} // namespace

void RailShooterHudRuntimeModel::Reset() {
    frame_ = {};
    revision_ = 0;
}

void RailShooterHudRuntimeModel::Update(
    const RailShooterHudRuntimeInput& input) {
    RailShooterHudRuntimeFrame next{};
    next.visible = input.gameplayVisible && input.session != nullptr;
    next.revision = ++revision_;

    if (input.session != nullptr) {
        const GameSessionRuntimeState& session = *input.session;
        next.sessionPhase = session.phase;
        next.gameplayActive = session.gameplaySimulationEnabled;
        next.sessionRevision = session.revision;
        next.playerHealth = session.playerHealth;
        next.maximumPlayerHealth = session.maximumPlayerHealth;
        next.playerHealthNormalized = Ratio(
            session.playerHealth,
            session.maximumPlayerHealth);
        next.courseDistance = session.courseDistance;
        next.courseLength = session.courseLength;
        next.courseProgressNormalized = Ratio(
            session.courseDistance,
            session.courseLength);
        next.score = session.score;
        next.combo = session.combo;
        next.retriesRemaining = session.retriesRemaining;
        next.completedWaves = session.completedMandatoryWaves;
        next.totalWaves = session.totalMandatoryWaves;
    }

    if (input.vehicleDefinition != nullptr && input.vehicle != nullptr &&
        input.vehicle->initialized) {
        next.vehicleRevision = input.vehicle->revision;
        next.vehicleIntegrity = input.vehicle->hitPoints;
        next.maximumVehicleIntegrity =
            input.vehicleDefinition->maximumHitPoints;
        next.vehicleIntegrityNormalized = Ratio(
            input.vehicle->hitPoints,
            input.vehicleDefinition->maximumHitPoints);
        next.speed = input.vehicle->speed;
        next.maximumSpeed = input.vehicleDefinition->maximumSpeed;
        next.speedNormalized = Ratio(
            input.vehicle->speed,
            input.vehicleDefinition->maximumSpeed);
        if (next.courseLength <= 0.0f) {
            next.courseProgressNormalized =
                (std::clamp)(input.vehicle->normalizedProgress, 0.0f, 1.0f);
        }
    }

    if (next.gameplayActive && input.spawnRuntime != nullptr &&
        input.vehicleDefinition != nullptr && input.vehicle != nullptr && input.vehicle->initialized) {
        const auto& vehicle = *input.vehicle;
        const auto& body = *input.vehicleDefinition;
        float nearestContact = 5.0f;
        for (const auto& actor : input.spawnRuntime->Obstacles()) {
            if(RailRouteHazards::IsSlab(actor) || RailRouteHazards::IsBarricade(actor))continue;
            const auto& obstacle = actor.desc;
            if (obstacle.hitPoints <= 0.0f || actor.age >= obstacle.lifetime ||
                std::abs(obstacle.lateralOffset) > obstacle.halfExtents.x + body.collisionHalfExtents.x ||
                std::abs(obstacle.verticalOffset - body.bodyVerticalOffset) >
                    obstacle.halfExtents.y + body.collisionHalfExtents.y) continue;
            const float relativeSpeed = vehicle.speed - obstacle.forwardSpeed;
            const float centreGap = obstacle.spawnDistance + obstacle.distanceOffset - vehicle.distance;
            const float extent = obstacle.halfExtents.z + body.collisionHalfExtents.z;
            if (relativeSpeed <= 0.01f || centreGap < -extent) continue;
            const float time = (std::max)(0.0f, (centreGap - extent) / relativeSpeed);
            if (time > nearestContact || time >= obstacle.lifetime - actor.age) continue;
            nearestContact = time;
            next.obstacleApproaching = true;
            next.approachingObstacleBreakable = obstacle.breakable;
            next.obstacleTimeToContact = time;
            next.approachingObstacleHealth = obstacle.hitPoints;
            next.approachingObstacleActorId = actor.actorId;
        }
    }

    next.primaryWeapon = BuildWeaponSnapshot(
        input.primaryWeaponDefinition,
        input.primaryWeapon);
    next.lockWeapon = BuildWeaponSnapshot(
        input.lockWeaponDefinition,
        input.lockWeapon);

    if (input.waves != nullptr && input.waves->bound) {
        next.completedWaves = input.waves->completedWaves;
        next.activeWaves = input.waves->activeWaves;
        next.activeEnemies = input.waves->activeActors;
        next.defeatedEnemies = input.waves->defeatedActors;
    }
    if (input.graze != nullptr) {
        next.grazeRevision = input.graze->revision;
        next.grazeChain = input.graze->chain;
        next.adrenalineNormalized =
            (std::clamp)(input.graze->adrenalineNormalized, 0.0f, 1.0f);
    }
    if (input.threat != nullptr) {
        next.threatRevision = input.threat->revision;
        next.threatBand = input.threat->band;
        next.threatNormalized =
            (std::clamp)(input.threat->threatNormalized, 0.0f, 1.0f);
        next.nearbyThreats = input.threat->nearbyThreats;
    }
    if (input.encounterReadability != nullptr) {
        const EnemyEncounterReadabilityFrame& encounter =
            *input.encounterReadability;
        next.encounterReadabilityRevision = encounter.revision;
        next.activeEnemies = encounter.truth.activeHostiles;
        next.activeTelegraphs = encounter.truth.activeTelegraphs;
        next.activeHostileProjectiles =
            encounter.truth.activeHostileProjectiles;
        next.unresolvedDefenseWindows =
            encounter.truth.unresolvedDefenseWindows;
        next.readableHostiles = encounter.readableHostiles;
        next.combatClearConfirmed = encounter.truth.safeToAnnounceClear;
        next.combatStatusText = encounter.truth.statusText;
        next.nearbyThreats = (std::max)(
            next.nearbyThreats,
            encounter.truth.activeHostiles +
                encounter.truth.activeHostileProjectiles +
                encounter.truth.unresolvedDefenseWindows);
    }
    next.maximumLocks = input.maximumLocks;
    next.lockCount = (std::min)(input.lockCount, input.maximumLocks);
    frame_ = std::move(next);
}
