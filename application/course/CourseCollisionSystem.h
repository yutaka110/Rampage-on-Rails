#pragma once

#include <cstdint>

#include "../diagnostics/DebugDrawSystem.h"
#include "../terrain/RailPath.h"
#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"
#include "PlayerDamageSystem.h"
#include "PlayerHitboxSystem.h"
#include "PlayerNearMissSystem.h"
#include "EnemyProjectileShootDownSystem.h"
#include "EnemyAttackInterruptSystem.h"
#include "RailAimState.h"
#include "WeaponDamageSystem.h"
#include "WeaponDefinitionRegistry.h"
#include "WeaponFeedbackSystem.h"
#include "WeaponFireSystem.h"
#include "RailPulseShotPresentation.h"

struct RailVehicleCombatMountFrame;
struct TerrainGenerationSettings;

struct CourseCollisionPlayerState {
    float distance = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 4.0f;
    float radius = 1.6f;
    float hitPoints = 100.0f;
    float maximumHitPoints = 100.0f;
    float invulnerabilityTime = 0.0f;
    bool dodgeActive = false;
    float hurtRadiusScale = 1.0f;
};

struct CourseCollisionWeaponState {
    bool enabled = true;
    bool triggerHeld = false;
    bool triggerPressed = false;
    bool triggerReleased = false;
    float shotTimer = 0.0f;
    float shotInterval = 0.12f;
    float range = 96.0f;
    float radius = RailPulseShot::ContactRadius;
    float damage = 18.0f;
    float damageMultiplier = 1.0f;
    bool assistEnabled = false;
    float assistLateralOffset = 0.0f;
    float assistVerticalOffset = 4.0f;
    float muzzleForwardOffset = 3.2f;
    float tracerForwardDistance = 28.0f;
    float muzzleRadius = 0.9f;
    float tracerRadius = 1.0f;
};

struct CourseCollisionFrameInput {
    float deltaTime = 0.016f;
    const CourseAsset* course = nullptr;
    const RailAimState* worldAim = nullptr;
    const RailPath* railPath = nullptr;
    CourseCollisionPlayerState player;
    CourseCollisionWeaponState weapon;
    // Mounted vehicle body collision is resolved by
    // RailVehicleBodyCollisionSystem. Suppress the legacy occupant-sphere
    // obstacle/terrain loops so one contact can never subtract HP twice.
    bool externalBodyCollisionAuthority = false;
    // Target-bound primary reward; interception rays and other actors retain
    // the weapon's normal damage. The host owns this opportunity's lifetime.
    uint32_t bonusTargetActorId = 0;
    float bonusDamageMultiplier = 1.0f;
    const RailVehicleCombatMountFrame* combatMount = nullptr;
    // Use a visible gatling round's depth for primary convergence, preserving
    // the player's camera ray and the actual mounted muzzle.
    bool convergePrimaryOnGatling = false;
    const TerrainGenerationSettings* primaryTerrainSettings = nullptr;
    const TerrainEditLayer* primaryTerrainPreview = nullptr;
};

struct CourseCollisionFrameStats {
    uint32_t enemyBulletHits = 0;
    uint32_t obstacleHits = 0;
    uint32_t playerHitsRejected = 0;
    uint32_t playerNearMisses = 0;
    uint32_t playerShotsFired = 0;
    uint32_t playerShotWorldHits = 0;
    uint32_t playerShotEnemyHits = 0;
    uint32_t playerShotObstacleHits = 0;
    uint32_t playerShotTerrainHits = 0;
    uint32_t playerShotStaleHits = 0;
    uint32_t enemyProjectilesShotDown = 0;
    uint32_t enemyAttacksInterrupted = 0;
    float playerDamage = 0.0f;
};

// One endpoint per accepted primary shot, including catch-up salvos. Tracers
// must not reuse the final shot's endpoint or create extra damage requests.
struct CoursePlayerShotTrace final {
    uint64_t shotId = 0;
    Vector3 end{};
    float presentationAge = 0.0f;
    Vector3 start{};
    float radius = RailPulseShot::ContactRadius;
};

class CourseCollisionSystem {
public:
    CourseCollisionSystem();
    void Reset();
    void SynchronizePlayerHitPoints(
        float hitPoints,
        float maximumHitPoints = 100.0f);
    CourseCollisionFrameStats Update(CourseSpawnRuntime& runtime, const CourseCollisionFrameInput& input);
    void AppendDebugDraw(ge3::debug::DebugDrawSystem& debugDraw, const RailPath& railPath) const;

