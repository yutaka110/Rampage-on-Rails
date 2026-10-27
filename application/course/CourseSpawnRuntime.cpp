#include "RailTurretGeometry.h"
#include "CourseSpawnRuntime.h"
#include "../diagnostics/DronePerformanceProfile.h"
#include "RailWorldRaycast.h"
#include "EnemyAttackInterruptSystem.h"
#include "EnemyProjectileShootDownSystem.h"
#include "WeaponDamageSystem.h"
#include <unordered_set>
#include <initializer_list>

#include "../EffectRuntime.h"
#include "../terrain/TerrainCollisionWorld.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr float kMinimumEnemyCartClearance = 18.0f;
constexpr float kMinimumEnemyCameraClearance = 12.0f;
constexpr float kEnemyPresentationClearancePadding = 2.0f;

bool FiniteValues(std::initializer_list<float> values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

bool ValidEnemyDescription(const CourseEnemyActorDesc& d) {
    return FiniteValues({d.spawnDistance, d.distanceOffset, d.lateralOffset, d.verticalOffset,
        d.forwardSpeed, d.radius, d.lifetime, d.hitPoints, d.fireInterval, d.firstShotDelay,
        d.bulletSpeed, d.bulletLateralSpreadSpeed, d.bulletVerticalSpreadSpeed, d.bulletRadius,
        d.bulletLifetime, d.bulletDamage, d.color.x, d.color.y, d.color.z, d.color.w,
        d.bulletColor.x, d.bulletColor.y, d.bulletColor.z, d.bulletColor.w,
        d.localRotation.x, d.localRotation.y, d.localRotation.z,
        d.localScale.x, d.localScale.y, d.localScale.z, d.stationaryActivationDistance}) &&
        d.stationaryActivationDistance >= -1.0f &&
        d.hitPoints >= 0.0f && d.lifetime > 0.0f && d.radius > 0.0f &&
        d.entrySource<=EnemyEntrySource::SideOpening;
}

bool ValidObstacleDescription(const CourseObstacleActorDesc& d) {
    return FiniteValues({d.spawnDistance, d.distanceOffset, d.lateralOffset, d.verticalOffset,
        d.forwardSpeed, d.lifetime, d.hitPoints, d.halfExtents.x, d.halfExtents.y, d.halfExtents.z,
        d.color.x, d.color.y, d.color.z, d.color.w}) && d.hitPoints >= 0.0f && d.lifetime > 0.0f &&
        d.halfExtents.x > 0.0f && d.halfExtents.y > 0.0f && d.halfExtents.z > 0.0f;
}

bool ValidProjectile(const CourseBulletActor& p) {
    return p.projectileId != UINT64_MAX && FiniteValues({p.spawnDistance, p.distanceOffset,
        p.lateralOffset, p.verticalOffset, p.previousDistanceOffset, p.previousLateralOffset,
        p.previousVerticalOffset, p.forwardSpeed, p.lateralSpeed, p.verticalSpeed, p.acceleration,
        p.maximumSpeed, p.homingTurnRateRadians, p.arcGravity, p.radius, p.lifetime, p.age, p.damage,
        p.shootDownHitPoints, p.shootDownMaximumHitPoints, p.shootDownRadiusScale,
        p.lockedTargetDistance, p.lockedTargetLateralOffset, p.lockedTargetVerticalOffset,
        p.color.x, p.color.y, p.color.z, p.color.w}) && p.radius > 0.0f && p.lifetime > 0.0f &&
        p.age >= 0.0f && p.damage >= 0.0f && p.maximumSpeed >= 0.0f &&
        p.homingTurnRateRadians >= 0.0f && p.shootDownHitPoints >= 0.0f &&
        p.shootDownMaximumHitPoints >= p.shootDownHitPoints && p.shootDownRadiusScale > 0.0f;
}

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
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

Vector4 FadeColor(Vector4 color, float age, float lifetime, float floorAlpha = 0.20f) {
    const float t = lifetime > 0.0f ? (std::clamp)(age / lifetime, 0.0f, 1.0f) : 1.0f;
    color.w *= (std::max)(floorAlpha, 1.0f - t);
    return color;
}

bool RoleContains(const std::string& role, const char* token) {
    return role.find(token) != std::string::npos;
}

CourseEnemyFirePattern PatternForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return CourseEnemyFirePattern::BossArc;
    }
    if (RoleContains(role, "turret") || RoleContains(role, "crossfire")) {
        return CourseEnemyFirePattern::Spread;
    }
    if (RoleContains(role, "chase") || RoleContains(role, "pursuit")) {
        return CourseEnemyFirePattern::Twin;
    }
    return CourseEnemyFirePattern::Single;
}
} // namespace

bool CourseEnemyFireSafetySettings::Validate(std::string* errorMessage) const {
    const bool valid = std::isfinite(minForwardDistance) && minForwardDistance >= 0.0f &&
        std::isfinite(maxForwardDistance) && maxForwardDistance >= minForwardDistance &&
        std::isfinite(minVisibleBeforeFire) && minVisibleBeforeFire >= 0.0f &&
        std::isfinite(blockedRetryDelay) && blockedRetryDelay >= 0.0f;
    if (errorMessage != nullptr) *errorMessage = valid ? "" :
        "Enemy fire safety requires finite nonnegative distances/durations and min distance <= max distance.";
    return valid;
}

bool CourseSpawnRuntime::ConfigureFireSafety(
    const CourseEnemyFireSafetySettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    fireSafetySettings_ = settings;
    return true;
}

uint32_t CourseSpawnRuntime::CommitEnemyVolley(
    uint32_t actorId, uint64_t intentSequence, uint64_t tokenId) {
    for (CourseEnemyActor& actor : enemies_) {
        if (actor.actorId != actorId) continue;
        if (!actor.fireSafetyAllowed || !actor.attackState.tokenReserved ||
            actor.attackState.phase != EnemyAttackRuntimePhase::Executing ||
            intentSequence == 0 || tokenId == 0 ||
            actor.attackState.intentSequence != intentSequence ||
            actor.attackState.tokenId != tokenId ||
            actor.bulletsEmittedThisFrame != 0 ||
            !CanEnemyProjectileVolleyReachTarget(actor)) return 0;
        const uint32_t emitted = EmitEnemyBullets(actor);
        actor.bulletsEmittedThisFrame += emitted;
        if (emitted != 0) ++actor.fireSequence;
        return emitted;
    }
    return 0;
}

