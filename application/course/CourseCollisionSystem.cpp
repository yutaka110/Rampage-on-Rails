#include "CourseCollisionSystem.h"
#include "../AppLogFile.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace {
bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float LengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

float Abs(float value) {
    return std::fabs(value);
}

float ActorDistance(const CourseEnemyActor& enemy) {
    return enemy.desc.spawnDistance + enemy.desc.distanceOffset;
}

float ObstacleDistance(const CourseObstacleActor& obstacle) {
    return obstacle.desc.spawnDistance + obstacle.desc.distanceOffset;
}

bool PlayerOverlapsObstacle(
    const CourseCollisionPlayerState& player,
    const CourseObstacleActor& obstacle) {
    const float distance = ObstacleDistance(obstacle);
    return Abs(player.distance - distance) <= obstacle.desc.halfExtents.z + player.radius &&
        Abs(player.lateralOffset - obstacle.desc.lateralOffset) <= obstacle.desc.halfExtents.x + player.radius &&
        Abs(player.verticalOffset - obstacle.desc.verticalOffset) <= obstacle.desc.halfExtents.y + player.radius;
}

bool PlayerOverlapsTerrainPlacement(
    const CourseCollisionPlayerState& player,
    const CourseTerrainPlacement& placement) {
    if (placement.layer != CourseTerrainLayer::GameplayCollision ||
        placement.collisionMode == CourseTerrainCollisionMode::None) {
        return false;
    }

    const float distance = placement.distance + placement.forwardOffset;
    return Abs(player.distance - distance) <= placement.scale.z + player.radius &&
        Abs(player.lateralOffset - placement.lateralOffset) <= placement.scale.x + player.radius &&
        Abs(player.verticalOffset - placement.verticalOffset) <= placement.scale.y + player.radius;
}

} // namespace

CourseCollisionSystem::CourseCollisionSystem() {
    Reset();
}

void CourseCollisionSystem::Reset() {
    player_ = {};
    playerDamageSystem_.Reset(
        player_.maximumHitPoints,
        player_.hitPoints);
    playerHitboxSystem_.Reset();
    playerNearMissSystem_.Reset();
    projectileShootDownSystem_.Reset();
    attackInterruptSystem_.Reset();
    weapon_ = {};
    lastFrameStats_ = {};
    lastShotDistance_ = 0.0f;
    lastShotLateralOffset_ = 0.0f;
    lastShotVerticalOffset_ = 0.0f;
    lastShotWorldPoint_ = {};
    lastShotWorldNormal_ = {};
    lastShotHitKind_ = RailAimHitKind::None;
    lastShotHitActorId_ = 0;
    lastShotHasWorldPoint_ = false;
    lastShotHasWorldHit_ = false;
    lastShotVisible_ = false;
    lastWeaponHitRequest_ = {};
    lastDamageResult_ = {};
    lastWeaponFeedbackResult_ = {};
    lastWeaponFireResult_ = {};
    weaponFireSystem_.Reset();
    weaponDefinitionRegistry_.SynchronizeFireSystem(weaponFireSystem_);
    weaponFeedbackSystem_.Reset();
    damageReceiver_.Reset();
}

void CourseCollisionSystem::SynchronizePlayerHitPoints(
    float hitPoints,
    float maximumHitPoints) {
    playerDamageSystem_.SynchronizeHealth(hitPoints, maximumHitPoints);
    player_.hitPoints = playerDamageSystem_.State().hitPoints;
    player_.maximumHitPoints = playerDamageSystem_.State().maximumHitPoints;
    player_.invulnerabilityTime =
        playerDamageSystem_.State().invulnerabilityRemainingSeconds;
}

bool CourseCollisionSystem::LoadWeaponDefinitions(
    const std::filesystem::path& directory,
    std::string* errorMessage) {
    return weaponDefinitionRegistry_.LoadDirectory(
        directory,
        &weaponFireSystem_,
        errorMessage);
}