    const CourseCollisionPlayerState& Player() const { return player_; }
    const CourseCollisionWeaponState& Weapon() const { return weapon_; }
    const CourseCollisionFrameStats& LastFrameStats() const { return lastFrameStats_; }
    const PlayerDamageSystem& PlayerDamage() const noexcept {
        return playerDamageSystem_;
    }
    PlayerDamageSystem& PlayerDamage() noexcept { return playerDamageSystem_; }
    const std::vector<PlayerDamageResult>& PlayerDamageResults() const noexcept {
        return playerDamageSystem_.ResultsThisFrame();
    }
    const PlayerDamageResult& LastPlayerDamageResult() const noexcept {
        return playerDamageSystem_.LastResult();
    }
    const PlayerHitboxSystem& PlayerHitbox() const noexcept {
        return playerHitboxSystem_;
    }
    PlayerHitboxSystem& PlayerHitbox() noexcept { return playerHitboxSystem_; }
    const PlayerNearMissSystem& PlayerNearMiss() const noexcept {
        return playerNearMissSystem_;
    }
    PlayerNearMissSystem& PlayerNearMiss() noexcept {
        return playerNearMissSystem_;
    }
    const std::vector<PlayerNearMissResult>& PlayerNearMissResults() const noexcept {
        return playerNearMissSystem_.ResultsThisFrame();
    }
    bool LastShotVisible() const { return lastShotVisible_; }
    float LastShotDistance() const { return lastShotDistance_; }
    float LastShotLateralOffset() const { return lastShotLateralOffset_; }
    float LastShotVerticalOffset() const { return lastShotVerticalOffset_; }
    const Vector3& LastShotWorldPoint() const { return lastShotWorldPoint_; }
    const Vector3& LastShotWorldNormal() const { return lastShotWorldNormal_; }
    RailAimHitKind LastShotHitKind() const { return lastShotHitKind_; }
    uint32_t LastShotHitActorId() const { return lastShotHitActorId_; }
    bool LastShotHasWorldPoint() const { return lastShotHasWorldPoint_; }
    bool LastShotHasWorldHit() const { return lastShotHasWorldHit_; }
    const WeaponHitRequest& LastWeaponHitRequest() const { return lastWeaponHitRequest_; }
    const DamageResult& LastDamageResult() const { return lastDamageResult_; }
    const WeaponFeedbackDispatchResult& LastWeaponFeedbackResult() const {
        return lastWeaponFeedbackResult_;
    }
    const WeaponFeedbackSystem& WeaponFeedback() const { return weaponFeedbackSystem_; }
    const WeaponFireResult& LastWeaponFireResult() const { return lastWeaponFireResult_; }
    const std::vector<CoursePlayerShotTrace>& PlayerShotTraces() const { return playerShotTraces_; }
    const EnemyProjectileShootDownSystem& ProjectileShootDown() const noexcept {
        return projectileShootDownSystem_;
    }
    const EnemyAttackInterruptSystem& AttackInterrupt() const noexcept {
        return attackInterruptSystem_;
    }
    WeaponFireSystem& WeaponFire() { return weaponFireSystem_; }
    const WeaponFireSystem& WeaponFire() const { return weaponFireSystem_; }
    bool LoadWeaponDefinitions(
        const std::filesystem::path& directory,
        std::string* errorMessage = nullptr);
    WeaponDefinitionReloadReport ReloadChangedWeaponDefinitions();
    const WeaponDefinitionAsset* FindWeaponDefinition(const std::string& weaponId) const {
        return weaponDefinitionRegistry_.Find(weaponId);
    }
    const WeaponDefinitionRegistry& WeaponDefinitions() const {
        return weaponDefinitionRegistry_;
    }
    WeaponFireResult UpdateWeaponFire(const WeaponFireInput& input);
    DamageResult ApplyWeaponHit(
        CourseSpawnRuntime& runtime,
        const CourseAsset* course,
        const WeaponHitRequest& request);
    PlayerDamageResult ApplyPlayerHit(const PlayerHitRequest& request);

private:
    void FirePlayerShot(
        CourseSpawnRuntime& runtime,
        const CourseCollisionFrameInput& input,
        const WeaponShot& shot);
    PlayerDamageResult SubmitPlayerHit(const PlayerHitRequest& request);
    void LogFrameStats(const CourseCollisionFrameStats& stats) const;

    CourseCollisionPlayerState player_{};
    CourseCollisionWeaponState weapon_{};
    CourseCollisionFrameStats lastFrameStats_{};
    PlayerDamageSystem playerDamageSystem_{};
    PlayerHitboxSystem playerHitboxSystem_{};
    PlayerNearMissSystem playerNearMissSystem_{};
    EnemyProjectileShootDownSystem projectileShootDownSystem_{};
    EnemyAttackInterruptSystem attackInterruptSystem_{};
    float lastShotDistance_ = 0.0f;
    float lastShotLateralOffset_ = 0.0f;
    float lastShotVerticalOffset_ = 0.0f;
    Vector3 lastShotWorldPoint_{};
    Vector3 lastShotWorldNormal_{};
    RailAimHitKind lastShotHitKind_ = RailAimHitKind::None;
    uint32_t lastShotHitActorId_ = 0;
    bool lastShotHasWorldPoint_ = false;
    bool lastShotHasWorldHit_ = false;
    bool lastShotVisible_ = false;
    WeaponHitRequest lastWeaponHitRequest_{};
    DamageResult lastDamageResult_{};
    WeaponFeedbackDispatchResult lastWeaponFeedbackResult_{};
    WeaponFireResult lastWeaponFireResult_{};
    std::vector<CoursePlayerShotTrace> playerShotTraces_;
    WeaponDefinitionRegistry weaponDefinitionRegistry_{};
    WeaponFireSystem weaponFireSystem_{};
    WeaponFeedbackSystem weaponFeedbackSystem_{};
    CourseActorDamageReceiver damageReceiver_{};
};