bool CourseSpawnRuntime::ConsumeProjectile(uint64_t projectileId) {
    for (CourseBulletActor& bullet : bullets_) {
        if (bullet.projectileId != projectileId) continue;
        if (!bullet.active || bullet.hitConsumed) return false;
        bullet.age = bullet.lifetime;
        bullet.active = false;
        bullet.hitConsumed = true;
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::SpawnProjectile(CourseBulletActor projectile, std::string* errorMessage) {
    const bool accepted = ValidProjectile(projectile) &&
        enemyProjectileSystem_.SpawnProjectile(std::move(projectile), bullets_);
    if (errorMessage != nullptr) *errorMessage = accepted ? "" :
        "Projectile values are invalid or its ID already exists.";
    return accepted;
}

EnemyProjectileShootDownResult CourseSpawnRuntime::ShootDownProjectile(
    EnemyProjectileShootDownSystem& system, const RailPath& railPath,
    const EnemyProjectileShootDownRequest& request) {
    return system.Submit(bullets_, railPath, request);
}

void CourseSpawnRuntime::ClearProjectiles() {
    bullets_.clear();
    // Preserve the ID allocator so an old damage-history ID is never reused.
}

void CourseSpawnRuntime::SetEnemyScreenPresence(
    uint32_t actorId, bool evaluated, bool attackAllowed) {
    for (CourseEnemyActor& actor : enemies_) if (actor.actorId == actorId) {
        actor.screenPresenceEvaluated = evaluated;
        actor.screenPresenceAttackAllowed = attackAllowed;
        return;
    }
}

void CourseSpawnRuntime::SetEnemyEncounterPacing(
    uint32_t actorId, bool evaluated, bool attackAllowed) {
    for (CourseEnemyActor& actor : enemies_) if (actor.actorId == actorId) {
        actor.encounterPacingEvaluated = evaluated;
        actor.encounterPacingAttackAllowed = attackAllowed;
        return;
    }
}

void CourseSpawnRuntime::ResetEnemyScreenPresence() {
    for (CourseEnemyActor& actor : enemies_) {
        actor.screenPresenceEvaluated = false;
        actor.screenPresenceAttackAllowed = true;
    }
}

void CourseSpawnRuntime::ResetEnemyEncounterPacing() {
    for (CourseEnemyActor& actor : enemies_) {
        actor.encounterPacingEvaluated = false;
        actor.encounterPacingAttackAllowed = true;
    }
}

bool CourseSpawnRuntime::SetEnemyRailPose(uint32_t actorId, float distanceOffset,
    float lateralOffset, float verticalOffset, float forwardSpeed) {
    if (!std::isfinite(distanceOffset) || !std::isfinite(lateralOffset) ||
        !std::isfinite(verticalOffset) || !std::isfinite(forwardSpeed)) return false;
    for (CourseEnemyActor& actor : enemies_) if (actor.actorId == actorId) {
        actor.desc.distanceOffset = distanceOffset;
        actor.desc.lateralOffset = lateralOffset;
        actor.desc.verticalOffset = verticalOffset;
        actor.desc.forwardSpeed = forwardSpeed;
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::SynchronizePreviewEnemy(uint32_t actorId, CourseEnemyActorDesc desc) {
    // Preview authoring can change a description, but never a gameplay actor's HP.
    for (CourseEnemyActor& actor : enemies_) if (actor.actorId == actorId && actor.desc.previewOnly) {
        if (!ValidEnemyDescription(desc)) return false;
        // Identity is fixed at spawn, even while authoring updates the preview pose.
        desc.sourcePlacementGuid = actor.desc.sourcePlacementGuid;
        desc.waveId = actor.desc.waveId;
        desc.previewOnly = true;
        desc.hitPoints = actor.desc.hitPoints;
        actor.desc = std::move(desc);
        return true;
    }
    return false;
}

void CourseSpawnRuntime::RetireEnemies(std::span<const uint32_t> actorIds, bool playAuthoredExit) {
    const std::unordered_set<uint32_t> ids(actorIds.begin(), actorIds.end());
    std::erase_if(enemies_, [&](CourseEnemyActor& actor) {
        if (!ids.contains(actor.actorId)) return false;
        if (playAuthoredExit && actor.HoldsCombatPositionUntilResolved() &&
            actor.desc.hitPoints > 0.0f) return false;
        const std::string formationId = !actor.desc.formationDefinition.definitionId.empty()
            ? actor.desc.formationDefinition.definitionId : actor.desc.waveId;
        const bool hasAuthoredExit = !actor.desc.formationDefinition.definitionId.empty() ||
            enemyFormationSystem_.FindDefinition(formationId) != nullptr;
        if (!playAuthoredExit || !hasAuthoredExit) return true;
        actor.entranceExitState.exitRequested = true;
        actor.entranceExitState.attackSuppressed = true;
        actor.entranceExitState.targetable = false;
        if (formationId.empty()) enemyEntranceExitDirector_.RequestActorExit(actor.actorId);
        else enemyEntranceExitDirector_.RequestFormationExit(formationId);
        return false;
    });
    // Both immediate removal and animated retirement stop owned projectiles.
    std::erase_if(bullets_, [&](const CourseBulletActor& bullet) {
        if (!ids.contains(bullet.ownerActorId)) return false;
        if (playAuthoredExit) {
            const auto owner = std::find_if(enemies_.begin(), enemies_.end(), [&](const auto& actor) {
                return actor.actorId == bullet.ownerActorId;
            });
            if (owner != enemies_.end() && owner->HoldsCombatPositionUntilResolved() &&
                owner->desc.hitPoints > 0.0f) return false;
        }
        return true;
    });
}

void CourseSpawnRuntime::ClearEnemies() {
    std::vector<uint32_t> ids;
    for (const CourseEnemyActor& actor : enemies_) ids.push_back(actor.actorId);
    RetireEnemies(ids, false);
}

void CourseSpawnRuntime::ClearObstacles() { obstacles_.clear(); }

size_t CourseSpawnRuntime::ActiveEnemyCount() const {
    return static_cast<size_t>(std::count_if(enemies_.begin(),enemies_.end(),
        [](const auto& actor){return !actor.stationaryDormant;}));
}

void CourseSpawnRuntime::Reset() {
    enemies_.clear();
    bullets_.clear();
    obstacles_.clear();
    vfxCues_.clear();
    fireSafetyStats_ = {};
    enemyCombatSystem_.Reset();
    enemyBehaviorSystem_.Reset();
    enemyAttackCoordinator_.Reset();
    enemyAttackExecutionSystem_.Reset();
    enemyTargetingSystem_.Reset();
    enemyProjectileSystem_.Reset();
    enemyFormationSystem_.Reset();
    enemyEntranceExitDirector_.Reset();
    nextActorId_ = 1;
}

CourseSpawnRuntimeCheckpoint CourseSpawnRuntime::CaptureCheckpoint() const {
    CourseSpawnRuntimeCheckpoint checkpoint{};
    checkpoint.enemies = enemies_;
    checkpoint.bullets = bullets_;
    checkpoint.obstacles = obstacles_;
    checkpoint.nextActorId = nextActorId_;
    return checkpoint;
}

bool CourseSpawnRuntime::ValidateCheckpoint(
    const CourseSpawnRuntimeCheckpoint& checkpoint, bool restoreProjectiles, std::string* errorMessage) {
    // Validate the entire candidate before publishing any part of it.
    std::unordered_set<uint32_t> actorIds;
    const auto validId = [&](uint32_t id) {
        return id != 0 && id < checkpoint.nextActorId && actorIds.insert(id).second;
    };
    bool valid = checkpoint.nextActorId != 0;
    for (const CourseEnemyActor& actor : checkpoint.enemies) {
        valid = valid && validId(actor.actorId) && ValidEnemyDescription(actor.desc) &&
            FiniteValues({actor.age, actor.fireTimer, actor.fireVisibleTime}) && actor.age >= 0.0f &&
            ValidateRailEnemyIngress(actor.terrainIngress) &&
            (!actor.carrierDeployment.initialized ||
                (FiniteValues({actor.carrierDeployment.elapsed,actor.carrierDeployment.delay,
                    actor.carrierDeployment.launchDistance,actor.carrierDeployment.launchLateral,
                    actor.carrierDeployment.launchVertical,actor.carrierDeployment.forward,
                    actor.carrierDeployment.lateral,actor.carrierDeployment.vertical}) &&
                 actor.carrierDeployment.elapsed>=0.0f && actor.carrierDeployment.delay>=0.0f)) &&
            (!actor.combatState.initialized ||
                (FiniteValues({actor.combatState.currentHitPoints, actor.combatDefinition.maximumHitPoints}) &&
                 actor.combatDefinition.maximumHitPoints > 0.0f && actor.combatState.currentHitPoints >= 0.0f &&
                 actor.combatState.currentHitPoints <= actor.combatDefinition.maximumHitPoints &&
                 std::abs(actor.desc.hitPoints - actor.combatState.currentHitPoints) < 0.001f));
    }
    for (const CourseObstacleActor& actor : checkpoint.obstacles) {
        valid = valid && validId(actor.actorId) && ValidObstacleDescription(actor.desc) &&
            std::isfinite(actor.age) && actor.age >= 0.0f;
    }
    std::unordered_set<uint64_t> projectileIds;
    if (restoreProjectiles) for (const CourseBulletActor& bullet : checkpoint.bullets) {
        valid = valid && ValidProjectile(bullet) && bullet.projectileId != 0 &&
            projectileIds.insert(bullet.projectileId).second;
    }
    if (!valid) {
        if (errorMessage != nullptr) *errorMessage = "Spawn checkpoint has invalid IDs, geometry, health or projectile state.";
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool CourseSpawnRuntime::RestoreCheckpoint(
    const CourseSpawnRuntimeCheckpoint& checkpoint,
    bool restoreProjectiles,
    std::string* errorMessage) {
    if (!ValidateCheckpoint(checkpoint, restoreProjectiles, errorMessage)) return false;
    enemies_ = checkpoint.enemies;
    bullets_ = restoreProjectiles
        ? checkpoint.bullets
        : std::vector<CourseBulletActor>{};
    obstacles_ = checkpoint.obstacles;
    vfxCues_.clear();
    fireSafetyStats_ = {};
    enemyCombatSystem_.Reset();
    enemyBehaviorSystem_.Reset();
    enemyAttackCoordinator_.Reset();
    enemyAttackCoordinator_.RebuildFromRuntime(*this);
    enemyAttackExecutionSystem_.Reset();
    enemyTargetingSystem_.Reset();
    enemyProjectileSystem_.Reset();
    enemyProjectileSystem_.RebuildFromProjectiles(bullets_);
    enemyFormationSystem_.Reset();
    enemyEntranceExitDirector_.Reset();
    nextActorId_ = (std::max)(1u, checkpoint.nextActorId);
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void CourseSpawnRuntime::Update(float deltaTime) {
    CourseEnemyFireSafetyFrameInput safetyInput{};
    safetyInput.deltaTime = deltaTime;
    Update(deltaTime, safetyInput);
}

void CourseSpawnRuntime::UpdateDefeatPresentation(float deltaTime, CourseEnemyFireSafetyFrameInput safetyInput) {
    if(!std::isfinite(deltaTime) || deltaTime<=0) return;
    for(auto& enemy:enemies_) enemy.desc.suppressFire=true;
    safetyInput.cameraAllowsEnemyFire=false;
    safetyInput.deltaTime=deltaTime;
    Update(deltaTime,safetyInput);
}

void CourseSpawnRuntime::Update(float deltaTime, const CourseEnemyFireSafetyFrameInput& safetyInput) {
    const drone_perf::Scope profile(drone_perf::Stage::Spawn);
    const float dt = (std::max)(0.0f, deltaTime);
    fireSafetyStats_ = {};
    for(auto& enemy:enemies_) {
        const bool wasDormant=enemy.stationaryDormant;
        enemy.stationaryDormant=enemy.desc.stationaryActivationDistance>=0 &&
            safetyInput.playerDistance<enemy.desc.stationaryActivationDistance;
        if(enemy.stationaryDormant || wasDormant) {
            auto& staging=enemy.entranceExitState;
            staging.initialized=true;staging.phase=EnemyEntranceExitPhase::Active;
            staging.presentationAlpha=staging.presentationScale=1.0f;
            staging.targetable=!enemy.stationaryDormant && enemy.groundPlacementResolved;
            staging.attackSuppressed=!staging.targetable;
        }
    }

    UpdateCarrierDeployments(dt, safetyInput.playerDistance, safetyInput.playerSpeed);

    // Additive staging is removed in reverse order before Behavior writes the
    // new base pose. This prevents cumulative drift in long-lived formations.
    enemyEntranceExitDirector_.BeginFrame(*this);
    enemyFormationSystem_.BeginFrame(*this);
    UpdateTerrainIngresses(dt,safetyInput);

    const auto placeCarrierWeapons = [&]() {
        for (auto& actor : enemies_) {
            if (RailMothership::PartIndex(actor.desc.actorAssetId)<0 || actor.desc.hitPoints<=0) continue;
            auto& state=actor.behaviorState;
            actor.desc.distanceOffset=safetyInput.playerDistance+RailMothership::PartForwardDistance-actor.desc.spawnDistance;
            state.integratedForwardOffset=actor.desc.distanceOffset-state.authoredForwardOffset-state.behaviorForwardOffset;
            state.engagementBandForwardDistance=RailMothership::PartForwardDistance;
            state.engagementBandVelocity=(std::max)(0.0f,safetyInput.playerSpeed);
            actor.desc.lateralOffset=state.authoredLateralOffset;
            actor.desc.verticalOffset=state.authoredVerticalOffset;
            state.behaviorLateralOffset=state.behaviorVerticalOffset=state.safetyLateralOffset=0;
        }
    };
    // Both weapons belong to one pursuing hull. Authored spawn distance or
    // independent engagement smoothing must never strand either out of range.
    placeCarrierWeapons();

    EnemyCombatFrameInput combatInput{};
    combatInput.deltaTime = dt;
    combatInput.playerDistance = safetyInput.playerDistance;
    enemyCombatSystem_.Update(*this, combatInput);
    for (CourseEnemyActor& enemy : enemies_) {
        if(enemy.desc.actorAssetId=="short_mothership_core") {
            const bool damaged=enemy.desc.hitPoints<enemy.combatDefinition.maximumHitPoints*0.5f;
            enemy.desc.bulletCount=damaged?5:3;
            enemy.desc.bulletLateralSpreadSpeed=damaged?2.25f:2.8f;
        }
        if(enemy.desc.actorAssetId=="short_turret") {
            // A fixed emplacement must not be chased by the engagement band,
            // launched from the carrier, or left alive behind the camera.
            if(safetyInput.playerDistance>enemy.desc.spawnDistance+enemy.desc.distanceOffset+35.0f) {
                enemy.entranceExitState.initialized=true;
                enemy.entranceExitState.exitComplete=true;
                enemy.entranceExitState.presentationAlpha=0.0f;
                enemy.entranceExitState.targetable=false;
                enemy.entranceExitState.attackSuppressed=true;
                InvalidateEnemyAttackWarning(enemy.actorId);
                continue;
            }
            if(!enemy.groundPlacementResolved && safetyInput.railPath && safetyInput.terrainSettings) {
                auto anchor=safetyInput.railPath->Evaluate(enemy.desc.spawnDistance+enemy.desc.distanceOffset);
                anchor.position=Add(anchor.position,Scale(anchor.right,enemy.desc.lateralOffset));
                const auto geometry=RailTurret::ForMesh(enemy.desc.meshId);
                const float footprint=enemy.desc.radius*geometry.halfFootprint;
                float support=-1000.0f;bool placed=false;
                if(safetyInput.groundSupportQuery) {
                    placed=safetyInput.groundSupportQuery(anchor,footprint,support);
                } else {
                    // Headless/editor fallback still checks the whole footprint.
                    placed=true;
                    for(float x:{-footprint,0.0f,footprint}) for(float z:{-footprint,0.0f,footprint}) {
                        RailAimState down;down.valid=true;down.maxDistance=100;
                        down.worldRayOrigin=Add(anchor.position,Add(Scale(anchor.up,30),
                            Add(Scale(anchor.right,x),Scale(anchor.tangent,z))));
                        down.worldRayDirection=Scale(anchor.up,-1);
                        RailWorldRaycastInput ground;ground.aim=&down;ground.railPath=safetyInput.railPath;
                        ground.course=safetyInput.course;ground.terrainSettings=safetyInput.terrainSettings;
                        ground.terrainEdits=safetyInput.terrainEdits;ground.terrainPreview=safetyInput.terrainPreview;
                        ground.terrainCollision=safetyInput.terrainCollision;ground.playerDistance=safetyInput.playerDistance;
                        const auto hit=RailWorldRaycast::Query(ground);
                        if(!hit.hit || hit.normal.x*anchor.up.x+hit.normal.y*anchor.up.y+hit.normal.z*anchor.up.z<0.35f) {placed=false;continue;}
                        const auto delta=Vector3{hit.position.x-anchor.position.x,hit.position.y-anchor.position.y,hit.position.z-anchor.position.z};
                        support=(std::max)(support,delta.x*anchor.up.x+delta.y*anchor.up.y+delta.z*anchor.up.z);
                    }
                }
                auto& staging=enemy.entranceExitState;
                staging.initialized=true;staging.phase=EnemyEntranceExitPhase::Active;
                staging.targetable=placed;staging.attackSuppressed=!placed;staging.presentationAlpha=placed?1.0f:0.0f;
                if(!placed) { enemy.fireEnvironmentReady=false;InvalidateEnemyAttackWarning(enemy.actorId);continue; }
                enemy.groundPlacementResolved=true;
                // Distant and near terrain tessellation can differ slightly.
                // The deep foundation skirt covers this small shoulder margin
                // without moving the tower when its terrain LOD changes.
                const float vertical=support-geometry.groundFootY*enemy.desc.radius+0.30f;
                enemy.desc.verticalOffset=vertical;
                enemy.behaviorState.authoredVerticalOffset=vertical;
            }
        }
        if(enemy.stationaryDormant) {
            enemy.entranceExitState.targetable=false;
            enemy.entranceExitState.attackSuppressed=true;
            enemy.fireEnvironmentReady=enemy.fireSafetyAllowed=false;
            enemy.fireVisibleTime=0;
            InvalidateEnemyAttackWarning(enemy.actorId);
            continue;
        }
        enemy.fireEnvironmentReady = UpdateEnemyFireEnvironment(enemy, safetyInput, dt);
        if (!enemy.fireEnvironmentReady) {
            InvalidateEnemyAttackWarning(enemy.actorId);
        }
    }
    EnemyBehaviorFrameInput behaviorInput{};
    behaviorInput.deltaTime = dt;
    behaviorInput.playerDistance = safetyInput.playerDistance;
    enemyBehaviorSystem_.Update(*this, behaviorInput);
    enemyFormationSystem_.Update(*this, dt, safetyInput.railPath, &safetyInput);
    enemyEntranceExitDirector_.Update(*this, dt);
    placeCarrierWeapons();
    // Check the final staged pose, not just Behavior's pre-formation position.
    EnforceEnemyEngagementClearance(safetyInput);
    fireSafetyStats_ = {};

    for (CourseEnemyActor& enemy : enemies_) {
        enemy.bulletsEmittedThisFrame = 0;
        if(enemy.stationaryDormant) continue;
        ++fireSafetyStats_.activeEnemies;
        enemy.age += dt;
        const bool behaviorDriven = enemy.behaviorState.initialized &&
            enemy.behaviorDefinition.commercialBehavior;
        if (!behaviorDriven) {
            enemy.desc.distanceOffset += enemy.desc.forwardSpeed * dt;
            enemy.fireTimer -= dt;
        }
        const bool canFire = CanEnemyFire(enemy, safetyInput, 0.0f);
        while (!behaviorDriven && enemy.fireTimer <= 0.0f &&
               enemy.age < enemy.desc.lifetime) {
            if (!canFire) {
                enemy.fireTimer = (std::max)(enemy.fireTimer, fireSafetySettings_.blockedRetryDelay);
                break;
            }
            const uint32_t emitted = EmitEnemyBullets(enemy);
            fireSafetyStats_.bulletsEmitted += emitted;
            enemy.bulletsEmittedThisFrame += emitted;
            if (emitted > 0) {
                ++enemy.fireSequence;
            }
            enemy.fireTimer += (std::max)(0.08f, enemy.desc.fireInterval);
        }
    }

    enemyAttackCoordinator_.Update(*this, enemyBehaviorSystem_.Frame(), dt);
    EnemyTargetingFrameInput targetingInput{};
    targetingInput.travelPredictionInput = safetyInput.travelPredictionInput;
    targetingInput.railPath = safetyInput.railPath;
    targetingInput.cameraPosition = safetyInput.cameraPosition;
    targetingInput.hasCameraPosition = safetyInput.hasCameraPosition;
    targetingInput.presentationSettings = safetyInput.presentationSettings;
    targetingInput.readability = safetyInput.readability;
    targetingInput.deltaTime = dt;
    targetingInput.playerDistance = safetyInput.playerDistance;
    targetingInput.playerLateralOffset = safetyInput.playerLateralOffset;
    targetingInput.playerVerticalOffset = safetyInput.playerVerticalOffset;
    enemyTargetingSystem_.Update(*this, targetingInput);
    enemyAttackExecutionSystem_.Update(
        *this, enemyAttackCoordinator_, enemyBehaviorSystem_);
    fireSafetyStats_.bulletsEmitted +=
        enemyAttackExecutionSystem_.Frame().emittedProjectiles;

    EnemyProjectileFrameInput projectileInput{};
    projectileInput.deltaTime = dt;
    projectileInput.playerDistance = safetyInput.playerDistance;
    projectileInput.playerLateralOffset = safetyInput.playerLateralOffset;
    projectileInput.playerVerticalOffset = safetyInput.playerVerticalOffset;
    enemyProjectileSystem_.Update(bullets_, projectileInput);

    for (CourseObstacleActor& obstacle : obstacles_) {
        obstacle.age += dt;
        obstacle.desc.distanceOffset += obstacle.desc.forwardSpeed * dt;
    }

    for (CourseVfxCue& cue : vfxCues_) {
        cue.age += dt;
    }

    PruneDestroyedActors();
}

void CourseSpawnRuntime::EnforceEnemyEngagementClearance(
    const CourseEnemyFireSafetyFrameInput& input) {
    const drone_perf::Scope profile(drone_perf::Stage::Clearance);
    const auto dot = [](const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const auto subtract = [](const Vector3& a, const Vector3& b) {
        return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    for (CourseEnemyActor& actor : enemies_) {
        const auto& definition = actor.behaviorDefinition;
        auto& behavior = actor.behaviorState;
        auto& staging = actor.entranceExitState;
        if (actor.startupFlightControlled || !definition.maintainForwardEngagementBand ||
            actor.desc.hitPoints <= 0.0f ||
            actor.combatState.phase == EnemyCombatPhase::Dying ||
            actor.combatState.phase == EnemyCombatPhase::Retired) continue;

        // Hard floor survives sudden acceleration and additive staging. The
        // correction is also stored in Behavior so BeginFrame cannot undo it.
        const float forward = actor.desc.spawnDistance + actor.desc.distanceOffset -
            input.playerDistance;
        const float correction = (std::max)(
            0.0f, definition.engagementBandMinimumForwardDistance - forward);
        actor.desc.distanceOffset += correction;
        behavior.integratedForwardOffset += correction;
        behavior.engagementBandForwardDistance = forward + correction;

        bool unsafeSpatialPose = false;
        if (input.railPath != nullptr && input.railPath->Length() > 0.0f) {
            const RailPath& rail = *input.railPath;
            const Vector3 position = ResolveRailLocal(rail, actor.desc.spawnDistance,
                actor.desc.distanceOffset, actor.desc.lateralOffset, actor.desc.verticalOffset);
            const Vector3 cart = ResolveRailLocal(rail, input.playerDistance, 0.0f,
                input.playerLateralOffset, input.playerVerticalOffset);
            const float scale = (std::max)({1.0f, std::abs(actor.desc.localScale.x),
                std::abs(actor.desc.localScale.y), std::abs(actor.desc.localScale.z)});
            // Pods/backdrop and presentation-only recoil extend beyond the
            // collision sphere; reserve room for those visuals as well.
            const float radius = actor.desc.radius * scale * 2.0f +
                kEnemyPresentationClearancePadding;
            const float cartClearance = (std::max)(kMinimumEnemyCartClearance,
                definition.engagementBandDisengageForwardDistance) + radius;
            const float cameraClearance = kMinimumEnemyCameraClearance + radius;
            const Vector3 toCart = subtract(position, cart);
            const Vector3 toCamera = subtract(position, input.cameraPosition);
            unsafeSpatialPose = dot(toCart, toCart) < cartClearance * cartClearance ||
                (input.hasCameraPosition &&
                 dot(toCamera, toCamera) < cameraClearance * cameraClearance);
            if (unsafeSpatialPose) {
                bool relocatedForward = false;
                if (actor.HoldsCombatPositionUntilResolved()) {
                    // At bends, an arc-distance floor can still overlap the
                    // camera. Find a clear place ahead instead of retiring.
                    float bestDelta = 0.0f;
                    float bestScore = 1000000.0f;
                    const float currentDistance = actor.desc.spawnDistance + actor.desc.distanceOffset;
                    for (float ahead = definition.engagementBandMinimumForwardDistance;
                         ahead <= definition.engagementBandMaximumForwardDistance; ahead += 2.0f) {
                        const float distance = input.playerDistance + ahead;
                        const Vector3 candidate = ResolveRailLocal(rail, distance, 0.0f,
                            actor.desc.lateralOffset, actor.desc.verticalOffset);
                        const Vector3 cartDelta = subtract(candidate, cart);
                        const Vector3 cameraDelta = subtract(candidate, input.cameraPosition);
                        if (dot(cartDelta, cartDelta) < cartClearance * cartClearance ||
                            (input.hasCameraPosition && dot(cameraDelta, cameraDelta) < cameraClearance * cameraClearance)) continue;
                        const float score = std::abs(distance - currentDistance);
                        if (score < bestScore) {
                            bestScore = score;
                            bestDelta = distance - currentDistance;
                            relocatedForward = true;
                        }
                    }
                    if (relocatedForward) {
                        actor.desc.distanceOffset += bestDelta;
                        behavior.integratedForwardOffset += bestDelta;
                        behavior.engagementBandForwardDistance += bestDelta;
                    }
                }
                if (!relocatedForward) {
                    // At an endpoint there may be no clear place ahead.
                    // Preserve both exclusion spheres without removing actors.
                    const float side = behavior.authoredLateralOffset < -0.1f ? -1.0f : 1.0f;
                    const Vector3 direction = Scale(rail.Evaluate(
                        actor.desc.spawnDistance + actor.desc.distanceOffset).right, side);
                    const auto outwardShift = [&](const Vector3& offset, float clearance) {
                        const float projection = dot(offset, direction);
                        const float discriminant = projection * projection -
                            dot(offset, offset) + clearance * clearance;
                        return discriminant >= 0.0f
                            ? (std::max)(0.0f, -projection + std::sqrt(discriminant)) : 0.0f;
                    };
                    float shift = outwardShift(toCart, cartClearance);
                    if (input.hasCameraPosition) {
                        shift = (std::max)(shift, outwardShift(toCamera, cameraClearance));
                    }
                    actor.desc.lateralOffset += side * (shift + 0.05f);
                    behavior.safetyLateralOffset += side * (shift + 0.05f);
                }
                if (!actor.HoldsCombatPositionUntilResolved()) {
                    enemyEntranceExitDirector_.RequestActorExit(actor.actorId);
                    staging.initialized = true;
                    if (staging.phase != EnemyEntranceExitPhase::Exiting &&
                        staging.phase != EnemyEntranceExitPhase::Exited) {
                        staging.phase = EnemyEntranceExitPhase::Exiting;
                        staging.phaseElapsedSeconds = 0.0f;
                        staging.presentationAlpha = 1.0f;
                        staging.presentationScale = 1.0f;
                    }
                    staging.exitRequested = true;
                }
            }
        }
        const bool departing = behavior.engagementBandExitRequested ||
            (staging.initialized && (staging.exitRequested ||
             staging.phase == EnemyEntranceExitPhase::Exiting ||
             staging.phase == EnemyEntranceExitPhase::Exited));
        if (departing || unsafeSpatialPose) {
            if (actor.HoldsCombatPositionUntilResolved()) {
                // Safety cancels a stale shot, but never fades or removes a
                // living hover drone. It can acquire a new full warning later.
                enemyBehaviorSystem_.CancelAttackIntent(actor, 0.25f);
                behavior.attackIntentActive = false;
                behavior.telegraphPresented = false;
                behavior.attackTimeRemaining = 0.0f;
                behavior.engagementBandExitRequested = false;
                behavior.engagementBandAttackAllowed = !unsafeSpatialPose;
                behavior.state = EnemyBehaviorState::Repositioning;
                behavior.stateElapsedSeconds = 0.0f;
                staging.exitRequested = false;
                staging.exitComplete = false;
                enemyAttackCoordinator_.CancelActor(actor, EnemyAttackCancelReason::ActorUnavailable);
                actor.targetingState.solutionLocked = false;
                continue;
            }
            behavior.attackIntentActive = false;
            behavior.telegraphPresented = false;
            behavior.attackTimeRemaining = 0.0f;
            behavior.engagementBandAttackAllowed = false;
            behavior.engagementBandExitRequested = true;
            behavior.state = EnemyBehaviorState::Retreating;
            staging.attackSuppressed = true;
            staging.targetable = false;
            actor.fireTimer = definition.attackCooldownSeconds;
            enemyAttackCoordinator_.CancelActor(actor, EnemyAttackCancelReason::ActorUnavailable);
            actor.targetingState.solutionLocked = false;
        }
    }
}

bool CourseSpawnRuntime::CanEnemyFire(
    CourseEnemyActor& enemy,
    const CourseEnemyFireSafetyFrameInput& safetyInput,
    float dt) {
    enemy.fireEnvironmentReady = UpdateEnemyFireEnvironment(enemy, safetyInput, dt);
    if (!enemy.fireEnvironmentReady) {
        InvalidateEnemyAttackWarning(enemy.actorId);
        return false;
    }
    if (enemy.behaviorState.initialized &&
        enemy.behaviorDefinition.commercialBehavior &&
        !enemyBehaviorSystem_.CanCommitAttack(enemy)) {
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = enemy.behaviorState.attackIntentActive
            ? "behavior awaiting readable warning countdown"
            : "behavior has no attack intent";
        return false;
    }
    return true;
}

bool CourseSpawnRuntime::InvalidateEnemyAttackWarning(uint32_t actorId) {
    for (CourseEnemyActor& enemy : enemies_) {
        if (enemy.actorId != actorId || !enemy.behaviorState.attackIntentActive) continue;
        auto& state = enemy.behaviorState;
        state.telegraphPresented = false;
        state.attackTimeRemaining = (std::max)(0.05f, enemy.behaviorDefinition.attackLeadSeconds);
        enemy.fireTimer = state.attackTimeRemaining;
        enemy.targetingState.solutionLocked = false;
        enemy.targetingState.waitingForReachableLaunch = false;
        enemy.targetingState.forwardInterceptReachable = true;
        enemyAttackCoordinator_.CancelActor(enemy, EnemyAttackCancelReason::ActorUnavailable);
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::DeferEnemyAttackForUnreachableLaunch(uint32_t actorId) {
    for (CourseEnemyActor& enemy : enemies_) {
        if (enemy.actorId != actorId || !enemy.behaviorState.attackIntentActive ||
            !enemy.targetingState.compensatesForwardTravel) continue;
        enemy.targetingState.forwardInterceptReachable = false;
        enemyAttackCoordinator_.DeferUnreachableAttack(enemy);
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::UpdateEnemyFireEnvironment(
    CourseEnemyActor& enemy,
    const CourseEnemyFireSafetyFrameInput& safetyInput,
    float dt) {
    const drone_perf::Scope profile(drone_perf::Stage::Environment);
    const bool entranceExitSuppressesFire =
        enemy.entranceExitState.initialized &&
        enemy.entranceExitState.attackSuppressed;
    if (enemy.stationaryDormant || enemy.startupFlightControlled || enemy.desc.suppressFire || entranceExitSuppressesFire ||
        (enemy.combatState.initialized && !enemy.combatState.canFire)) {
        enemy.fireVisibleTime = 0.0f;
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = enemy.desc.suppressFire
            ? "actor fire suppressed"
            : (entranceExitSuppressesFire
                ? "entrance/exit staging gate"
                : "combat phase: " + std::string(ToString(enemy.combatState.phase)));
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }
    // Never admit an attack solely through an offscreen HUD indicator.
    if (safetyInput.railPath != nullptr && safetyInput.viewProjection != nullptr) {
        const Vector3 world = ResolveRailLocal(*safetyInput.railPath,
            enemy.desc.spawnDistance, enemy.desc.distanceOffset,
            enemy.desc.lateralOffset, enemy.desc.verticalOffset);
        const auto& m = safetyInput.viewProjection->m;
        const float x = world.x * m[0][0] + world.y * m[1][0] + world.z * m[2][0] + m[3][0];
        const float y = world.x * m[0][1] + world.y * m[1][1] + world.z * m[2][1] + m[3][1];
        const float z = world.x * m[0][2] + world.y * m[1][2] + world.z * m[2][2] + m[3][2];
        const float w = world.x * m[0][3] + world.y * m[1][3] + world.z * m[2][3] + m[3][3];
        if (w <= 0.0001f || z < 0.0f || z > w ||
            std::abs(x) >= w * 0.90f || std::abs(y) >= w * 0.90f) {
            enemy.fireVisibleTime = 0.0f;
            enemy.fireSafetyAllowed = false;
            enemy.fireSafetyReason = "offscreen / warning safe-area gate";
            ++fireSafetyStats_.blockedByVisibilityTime;
            fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
            return false;
        }
    }
    // Check current geometry before decrementing the warning and again at
    // the final staged pose before execution. HUD-only LOS is one frame late.
    if (enemy.behaviorDefinition.commercialBehavior && enemy.behaviorState.attackIntentActive &&
        safetyInput.railPath != nullptr && safetyInput.hasCameraPosition &&
        (safetyInput.course != nullptr || safetyInput.terrainSettings != nullptr || !obstacles_.empty())) {
        const Vector3 world = ResolveRailLocal(*safetyInput.railPath,
            enemy.desc.spawnDistance, enemy.desc.distanceOffset,
            enemy.desc.lateralOffset, enemy.desc.verticalOffset);
        const Vector3 delta{world.x - safetyInput.cameraPosition.x,
            world.y - safetyInput.cameraPosition.y, world.z - safetyInput.cameraPosition.z};
        const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (distance > 0.001f) {
            RailAimState aim{};
            aim.valid = true;
            aim.worldRayOrigin = safetyInput.cameraPosition;
            aim.worldRayDirection = Scale(delta, 1.0f / distance);
            aim.maxDistance = distance + enemy.desc.radius;
            aim.aimDistance = aim.maxDistance;
            aim.worldAimPoint = world;
            RailWorldRaycastInput query{};
            query.aim = &aim;
            query.railPath = safetyInput.railPath;
            query.spawnRuntime = this;
            query.course = safetyInput.course;
            query.terrainSettings = safetyInput.terrainSettings;
            query.terrainEdits = safetyInput.terrainEdits;
            query.terrainPreview = safetyInput.terrainPreview;
            query.playerDistance = safetyInput.playerDistance;
            query.includeVisualColumns = true;
            const RailAimHit hit = RailWorldRaycast::Query(query);
            if (hit.hit && !(hit.kind == RailAimHitKind::Enemy && hit.actorId == enemy.actorId)) {
                enemy.fireVisibleTime = 0.0f;
                enemy.fireSafetyAllowed = false;
                enemy.fireSafetyReason = "warning line-of-sight blocked";
                ++fireSafetyStats_.blockedByVisibilityTime;
                fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
                return false;
            }
        }
    }
    if (enemy.screenPresenceEvaluated &&
        !enemy.screenPresenceAttackAllowed) {
        enemy.fireVisibleTime = 0.0f;
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = "screen presence exposure gate";
        ++fireSafetyStats_.blockedByVisibilityTime;
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }
    if (enemy.encounterPacingEvaluated &&
        !enemy.encounterPacingAttackAllowed) {
        enemy.fireVisibleTime = 0.0f;
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = "encounter pacing phase gate";
        ++fireSafetyStats_.blockedByVisibilityTime;
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }
    if (!fireSafetySettings_.enabled) {
        enemy.fireSafetyAllowed = true;
        enemy.fireSafetyReason = "safety disabled";
        ++fireSafetyStats_.allowedEnemies;
        fireSafetyStats_.lastAllowedReason = enemy.fireSafetyReason;
        return true;
    }

    const float actorDistance = enemy.desc.spawnDistance + enemy.desc.distanceOffset;
    const float forwardDistance = actorDistance - safetyInput.playerDistance;
    const bool cameraBlocks =
        fireSafetySettings_.requireCameraAllowsFire &&
        (!safetyInput.cameraAllowsEnemyFire ||
            !safetyInput.cameraStableForAiming ||
            safetyInput.cameraHardTransition);
    const bool inFireRange =
        forwardDistance >= fireSafetySettings_.minForwardDistance &&
        forwardDistance <= fireSafetySettings_.maxForwardDistance;

    if (!cameraBlocks && inFireRange) {
        enemy.fireVisibleTime += dt;
    } else {
        enemy.fireVisibleTime = 0.0f;
    }

    if (cameraBlocks) {
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = "camera: " + safetyInput.cameraReason;
        ++fireSafetyStats_.blockedByCamera;
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }
    if (!inFireRange) {
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = forwardDistance < fireSafetySettings_.minForwardDistance
            ? "too close / behind safety window"
            : "too far for readable fire";
        ++fireSafetyStats_.blockedByRange;
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }
    if (enemy.fireVisibleTime < fireSafetySettings_.minVisibleBeforeFire) {
        enemy.fireSafetyAllowed = false;
        enemy.fireSafetyReason = "visible time warming";
        ++fireSafetyStats_.blockedByVisibilityTime;
        fireSafetyStats_.lastBlockedReason = enemy.fireSafetyReason;
        return false;
    }

    enemy.fireSafetyAllowed = true;
    enemy.fireSafetyReason = "camera safe";
    ++fireSafetyStats_.allowedEnemies;
    fireSafetyStats_.lastAllowedReason = enemy.fireSafetyReason;
    return true;
}

void CourseSpawnRuntime::PruneDestroyedActors() {
    enemies_.erase(
        std::remove_if(
            enemies_.begin(),
            enemies_.end(),
            [](const CourseEnemyActor& enemy) {
                if (!enemy.HoldsCombatPositionUntilResolved() && enemy.desc.actorAssetId!="short_turret" &&
                    enemy.age >= enemy.desc.lifetime) {
                    return true;
                }
                if (enemy.entranceExitState.exitComplete) {
                    return true;
                }
                if (!enemy.combatState.initialized) {
                    return enemy.desc.hitPoints <= 0.0f;
                }
                return enemy.combatState.phase == EnemyCombatPhase::Retired;
            }),
        enemies_.end());
    bullets_.erase(
        std::remove_if(
            bullets_.begin(),
            bullets_.end(),
            [](const CourseBulletActor& bullet) {
                return !bullet.active || bullet.age >= bullet.lifetime;
            }),
        bullets_.end());
    obstacles_.erase(
        std::remove_if(
            obstacles_.begin(),
            obstacles_.end(),
            [](const CourseObstacleActor& obstacle) {
                return obstacle.age >= obstacle.desc.lifetime ||
                    (obstacle.desc.breakable && obstacle.desc.hitPoints <= 0.0f);
            }),
        obstacles_.end());
    vfxCues_.erase(
        std::remove_if(
            vfxCues_.begin(),
            vfxCues_.end(),
            [](const CourseVfxCue& cue) {
                return cue.age >= cue.desc.lifetime;
            }),
        vfxCues_.end());
}

bool CourseSpawnRuntime::SpawnEnemyActor(CourseEnemyActorDesc desc, std::string* errorMessage) {
    if (!ValidEnemyDescription(desc) || desc.bulletCount < 0 || desc.bulletCount > 4096 ||
        nextActorId_ == UINT32_MAX) {
        if (errorMessage != nullptr) *errorMessage = "Enemy spawn requires finite valid geometry/health and a bounded projectile count.";
        return false;
    }
    if(desc.actorAssetId=="short_mothership_core") desc.role="mothership_boss_reactor";
    const int carrierPart=RailMothership::PartIndex(desc.actorAssetId);
    if(carrierPart>=0) {
        desc.role=carrierPart==0?"carrier_rapid_part":"carrier_heavy_part";
        // The two sockets share one travelling frame instead of independently
        // drifting to different engagement distances.
        desc.behaviorDefinition.engagementBandPreferredForwardDistance=RailMothership::PartForwardDistance;
        desc.behaviorDefinition.engagementBandMinimumForwardDistance=40;
        desc.behaviorDefinition.engagementBandMaximumForwardDistance=60;
    }
    desc.lifetime = (std::max)(0.1f, desc.lifetime);
    desc.radius = (std::max)(0.05f, desc.radius);
    desc.hitPoints = (std::max)(1.0f, desc.hitPoints);
    desc.fireInterval = (std::max)(0.08f, desc.fireInterval);
    desc.firstShotDelay = (std::max)(0.02f, desc.firstShotDelay);
    desc.bulletSpeed = (std::max)(1.0f, desc.bulletSpeed);
    if (desc.firePattern == CourseEnemyFirePattern::Single) {
        desc.firePattern = PatternForRole(desc.role);
    }
    if (desc.bulletCount <= 1) {
        desc.bulletCount =
            desc.firePattern == CourseEnemyFirePattern::BossArc ? 5 :
            desc.firePattern == CourseEnemyFirePattern::Spread ? 3 :
            desc.firePattern == CourseEnemyFirePattern::Twin ? 2 :
            1;
    }
    if (desc.bulletLateralSpreadSpeed <= 0.0f) {
        desc.bulletLateralSpreadSpeed =
            desc.firePattern == CourseEnemyFirePattern::BossArc ? 3.2f :
            desc.firePattern == CourseEnemyFirePattern::Spread ? 2.4f :
            desc.firePattern == CourseEnemyFirePattern::Twin ? 1.3f :
            0.0f;
    }
    if (desc.bulletVerticalSpreadSpeed <= 0.0f &&
        desc.firePattern == CourseEnemyFirePattern::BossArc) {
        desc.bulletVerticalSpreadSpeed = 0.55f;
    }
    desc.bulletCount = (std::max)(1, desc.bulletCount);
    desc.bulletRadius = (std::max)(0.05f, desc.bulletRadius);
    desc.bulletLifetime = (std::max)(0.1f, desc.bulletLifetime);
    desc.bulletDamage = (std::max)(0.0f, desc.bulletDamage);

    CourseEnemyActor actor{};
    actor.desc = std::move(desc);
    actor.stationaryDormant=actor.desc.stationaryActivationDistance>=0;
    actor.fireTimer = actor.desc.firstShotDelay;
    actor.actorId = nextActorId_++;
    enemyCombatSystem_.InitializeActor(actor);
    enemyBehaviorSystem_.InitializeActor(actor);
    if (actor.desc.projectileDefinition.id.empty()) {
        actor.desc.projectileDefinition =
            EnemyProjectileDefinitionAsset::LegacyDirect();
        actor.desc.projectileDefinition.id = actor.desc.projectileDefinitionId.empty()
            ? "runtime_" + actor.desc.bulletPatternId
            : actor.desc.projectileDefinitionId;
        actor.desc.projectileDefinition.displayName =
            actor.desc.projectileDefinition.id;
        actor.desc.projectileDefinition.trajectory =
            actor.behaviorDefinition.commercialBehavior
                ? EnemyProjectileTrajectory::Predictive
                : EnemyProjectileTrajectory::Direct;
        actor.desc.projectileDefinition.initialSpeed = actor.desc.bulletSpeed;
        actor.desc.projectileDefinition.maximumSpeed = actor.desc.bulletSpeed;
        actor.desc.projectileDefinition.radius = actor.desc.bulletRadius;
        actor.desc.projectileDefinition.lifetime = actor.desc.bulletLifetime;
        actor.desc.projectileDefinition.damage = actor.desc.bulletDamage;
        actor.desc.projectileDefinition.color = actor.desc.bulletColor;
    }
    actor.desc.projectileDefinitionId = actor.desc.projectileDefinition.id;
    enemyAttackCoordinator_.InitializeActor(actor);
    if(actor.stationaryDormant) {
        actor.entranceExitState.initialized=true;
        actor.entranceExitState.phase=EnemyEntranceExitPhase::Active;
        actor.entranceExitState.presentationAlpha=actor.entranceExitState.presentationScale=1;
        actor.entranceExitState.targetable=false;
        actor.entranceExitState.attackSuppressed=true;
    }
    if (actor.desc.establishedEntrance) {
        actor.entranceExitState.initialized = true;
        actor.entranceExitState.phase = EnemyEntranceExitPhase::Active;
        actor.entranceExitState.presentationAlpha = actor.entranceExitState.presentationScale = 1.0f;
        actor.entranceExitState.attackSuppressed = false;
        actor.entranceExitState.targetable = true;
    }
    enemies_.push_back(std::move(actor));
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool CourseSpawnRuntime::SetEnemyLocalRotation(uint32_t actorId, Vector3 rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z)) return false;
    for (auto& actor : enemies_) {
        if (actor.actorId != actorId) continue;
        actor.desc.localRotation = rotation;
        actor.weaponMount.ready = false;
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::SetEnemyStartupFlight(uint32_t actorId, float playerDistance,
        float forward, float lateral, float vertical, Vector3 rotation,
        bool releaseToCombat, float playerSpeed) {
    if(!FiniteValues({playerDistance,forward,lateral,vertical,rotation.x,rotation.y,
            rotation.z,playerSpeed})) return false;
    for(auto& actor:enemies_) {
        if(actor.actorId!=actorId || !actor.desc.establishedEntrance) continue;
        actor.startupFlightControlled=!releaseToCombat;
        actor.desc.spawnDistance=playerDistance+forward;
        actor.desc.distanceOffset=0.0f;
        actor.desc.lateralOffset=lateral;
        actor.desc.verticalOffset=vertical;
        actor.desc.localRotation=rotation;
        actor.weaponMount.ready=false;
        actor.formationState={};
        auto& state=actor.behaviorState;
        state.authoredForwardOffset=state.integratedForwardOffset=0.0f;
        state.authoredLateralOffset=lateral;
        state.authoredVerticalOffset=vertical;
        state.engagementBandForwardDistance=forward;
        if(releaseToCombat) {
            // This visible entrance already occupies its endpoint. Do not let
            // the formation's age-zero hidden-spawn placement snap it elsewhere.
            actor.formationState.hoverInitialized=true;
            // Seed the same hover phase and velocity as the cinematic endpoint.
            // Subtract its procedural offset so GO never teleports the actor.
            state.hoverPreferredForwardDistance=forward;
            state.engagementBandVelocity=(std::max)(0.0f,playerSpeed);
            CourseEnemyActor preview=actor;
            AdvanceEnemyBehaviorMovement(preview,0.0f,playerDistance,playerSpeed);
            state.integratedForwardOffset=-preview.behaviorState.behaviorForwardOffset;
            state.authoredLateralOffset-=preview.behaviorState.behaviorLateralOffset;
            state.authoredVerticalOffset-=preview.behaviorState.behaviorVerticalOffset;
            state.behaviorForwardOffset=preview.behaviorState.behaviorForwardOffset;
            state.behaviorLateralOffset=preview.behaviorState.behaviorLateralOffset;
            state.behaviorVerticalOffset=preview.behaviorState.behaviorVerticalOffset;
            state.attackCooldownRemaining=(std::max)(state.attackCooldownRemaining,actor.desc.firstShotDelay);
        }
        return true;
    }
    return false;
}

bool CourseSpawnRuntime::MarkEnemyAttackTelegraphPresented(
    uint32_t actorId,
    uint64_t attackIntentSequence) {
    if (!enemyAttackCoordinator_.MarkTelegraphPresented(
            *this, actorId, attackIntentSequence)) {
        return false;
    }
    return enemyBehaviorSystem_.MarkTelegraphPresented(
        *this, actorId, attackIntentSequence);
}

void CourseSpawnRuntime::StageCarrierDeployments(float playerDistance,
        const RailMothership::Frame& carrier) {
    if(!carrier.visible || !std::isfinite(playerDistance)) return;
    uint32_t slot=0;
    for(auto& actor:enemies_) {
        if(actor.carrierDeployment.initialized && !actor.carrierDeployment.finished) {
            actor.carrierDeployment.launchDistance=carrier.railDistance;
            actor.carrierDeployment.launchVertical=carrier.railVertical+RailMothership::BayHeight;
        }
        if(actor.age>0.0f || actor.carrierDeployment.initialized ||
            actor.desc.establishedEntrance || RailTurret::IsMesh(actor.desc.meshId) ||
            RailMothership::IsCombatActor(actor.desc.actorAssetId)) continue;
        const size_t groupSize=std::count_if(enemies_.begin(),enemies_.end(),[&](const auto& member) {
            return member.desc.waveId==actor.desc.waveId && member.desc.hitPoints>0 &&
                !RailTurret::IsMesh(member.desc.meshId);
        });
        if(actor.desc.entrySource!=EnemyEntrySource::Carrier &&
            (actor.desc.entrySource!=EnemyEntrySource::Automatic || groupSize<3 || actor.desc.waveId.empty())) continue;
        auto& launch=actor.carrierDeployment;
        launch.initialized=true;
        launch.delay=carrier.deploymentDelay+RailMothership::LaunchStagger*float(slot);
        launch.launchDistance=carrier.railDistance;
        launch.launchLateral=(slot&1u)?RailMothership::BayLateral:-RailMothership::BayLateral;
        launch.launchVertical=carrier.railVertical+RailMothership::BayHeight;
        launch.forward=(std::clamp)(actor.desc.spawnDistance+actor.desc.distanceOffset-playerDistance,44.0f,85.0f);
        launch.lateral=actor.desc.lateralOffset;
        launch.vertical=actor.desc.verticalOffset;
        actor.desc.establishedEntrance=true;
        actor.entranceExitState={};
        actor.entranceExitState.initialized=true;
        actor.entranceExitState.phase=EnemyEntranceExitPhase::Active;
        actor.entranceExitState.presentationAlpha=actor.entranceExitState.presentationScale=1.0f;
        const auto flight=RailMothership::Evaluate(launch,playerDistance);
        (void)SetEnemyStartupFlight(actor.actorId,playerDistance,
            flight.distance-playerDistance,flight.lateral,flight.vertical,flight.rotation);
        actor.entranceExitState.targetable=false;
        actor.entranceExitState.attackSuppressed=true;
        ++slot;
    }
}

void CourseSpawnRuntime::UpdateCarrierDeployments(float deltaTime,float playerDistance,float playerSpeed) {
    for(auto& actor:enemies_) {
        auto& launch=actor.carrierDeployment;
        if(!launch.initialized || launch.finished || actor.desc.hitPoints<=0.0f) continue;
        launch.elapsed+=std::isfinite(deltaTime)?(std::clamp)(deltaTime,0.0f,0.25f):0.0f;
        const auto flight=RailMothership::Evaluate(launch,playerDistance);
        (void)SetEnemyStartupFlight(actor.actorId,playerDistance,
            flight.distance-playerDistance,flight.lateral,flight.vertical,flight.rotation,
            flight.released,playerSpeed);
        actor.entranceExitState.presentationAlpha=launch.elapsed>=launch.delay ? 1.0f : 0.0f;
        actor.entranceExitState.targetable=flight.released;
        actor.entranceExitState.attackSuppressed=!flight.released;
        launch.finished=flight.released;
    }
}

void CourseSpawnRuntime::UpdateTerrainIngresses(float deltaTime,const CourseEnemyFireSafetyFrameInput& input) {
    if(input.ingressRoutes.empty() || deltaTime<=0) return;
    unsigned validationBudget=48;
    const bool collisionMatches=!input.terrainCollision || (input.railPath && input.terrainSettings &&
        input.terrainCollision->Matches(*input.railPath,*input.terrainSettings,input.terrainEdits,input.terrainPreview));
    for(auto& actor:enemies_) {
        auto& entry=actor.terrainIngress;
        if(entry.finished) {entry.elapsed+=(std::min)(deltaTime,0.25f);continue;}
        if(actor.desc.hitPoints<=0 || actor.carrierDeployment.initialized ||
            RailTurret::IsMesh(actor.desc.meshId) || RailMothership::IsCombatActor(actor.desc.actorAssetId)) continue;
        if(!entry.waiting && !entry.initialized) {
            if(actor.age>0 || actor.desc.establishedEntrance) continue;
            entry.waiting=true;
            const float home=actor.behaviorState.hoverPreferredForwardDistance;
            entry.forward=(std::clamp)(home>0?home:
                actor.desc.spawnDistance+actor.desc.distanceOffset-input.playerDistance,44.0f,100.0f);
            entry.lateral=actor.desc.lateralOffset;entry.vertical=actor.desc.verticalOffset;
            const auto earlier=std::count_if(enemies_.begin(),enemies_.end(),[&](const auto& other) {
                return other.actorId<actor.actorId && other.age<0.25f && other.desc.waveId==actor.desc.waveId;
            });
            entry.delay=float((std::min)(earlier,decltype(earlier)(5)))*0.12f;
            actor.desc.establishedEntrance=true;actor.startupFlightControlled=true;
            actor.entranceExitState={};actor.entranceExitState.initialized=true;
            // This flight owns the whole entrance. Pending would replay the
            // legacy side-entry offset on the exact hover-release frame.
            actor.entranceExitState.phase=EnemyEntranceExitPhase::Active;
            actor.entranceExitState.presentationAlpha=actor.entranceExitState.presentationScale=1;
            actor.entranceExitState.targetable=false;
            actor.entranceExitState.attackSuppressed=true;
        }
        if(entry.waiting) {
            const float desiredSide=actor.desc.entrySource==EnemyEntrySource::RockLeft?-1.0f:
                actor.desc.entrySource==EnemyEntrySource::RockRight?1.0f:
                (entry.lateral<0?-1.0f:1.0f);
            std::vector<std::pair<float,const RailEnemyIngressRoute*>> candidates;
            for(const auto& route:input.ingressRoutes) {
                const float ahead=route.distance-input.playerDistance;
                if(ahead<32+(std::max)(0.0f,input.playerSpeed)*0.20f || ahead>300) continue;
                const float desiredAhead=route.kind==RailEnemyIngressKind::SideOpening?
                    90+input.playerSpeed*.4f:90+input.playerSpeed*1.8f;
                float candidate=std::abs(ahead-desiredAhead)+(route.side!=desiredSide?90.0f:0.0f);
                if(actor.desc.entrySource!=EnemyEntrySource::High)
                    candidate+=(std::max)(0.0f,route.departure.y-50)*0.40f;
                if(actor.desc.entrySource==EnemyEntrySource::High && route.kind!=RailEnemyIngressKind::Cliff)
                    candidate+=route.kind==RailEnemyIngressKind::SideOpening?18:45;
                if(actor.desc.entrySource==EnemyEntrySource::SideOpening && route.kind!=RailEnemyIngressKind::SideOpening)
                    candidate+=60;
                if((actor.desc.entrySource==EnemyEntrySource::RockLeft || actor.desc.entrySource==EnemyEntrySource::RockRight) &&
                    route.kind!=RailEnemyIngressKind::Rock) candidate+=20;
                candidates.push_back({candidate,&route});
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            const RailEnemyIngressRoute* best=nullptr;
            RailEnemyIngressState selected;
            for(size_t probe=0;probe<candidates.size();++probe) {
                const auto& candidate=candidates[entry.routeProbeCursor%candidates.size()];
                if(input.terrainCollision && (validationBudget<17 || !collisionMatches)) break;
                auto plan=entry;plan.route=*candidate.second;
                PrepareRailEnemyIngress(plan,input.playerDistance,input.playerSpeed,input.railPath);
                if(input.terrainCollision && input.railPath && input.terrainSettings) {
                    auto forecast=plan;forecast.delay=0;forecast.elapsed=0;
                    auto previous=EvaluateRailEnemyIngress(forecast,input.playerDistance,input.railPath).railPosition;
                    bool clear=true;
                    const auto world=[&](Vector3 p) {
                        const auto s=input.railPath->Evaluate(p.z);
                        return Add(s.position,Add(Scale(s.right,p.x),Scale(s.up,p.y)));
                    };
                    const auto eyeSample=input.railPath->Evaluate((std::max)(0.0f,input.playerDistance-24));
                    const auto eye=input.hasCameraPosition?input.cameraPosition:Add(eyeSample.position,Scale(eyeSample.up,10));
                    const auto sourceSample=input.railPath->Evaluate(previous.z);
                    const float body=actor.desc.radius*2;
                    // The exterior launch must be hidden by the ORIGINAL
                    // terrain, rather than made invisible in open air.
                    for(const auto& offset:{Vector3{},Scale(sourceSample.right,body),Scale(sourceSample.right,-body),
                            Scale(sourceSample.up,body),Scale(sourceSample.up,-body)}) {
                        --validationBudget;
                        const auto source=Add(world(previous),offset);
                        const Vector3 d{source.x-eye.x,source.y-eye.y,source.z-eye.z};
                        const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
                        const auto hit=input.terrainCollision->Query(eye,Scale(d,1/(std::max)(0.001f,length)),length,
                            (std::max)(0.0f,input.playerDistance-36),
                            (std::min)(input.railPath->Length()-0.001f,previous.z+12),true);
                        if(!hit.available || !hit.hit || hit.distance>length-body) {clear=false;break;}
                    }
                    if(!clear) {++entry.routeProbeCursor;continue;}
                    for(unsigned step=1;step<=12;++step) {
                        --validationBudget;forecast.elapsed=forecast.duration*float(step)/12;
                        const auto next=EvaluateRailEnemyIngress(forecast,input.playerDistance+
                            (std::max)(0.0f,input.playerSpeed)*(forecast.elapsed+entry.delay),input.railPath).railPosition;
                        const auto from=world(previous),to=world(next);
                        const Vector3 d{to.x-from.x,to.y-from.y,to.z-from.z};
                        const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
                        if(length>0.001f) {
                            const auto hit=input.terrainCollision->Query(from,Scale(d,1/length),length+actor.desc.radius,
                                (std::max)(0.0f,(std::min)(previous.z,next.z)-8),
                                (std::min)(input.railPath->Length()-0.001f,(std::max)(previous.z,next.z)+8),true);
                            if(!hit.available || hit.hit) {clear=false;break;}
                        }
                        previous=next;
                    }
                    if(!clear) {++entry.routeProbeCursor;continue;}
                }
                best=candidate.second;selected=std::move(plan);break;
            }
            if(!best) continue;
            entry=std::move(selected);entry.initialized=true;entry.waiting=false;
        }
        entry.elapsed+=(std::min)(deltaTime,0.25f);
        const auto flight=EvaluateRailEnemyIngress(entry,input.playerDistance,input.railPath);
        auto rotation=actor.desc.localRotation;
        if(input.railPath) {
            const auto target=ResolveRailEnemyIngressRotation(entry,*input.railPath,input.playerDistance,
                input.playerSpeed,input.cameraPosition,input.hasCameraPosition);
            if(entry.elapsed<=entry.delay+deltaTime*1.5f) rotation=target;
            else {
                const float dt=(std::min)(deltaTime,0.25f);
                const float tail=(std::clamp)((flight.progress-0.85f)/0.15f,0.0f,1.0f);
                const float response=1-std::exp(-7*dt)*(1-tail*tail*(3-2*tail));
                const float yawDelta=std::remainder(target.y-rotation.y,6.28318530718f);
                const float yawStep=(std::clamp)(yawDelta*response,-2.6f*dt,2.6f*dt);
                rotation.y+=yawStep;
                rotation.x+=(std::clamp)((target.x-rotation.x)*response,-1.8f*dt,1.8f*dt);
                const float bank=(std::clamp)(-yawStep/(std::max)(dt,0.0001f)*0.22f,-0.40f,0.40f)*(1-tail);
                rotation.z+=(bank-rotation.z)*(1-std::exp(-5*dt));
                rotation.z*=1-tail*tail*(3-2*tail);
            }
        }
        // The live aiming convention adds the camera-facing base rotation.
        // Do not add the cinematic absolute heading a second time at handoff.
        if(flight.released) rotation={};
        (void)SetEnemyStartupFlight(actor.actorId,input.playerDistance,
            flight.railPosition.z-input.playerDistance,flight.railPosition.x,flight.railPosition.y,
            rotation,flight.released,input.playerSpeed);
        actor.entranceExitState.presentationAlpha=actor.entranceExitState.presentationScale=1;
        // Visible arrivals can already be intercepted; terrain visibility and
        // the regular targeting range still exclude hidden/far actors.
        actor.entranceExitState.targetable=flight.progress>=0.30f;
        actor.entranceExitState.attackSuppressed=!flight.released;
        entry.finished=flight.released;
    }
}

bool CourseSpawnRuntime::SpawnObstacle(CourseObstacleActorDesc desc, std::string* errorMessage) {
    if (!ValidObstacleDescription(desc) || nextActorId_ == UINT32_MAX) {
        if (errorMessage != nullptr) *errorMessage = "Obstacle spawn requires finite valid geometry and nonnegative health.";
        return false;
    }
    desc.lifetime = (std::max)(0.1f, desc.lifetime);
    desc.halfExtents.x = (std::max)(0.25f, desc.halfExtents.x);
    desc.halfExtents.y = (std::max)(0.25f, desc.halfExtents.y);
    desc.halfExtents.z = (std::max)(0.25f, desc.halfExtents.z);

    CourseObstacleActor actor{};
    actor.desc = std::move(desc);
    actor.actorId = nextActorId_++;
    obstacles_.push_back(std::move(actor));
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void CourseSpawnRuntime::SpawnVfxCue(CourseVfxCueDesc desc) {
    desc.lifetime = (std::max)(0.1f, desc.lifetime);
    desc.radius = (std::max)(0.05f, desc.radius);
    if (desc.hasWorldPosition &&
        (!std::isfinite(desc.worldPosition.x) ||
         !std::isfinite(desc.worldPosition.y) ||
         !std::isfinite(desc.worldPosition.z))) {
        desc.hasWorldPosition = false;
    }

    CourseVfxCue cue{};
    cue.desc = std::move(desc);
    vfxCues_.push_back(std::move(cue));
}

void CourseSpawnRuntime::SubmitPendingVfx(EffectRuntime& effectRuntime, const RailPath& railPath) {
    if (railPath.Length() <= 0.0f || !effectRuntime.IsAttached()) {
        return;
    }

    for (CourseVfxCue& cue : vfxCues_) {
        if (cue.submitted) {
            continue;
        }

        const Vector3 center = cue.desc.hasWorldPosition
            ? cue.desc.worldPosition
            : ResolveRailLocal(
                railPath,
                cue.desc.spawnDistance,
                cue.desc.distanceOffset,
                cue.desc.lateralOffset,
                cue.desc.verticalOffset);
        cue.effectInstanceId = effectRuntime.PlayEffectWithParams(
            cue.desc.effectName,
            center,
            cue.desc.color,
            {cue.desc.radius, cue.desc.radius, cue.desc.radius});
        cue.submitted = cue.effectInstanceId != 0;
    }
}

uint32_t CourseSpawnRuntime::EmitEnemyBullets(const CourseEnemyActor& enemy) {
    return enemyProjectileSystem_.SpawnVolley(enemy, bullets_);
}

void CourseSpawnRuntime::AppendDebugDraw(
    ge3::debug::DebugDrawSystem& debugDraw,
    const RailPath& railPath) const {
    if (railPath.Length() <= 0.0f) {
        return;
    }

    for (const CourseEnemyActor& enemy : enemies_) {
        const RailPathSample sample = railPath.Evaluate(enemy.desc.spawnDistance + enemy.desc.distanceOffset);
        const Vector3 center = ResolveRailLocal(
            railPath,
            enemy.desc.spawnDistance,
            enemy.desc.distanceOffset,
            enemy.desc.lateralOffset,
            enemy.desc.verticalOffset);
        const Vector4 color = enemy.HoldsCombatPositionUntilResolved() ? enemy.desc.color :
            FadeColor(enemy.desc.color, enemy.age, enemy.desc.lifetime);
        debugDraw.AddPoint(center, enemy.desc.radius, color);
        debugDraw.AddCircle(center, sample.right, sample.up, enemy.desc.radius * 1.35f, color, 20);
        debugDraw.AddLine(center, Add(center, Scale(sample.tangent, -enemy.desc.radius * 2.0f)), color);
    }

    for (const CourseObstacleActor& obstacle : obstacles_) {
        const Vector3 center = ResolveRailLocal(
            railPath,
            obstacle.desc.spawnDistance,
            obstacle.desc.distanceOffset,
            obstacle.desc.lateralOffset,
            obstacle.desc.verticalOffset);
        const Vector3 extent = obstacle.desc.halfExtents;
        const Vector4 color = FadeColor(obstacle.desc.color, obstacle.age, obstacle.desc.lifetime);
        debugDraw.AddBox(
            {center.x - extent.x, center.y - extent.y, center.z - extent.z},
            {center.x + extent.x, center.y + extent.y, center.z + extent.z},
            color);
    }

    for (const CourseVfxCue& cue : vfxCues_) {
        const RailPathSample sample = railPath.Evaluate(cue.desc.spawnDistance + cue.desc.distanceOffset);
        const Vector3 center = cue.desc.hasWorldPosition
            ? cue.desc.worldPosition
            : ResolveRailLocal(
                railPath,
                cue.desc.spawnDistance,
                cue.desc.distanceOffset,
                cue.desc.lateralOffset,
                cue.desc.verticalOffset);
        const Vector3 axisU = cue.desc.hasWorldPosition
            ? Vector3{1.0f, 0.0f, 0.0f}
            : sample.right;
        const Vector3 axisV = cue.desc.hasWorldPosition
            ? Vector3{0.0f, 1.0f, 0.0f}
            : sample.up;
        const Vector4 color = FadeColor(cue.desc.color, cue.age, cue.desc.lifetime);
        debugDraw.AddCircle(center, axisU, axisV, cue.desc.radius, color, 32);
        debugDraw.AddLine(center, Add(center, Scale(axisV, cue.desc.radius * 1.4f)), color);
    }
}

void CourseSpawnRuntime::UpdateEnemyCombat(EnemyCombatSystem& system,
        const EnemyCombatFrameInput& input) {
    system.UpdateActors(enemies_, *this, input);
}

bool CourseSpawnRuntime::NotifyEnemyDamage(EnemyCombatSystem& system,
        const DamageResult& damageResult,
        const WeaponFeedbackEvent* feedbackEvent) {
    return system.SubmitDamageResultActors(enemies_, *this, damageResult, feedbackEvent);
}

bool CourseSpawnRuntime::DefeatEnemy(EnemyCombatSystem& system,
        uint32_t actorId) {
    return system.ForceDefeatActors(enemies_, *this, actorId);
}

void CourseSpawnRuntime::UpdateEnemyBehavior(EnemyBehaviorSystem& system,
        const EnemyBehaviorFrameInput& input) {
    system.UpdateActors(enemies_, *this, input);
}

bool CourseSpawnRuntime::MarkEnemyBehaviorTelegraph(EnemyBehaviorSystem& system,
        uint32_t actorId,
        uint64_t attackIntentSequence) {
    return system.MarkTelegraphPresentedActors(enemies_, *this, actorId, attackIntentSequence);
}

void CourseSpawnRuntime::RebuildEnemyAttacks(EnemyAttackCoordinator& system) {
    system.RebuildFromRuntimeActors(enemies_, *this);
}

void CourseSpawnRuntime::UpdateEnemyAttacks(EnemyAttackCoordinator& system,
        const EnemyBehaviorFrame& behaviorFrame,
        float deltaTime) {
    system.UpdateActors(enemies_, *this, behaviorFrame, deltaTime);
}

bool CourseSpawnRuntime::MarkEnemyCoordinatorTelegraph(EnemyAttackCoordinator& system,
        uint32_t actorId,
        uint64_t intentSequence) {
    return system.MarkTelegraphPresentedActors(enemies_, *this, actorId, intentSequence);
}

void CourseSpawnRuntime::BeginEnemyFormationFrame(EnemyFormationSystem& system) {
    system.BeginFrameActors(enemies_, *this);
}

void CourseSpawnRuntime::UpdateEnemyFormations(EnemyFormationSystem& system,
        float deltaTime, const RailPath* railPath, const CourseEnemyFireSafetyFrameInput* spatialContext) {
    system.UpdateActors(enemies_, *this, deltaTime, railPath, spatialContext);
}

void CourseSpawnRuntime::BeginEnemyEntranceExitFrame(EnemyEntranceExitDirector& system) {
    system.BeginFrameActors(enemies_, *this);
}

void CourseSpawnRuntime::UpdateEnemyEntranceExit(EnemyEntranceExitDirector& system,
        float deltaTime) {
    system.UpdateActors(enemies_, *this, deltaTime);
}

void CourseSpawnRuntime::UpdateEnemyTargeting(EnemyTargetingSystem& system,
        const EnemyTargetingFrameInput& input) {
    system.UpdateActors(enemies_, *this, input);
}

EnemyAttackInterruptResult CourseSpawnRuntime::InterruptEnemyAttack(EnemyAttackInterruptSystem& system,
        const DamageResult& damageResult) {
    return system.SubmitActors(enemies_, *this, damageResult);
}

void CourseSpawnRuntime::ExecuteEnemyAttacks(EnemyAttackExecutionSystem& system,
        EnemyAttackCoordinator& coordinator,
        EnemyBehaviorSystem& behaviorSystem) {
    system.UpdateActors(enemies_, *this, coordinator, behaviorSystem);
}

DamageResult CourseSpawnRuntime::ApplyWeaponHit(CourseActorDamageReceiver& system,
        const CourseAsset* course,
        const WeaponHitRequest& request) {
    return system.ApplyActors(enemies_, obstacles_, *this, course, request);
}