WeaponDefinitionReloadReport CourseCollisionSystem::ReloadChangedWeaponDefinitions() {
    return weaponDefinitionRegistry_.ReloadChangedAssets(&weaponFireSystem_);
}

DamageResult CourseCollisionSystem::ApplyWeaponHit(
    CourseSpawnRuntime& runtime,
    const CourseAsset* course,
    const WeaponHitRequest& request) {
    lastWeaponHitRequest_ = request;
    lastDamageResult_ = damageReceiver_.Apply(runtime, course, request);
    const EnemyAttackInterruptResult interruptResult =
        attackInterruptSystem_.Submit(runtime, lastDamageResult_);
    if (interruptResult.interrupted) {
        ++lastFrameStats_.enemyAttacksInterrupted;
    }
    lastWeaponFeedbackResult_ =
        weaponFeedbackSystem_.Submit(runtime, request, lastDamageResult_);
    runtime.EnemyCombat().SubmitDamageResult(
        runtime,
        lastDamageResult_,
        lastWeaponFeedbackResult_.accepted
            ? &lastWeaponFeedbackResult_.event
            : nullptr);
    return lastDamageResult_;
}

WeaponFireResult CourseCollisionSystem::UpdateWeaponFire(const WeaponFireInput& input) {
    lastWeaponFireResult_ = weaponFireSystem_.Update(input);
    return lastWeaponFireResult_;
}

CourseCollisionFrameStats CourseCollisionSystem::Update(
    CourseSpawnRuntime& runtime,
    const CourseCollisionFrameInput& input) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    weaponFeedbackSystem_.Update(dt);
    lastFrameStats_ = {};
    projectileShootDownSystem_.BeginFrame();
    attackInterruptSystem_.BeginFrame();
    lastShotVisible_ = false;

    PlayerHitboxFrameInput hitboxInput{};
    hitboxInput.distance = input.player.distance;
    hitboxInput.lateralOffset = input.player.lateralOffset;
    hitboxInput.verticalOffset = input.player.verticalOffset;
    hitboxInput.dodgeActive = input.player.dodgeActive ||
        input.player.invulnerabilityTime > 0.0f;
    hitboxInput.invulnerable = input.player.invulnerabilityTime > 0.0f;
    hitboxInput.hurtRadiusScale = input.player.hurtRadiusScale;
    const PlayerHitboxRuntimeState& hitbox =
        playerHitboxSystem_.Update(hitboxInput);
    playerNearMissSystem_.Update(dt);
    player_.distance = hitbox.distance;
    player_.lateralOffset = hitbox.lateralOffset;
    player_.verticalOffset = hitbox.verticalOffset;
    player_.radius = hitbox.hurtRadius;
    playerDamageSystem_.SynchronizeHealth(
        input.player.hitPoints,
        input.player.maximumHitPoints);
    playerDamageSystem_.Update(dt, input.player.invulnerabilityTime);
    player_.hitPoints = playerDamageSystem_.State().hitPoints;
    player_.maximumHitPoints = playerDamageSystem_.State().maximumHitPoints;
    player_.invulnerabilityTime =
        playerDamageSystem_.State().invulnerabilityRemainingSeconds;

    weapon_.enabled = input.weapon.enabled;
    weapon_.triggerHeld = input.weapon.triggerHeld;
    weapon_.triggerPressed = input.weapon.triggerPressed;
    weapon_.triggerReleased = input.weapon.triggerReleased;
    weapon_.shotInterval = (std::max)(0.03f, input.weapon.shotInterval);
    weapon_.range = (std::max)(1.0f, input.weapon.range);
    weapon_.radius = (std::max)(0.1f, input.weapon.radius);
    weapon_.damage = (std::max)(0.0f, input.weapon.damage);
    weapon_.damageMultiplier =
        std::isfinite(input.weapon.damageMultiplier)
        ? (std::max)(0.0f, input.weapon.damageMultiplier)
        : 1.0f;
    weapon_.assistEnabled = input.weapon.assistEnabled;
    weapon_.assistLateralOffset = input.weapon.assistLateralOffset;
    weapon_.assistVerticalOffset = input.weapon.assistVerticalOffset;
    weapon_.muzzleForwardOffset = (std::max)(0.0f, input.weapon.muzzleForwardOffset);
    weapon_.tracerForwardDistance = (std::max)(4.0f, input.weapon.tracerForwardDistance);
    weapon_.muzzleRadius = (std::max)(0.05f, input.weapon.muzzleRadius);
    weapon_.tracerRadius = (std::max)(0.05f, input.weapon.tracerRadius);
    if (const WeaponDefinitionAsset* pulseCannon =
            weaponDefinitionRegistry_.Find(RailWeaponIds::PulseCannon)) {
        weapon_.shotInterval = pulseCannon->definition.shotInterval;
        weapon_.range = pulseCannon->definition.range;
        weapon_.damage = pulseCannon->definition.baseDamage;
        weapon_.radius = pulseCannon->projectileRadius;
        weapon_.muzzleForwardOffset = pulseCannon->muzzleForwardOffset;
        weapon_.tracerForwardDistance = pulseCannon->tracerForwardDistance;
        weapon_.muzzleRadius = pulseCannon->muzzleRadius;
        weapon_.tracerRadius = pulseCannon->tracerRadius;
    }

    for (const CourseBulletActor& bullet : runtime.Bullets()) {
        if (!bullet.active || bullet.age >= bullet.lifetime) {
            continue;
        }
        const PlayerProjectileContact contact =
            playerHitboxSystem_.EvaluateProjectile(bullet);
        if (contact.kind == PlayerProjectileContactKind::None) {
            continue;
        }
        if (contact.kind == PlayerProjectileContactKind::NearMiss) {
            PlayerNearMissRequest nearMiss{};
            nearMiss.projectileId = bullet.projectileId;
            nearMiss.sourceActorId = bullet.ownerActorId;
            nearMiss.attackIntentSequence = bullet.attackIntentSequence;
            nearMiss.attackTokenId = bullet.attackTokenId;
            nearMiss.trajectory = bullet.trajectory;
            nearMiss.sourceId = bullet.definitionId.empty()
                ? bullet.sourceRole
                : bullet.definitionId;
            nearMiss.closeness = contact.nearMissCloseness;
            nearMiss.surfaceSeparation = contact.surfaceSeparation;
            nearMiss.railDistance = contact.closestRailDistance;
            nearMiss.lateralOffset = contact.closestLateralOffset;
            nearMiss.verticalOffset = contact.closestVerticalOffset;
            const PlayerNearMissResult result =
                playerNearMissSystem_.Submit(nearMiss);
            if (result.accepted) {
                ++lastFrameStats_.playerNearMisses;
            }
            continue;
        }

        PlayerHitRequest request{};
        request.kind = PlayerHitKind::EnemyProjectile;
        request.sourceActorId = bullet.ownerActorId;
        request.sourceProjectileId = bullet.projectileId;
        request.attackIntentSequence = bullet.attackIntentSequence;
        request.attackTokenId = bullet.attackTokenId;
        request.sourceId = bullet.definitionId.empty()
            ? bullet.sourceRole
            : bullet.definitionId;
        request.impactEffectId = bullet.impactEffectId;
        request.rawDamage = bullet.damage;
        request.postHitInvulnerabilitySeconds = 0.65f;
        request.railDistance = hitbox.distance;
        request.lateralOffset = hitbox.lateralOffset;
        request.verticalOffset = hitbox.verticalOffset;
        if (input.railPath != nullptr) {
            // Entry into the swept hurt sphere, not the closest point (which
            // can be the center or already on the far side of a fast shot).
            const Vector3 start{bullet.previousLateralOffset - hitbox.previousLateralOffset,
                bullet.previousVerticalOffset - hitbox.previousVerticalOffset,
                bullet.spawnDistance + bullet.previousDistanceOffset - hitbox.previousDistance};
            const Vector3 end{bullet.lateralOffset - hitbox.lateralOffset,
                bullet.verticalOffset - hitbox.verticalOffset,
                bullet.spawnDistance + bullet.distanceOffset - hitbox.distance};
            const Vector3 delta{end.x-start.x,end.y-start.y,end.z-start.z};
            const float a = LengthSquared(delta);
            const float b = start.x*delta.x + start.y*delta.y + start.z*delta.z;
            const float radius = hitbox.hurtRadius + bullet.radius;
            const float c = LengthSquared(start) - radius*radius;
            float t = 0.0f;
            if (c > 0.0f && a > 0.000001f)
                t = (std::clamp)((-b-std::sqrt((std::max)(0.0f,b*b-a*c)))/a,0.0f,1.0f);
            const auto lerp = [t](float from,float to){return from+(to-from)*t;};
            request.railDistance = bullet.spawnDistance + lerp(bullet.previousDistanceOffset,bullet.distanceOffset);
            request.lateralOffset = lerp(bullet.previousLateralOffset,bullet.lateralOffset);
            request.verticalOffset = lerp(bullet.previousVerticalOffset,bullet.verticalOffset);
            const auto sample = input.railPath->Evaluate(request.railDistance);
            request.impactWorldPosition = Add(sample.position,Add(Scale(sample.right,request.lateralOffset),
                Scale(sample.up,request.verticalOffset)));
            const Vector3 outward = NormalizeOr(Add(start,Scale(delta,t)),
                NormalizeOr(Scale(delta,-1.0f),{0,0,0}));
            request.impactNormalWorld = Add(Add(Scale(sample.right,outward.x),Scale(sample.up,outward.y)),
                Scale(sample.tangent,outward.z));
            request.hasWorldImpact = Finite(request.impactWorldPosition);
        }
        const PlayerDamageResult result = SubmitPlayerHit(request);
        if (result.projectileConsumed) {
            runtime.ConsumeProjectile(bullet.projectileId);
        }
        if (!result.accepted) {
            ++lastFrameStats_.playerHitsRejected;
            continue;
        }
        lastFrameStats_.enemyBulletHits++;
        lastFrameStats_.playerDamage += result.appliedDamage;
    }

    if (!input.externalBodyCollisionAuthority) {
        for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
            if (obstacle.age >= obstacle.desc.lifetime ||
                (obstacle.desc.breakable && obstacle.desc.hitPoints <= 0.0f)) continue;
            if (!PlayerOverlapsObstacle(player_, obstacle)) {
                continue;
            }

            constexpr float kObstacleContactDamage = 24.0f;
            PlayerHitRequest request{};
            request.kind = PlayerHitKind::ObstacleContact;
            request.sourceActorId = obstacle.actorId;
            request.sourceId = obstacle.desc.id;
            request.impactEffectId = "hit_ring";
            request.rawDamage = kObstacleContactDamage;
            request.postHitInvulnerabilitySeconds = 0.80f;
            request.railDistance = player_.distance;
            request.lateralOffset = player_.lateralOffset;
            request.verticalOffset = player_.verticalOffset;
            const PlayerDamageResult result = SubmitPlayerHit(request);
            if (!result.accepted) {
                ++lastFrameStats_.playerHitsRejected;
                break;
            }
            lastFrameStats_.obstacleHits++;
            lastFrameStats_.playerDamage += result.appliedDamage;
            break;
        }

        if (input.course != nullptr) {
            for (const CourseTerrainPlacement& placement : input.course->terrainPlacements) {
                if (!PlayerOverlapsTerrainPlacement(player_, placement)) {
                    continue;
                }

                constexpr float kTerrainContactDamage = 20.0f;
                PlayerHitRequest request{};
                request.kind = PlayerHitKind::TerrainContact;
                request.sourceId = placement.editorGuid.empty()
                    ? placement.id
                    : placement.editorGuid;
                request.impactEffectId = "hit_ring";
                request.rawDamage = kTerrainContactDamage;
                request.postHitInvulnerabilitySeconds = 0.80f;
                request.railDistance = player_.distance;
                request.lateralOffset = player_.lateralOffset;
                request.verticalOffset = player_.verticalOffset;
                const PlayerDamageResult result = SubmitPlayerHit(request);
                if (!result.accepted) {
                    ++lastFrameStats_.playerHitsRejected;
                    break;
                }
                lastFrameStats_.obstacleHits++;
                lastFrameStats_.playerDamage += result.appliedDamage;
                break;
            }
        }
    }

    WeaponFireInput fireInput{};
    fireInput.weaponId = RailWeaponIds::PulseCannon;
    fireInput.deltaTime = dt;
    fireInput.enabled = weapon_.enabled;
    fireInput.triggerHeld = weapon_.triggerHeld;
    fireInput.triggerPressed = weapon_.triggerPressed;
    fireInput.triggerReleased = weapon_.triggerReleased;
    fireInput.damageMultiplier = weapon_.damageMultiplier;
    lastWeaponFireResult_ = UpdateWeaponFire(fireInput);
    weapon_.shotTimer = lastWeaponFireResult_.cooldownRemaining;
    lastFrameStats_.playerShotsFired +=
        static_cast<uint32_t>(lastWeaponFireResult_.shots.size());
    for (const WeaponShot& shot : lastWeaponFireResult_.shots) {
        FirePlayerShot(runtime, input, shot);
    }

    runtime.PruneDestroyedActors();
    LogFrameStats(lastFrameStats_);
    return lastFrameStats_;
}

PlayerDamageResult CourseCollisionSystem::SubmitPlayerHit(
    const PlayerHitRequest& request) {
    const PlayerDamageResult result = playerDamageSystem_.Submit(request);
    if (result.accepted && result.appliedDamage > 0.0f) {
        // Body contacts arrive after Update/LogFrameStats; record at the
        // shared acceptance boundary so those hits cannot disappear from logs.
        std::ofstream log = app::OpenRotatingLog("logs/player_damage.log");
        if (log) {
            log << "[PlayerDamage] sequence=" << result.sequence
                << " kind=" << ToString(request.kind)
                << " source=" << std::quoted(request.sourceId)
                << " actor=" << request.sourceActorId
                << " distance=" << request.railDistance
                << " damage=" << result.appliedDamage
                << " hpBefore=" << result.hitPointsBefore
                << " hpAfter=" << result.hitPointsAfter
                << " lethal=" << result.lethal << '\n';
        }
    }
    player_.hitPoints = playerDamageSystem_.State().hitPoints;
    player_.maximumHitPoints = playerDamageSystem_.State().maximumHitPoints;
    player_.invulnerabilityTime =
        playerDamageSystem_.State().invulnerabilityRemainingSeconds;
    return result;
}

PlayerDamageResult CourseCollisionSystem::ApplyPlayerHit(
    const PlayerHitRequest& request) {
    const PlayerDamageResult result = SubmitPlayerHit(request);
    if (result.accepted) {
        ++lastFrameStats_.obstacleHits;
        lastFrameStats_.playerDamage += result.appliedDamage;
    } else {
        ++lastFrameStats_.playerHitsRejected;
    }
    return result;
}

void CourseCollisionSystem::FirePlayerShot(
    CourseSpawnRuntime& runtime,
    const CourseCollisionFrameInput& input,
    const WeaponShot& shot) {
    const float aimLateral = input.weapon.assistEnabled
        ? input.weapon.assistLateralOffset
        : input.player.lateralOffset;
    const float aimVertical = input.weapon.assistEnabled
        ? input.weapon.assistVerticalOffset
        : input.player.verticalOffset;
    lastShotDistance_ = input.player.distance + shot.range;
    lastShotLateralOffset_ = aimLateral;
    lastShotVerticalOffset_ = aimVertical;
    lastShotWorldNormal_ = {};
    lastShotHitKind_ = RailAimHitKind::None;
    lastShotHitActorId_ = 0;
    lastShotHasWorldPoint_ = false;
    lastShotHasWorldHit_ = false;
    lastShotVisible_ = true;
    lastWeaponHitRequest_ = {};
    lastDamageResult_ = {};
    lastWeaponFeedbackResult_ = {};

    const RailAimState* aim = input.worldAim;
    if (aim == nullptr || !aim->valid || !Finite(aim->worldRayOrigin) ||
        !Finite(aim->worldRayDirection)) {
        return;
    }

    const Vector3 direction = NormalizeOr(aim->worldRayDirection, {0.0f, 0.0f, 1.0f});
    lastShotWorldPoint_ = Add(aim->worldRayOrigin, Scale(direction, shot.range));
    lastShotHasWorldPoint_ = true;
    if (input.railPath != nullptr) {
        EnemyProjectileShootDownRequest shootDownRequest{};
        shootDownRequest.shotId = shot.shotId;
        shootDownRequest.rayOrigin = aim->worldRayOrigin;
        shootDownRequest.rayDirection = direction;
        shootDownRequest.maximumDistance = shot.range;
        shootDownRequest.maximumWorldHitDistance =
            aim->hasWorldHit ? aim->aimDistance : shot.range;
        shootDownRequest.damage = shot.damage;
        const EnemyProjectileShootDownResult shootDown =
            runtime.ShootDownProjectile(
                projectileShootDownSystem_,
                *input.railPath,
                shootDownRequest);
        if (shootDown.accepted) {
            lastShotWorldPoint_ = shootDown.worldHitPoint;
            lastShotWorldNormal_ = shootDown.worldHitNormal;
            lastShotDistance_ = input.player.distance + shootDown.hitDistance;
            lastShotHasWorldHit_ = true;
            if (shootDown.destroyed) {
                ++lastFrameStats_.enemyProjectilesShotDown;
            }
            ++lastFrameStats_.playerShotWorldHits;
            return;
        }
    }
    if (!aim->hasWorldHit || aim->hitKind == RailAimHitKind::None ||
        !Finite(aim->worldAimPoint) || !Finite(aim->worldAimNormal) ||
        !std::isfinite(aim->aimDistance) || aim->aimDistance < 0.0f ||
        aim->aimDistance > shot.range + 0.001f) {
        return;
    }

    WeaponHitRequest request{};
    request.shotId = shot.shotId;
    request.targetActorId = aim->hitActorId;
    request.sourceIndex = aim->hitSourceIndex;
    request.hitKind = aim->hitKind;
    request.damageType = shot.damageType;
    request.rayOrigin = aim->worldRayOrigin;
    request.rayDirection = direction;
    request.hitPoint = aim->worldAimPoint;
    request.hitNormal = aim->worldAimNormal;
    request.hitDistance = aim->aimDistance;
    request.baseDamage = shot.damage;
    ApplyWeaponHit(runtime, input.course, request);

    if (!lastWeaponFeedbackResult_.accepted) {
        lastFrameStats_.playerShotStaleHits++;
        return;
    }

    if (aim->hitKind == RailAimHitKind::Enemy &&
        lastDamageResult_.damageApplied) {
        lastFrameStats_.playerShotEnemyHits++;
    } else if (aim->hitKind == RailAimHitKind::Obstacle &&
               lastDamageResult_.damageApplied) {
        lastFrameStats_.playerShotObstacleHits++;
    } else if (aim->hitKind == RailAimHitKind::TerrainPlacement ||
               aim->hitKind == RailAimHitKind::ProceduralTerrain) {
        lastFrameStats_.playerShotTerrainHits++;
    }

    lastFrameStats_.playerShotWorldHits++;
    const WeaponFeedbackEvent& feedback = lastWeaponFeedbackResult_.event;
    lastShotWorldPoint_ = feedback.worldPosition;
    lastShotWorldNormal_ = feedback.worldNormal;
    lastShotHitKind_ = feedback.hitKind;
    lastShotHitActorId_ = feedback.targetActorId;
    lastShotHasWorldHit_ = true;
    lastShotDistance_ = input.player.distance + aim->aimDistance;
}

void CourseCollisionSystem::AppendDebugDraw(
    ge3::debug::DebugDrawSystem& debugDraw,
    const RailPath& railPath) const {
    if (railPath.Length() <= 0.0f) {
        return;
    }

    const PlayerHitboxRuntimeState& hitbox = playerHitboxSystem_.State();
    if (!hitbox.initialized) return;
    const RailPathSample playerSample = railPath.Evaluate(hitbox.distance);
    const Vector3 playerCenter{
        playerSample.position.x + playerSample.right.x * hitbox.lateralOffset + playerSample.up.x * hitbox.verticalOffset,
        playerSample.position.y + playerSample.right.y * hitbox.lateralOffset + playerSample.up.y * hitbox.verticalOffset,
        playerSample.position.z + playerSample.right.z * hitbox.lateralOffset + playerSample.up.z * hitbox.verticalOffset,
    };
    const Vector4 playerColor =
        player_.invulnerabilityTime > 0.0f ?
            Vector4{1.0f, 0.35f, 0.15f, 1.0f} :
            Vector4{0.2f, 1.0f, 0.75f, 1.0f};
    debugDraw.AddCircle(
        playerCenter,
        playerSample.right,
        playerSample.up,
        hitbox.nearMissOuterRadius,
        {0.18f, 0.62f, 1.0f, 0.28f},
        32);
    debugDraw.AddCircle(
        playerCenter,
        playerSample.right,
        playerSample.up,
        hitbox.hurtRadius,
        playerColor,
        24);
}

void CourseCollisionSystem::LogFrameStats(const CourseCollisionFrameStats& stats) const {
    if (stats.enemyBulletHits == 0 &&
        stats.obstacleHits == 0 &&
        stats.playerNearMisses == 0 &&
        stats.playerShotWorldHits == 0 &&
        stats.playerShotStaleHits == 0 &&
        stats.playerShotEnemyHits == 0 &&
        stats.playerShotObstacleHits == 0 &&
        stats.enemyProjectilesShotDown == 0 &&
        stats.enemyAttacksInterrupted == 0) {
        return;
    }

    std::ostringstream line;
    line << "[CourseCollision] playerHp=" << player_.hitPoints
         << " damage=" << stats.playerDamage
         << " enemyBulletHits=" << stats.enemyBulletHits
         << " nearMisses=" << stats.playerNearMisses
         << " obstacleHits=" << stats.obstacleHits
         << " shots=" << stats.playerShotsFired
         << " worldShotHits=" << stats.playerShotWorldHits
         << " enemyShotHits=" << stats.playerShotEnemyHits
         << " obstacleShotHits=" << stats.playerShotObstacleHits
         << " terrainShotHits=" << stats.playerShotTerrainHits
         << " staleShotHits=" << stats.playerShotStaleHits
         << " projectilesShotDown=" << stats.enemyProjectilesShotDown
         << " attacksInterrupted=" << stats.enemyAttacksInterrupted
         << "\n";
    OutputDebugStringA(line.str().c_str());
    std::ofstream log = app::OpenRotatingLog("logs/course_collision.log");
    if (log) {
        log << line.str();
    }
}
