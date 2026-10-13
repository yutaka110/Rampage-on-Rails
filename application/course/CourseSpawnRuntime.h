#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

#include "../diagnostics/DebugDrawSystem.h"
#include "../terrain/RailPath.h"
#include "EnemyAttackCoordinator.h"
#include "EnemyAttackExecutionSystem.h"
#include "EnemyCombatSystem.h"
#include "EnemyBehaviorSystem.h"
#include "EnemyProjectileDefinitionAsset.h"
#include "EnemyProjectileSystem.h"
#include "EnemyFormationSystem.h"
#include "EnemyEntranceExitDirector.h"
#include "EnemyTargetingSystem.h"
#include "utils/math/Vector.h"
#include "utils/math/MathUtils.h"

class EffectRuntime;
class EnemyAttackInterruptSystem;
struct EnemyAttackInterruptResult;
class CourseActorDamageReceiver;
class EnemyProjectileShootDownSystem;
struct EnemyProjectileShootDownRequest;
struct EnemyProjectileShootDownResult;
struct CourseAsset;
struct TerrainGenerationSettings;
class TerrainEditLayer;
class TerrainCollisionWorld;
struct EnemyCombatPresentationSettings;
class EnemyEncounterReadabilityDirector;

enum class CourseEnemyFirePattern {
    Single,
    Twin,
    Spread,
    BossArc,
};

struct CourseEnemyFireSafetySettings {
    bool enabled = true;
    bool requireCameraAllowsFire = true;
    float minForwardDistance = 6.0f;
    float maxForwardDistance = 150.0f;
    float minVisibleBeforeFire = 0.22f;
    float blockedRetryDelay = 0.05f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseEnemyFireSafetyFrameInput {
    const RailTravelPredictionInput* travelPredictionInput = nullptr;
    const EnemyCombatPresentationSettings* presentationSettings = nullptr;
    const EnemyEncounterReadabilityDirector* readability = nullptr;
    bool cameraAllowsEnemyFire = true;
    bool cameraStableForAiming = true;
    bool cameraHardTransition = false;
    float playerDistance = 0.0f;
    float playerLateralOffset = 0.0f;
    float playerVerticalOffset = 4.0f;
    float deltaTime = 0.016f;
    std::string cameraReason = "stable";
    // Optional spatial context. Headless/editor-authored previews retain the
    // rail-distance guard even when no camera pose is available.
    const RailPath* railPath = nullptr;
    Vector3 cameraPosition{};
    bool hasCameraPosition = false;
    const Matrix4x4* viewProjection = nullptr;
    const CourseAsset* course = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    const TerrainEditLayer* terrainPreview = nullptr;
    const TerrainCollisionWorld* terrainCollision = nullptr;
};

struct CourseEnemyFireSafetyStats {
    uint32_t activeEnemies = 0;
    uint32_t allowedEnemies = 0;
    uint32_t blockedByCamera = 0;
    uint32_t blockedByRange = 0;
    uint32_t blockedByVisibilityTime = 0;
    uint32_t bulletsEmitted = 0;
    std::string lastBlockedReason = "-";
    std::string lastAllowedReason = "-";
};

struct CourseEnemyActorDesc {
    std::string waveId;
    // Stable schema-v7 placement identity. Empty for legacy/event-spawned actors.
    std::string sourcePlacementGuid;
    std::string actorAssetId;
    std::string meshId = "ball";
    std::string bulletPatternId = "single_red";
    std::string projectileDefinitionId;
    std::string role = "drone";
    float spawnDistance = 0.0f;
    float distanceOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float forwardSpeed = 0.0f;
    float radius = 1.2f;
    float lifetime = 8.0f;
    float hitPoints = 30.0f;
    float fireInterval = 0.8f;
    float firstShotDelay = 0.35f;
    float bulletSpeed = 48.0f;
    int bulletCount = 1;
    float bulletLateralSpreadSpeed = 0.0f;
    float bulletVerticalSpreadSpeed = 0.0f;
    float bulletRadius = 0.34f;
    float bulletLifetime = 4.0f;
    float bulletDamage = 8.0f;
    Vector4 bulletColor{1.0f, 0.18f, 0.08f, 1.0f};
    CourseEnemyFirePattern firePattern = CourseEnemyFirePattern::Single;
    Vector4 color{1.0f, 0.25f, 0.18f, 1.0f};
    Vector3 localRotation{};
    Vector3 localScale{1.0f, 1.0f, 1.0f};
    // Empty definitionId selects commercial defaults for production
    // ActorAssets and legacy-compatible timing for anonymous/event actors.
    EnemyCombatDefinition combatDefinition{};
    EnemyBehaviorDefinition behaviorDefinition{};
    EnemyProjectileDefinitionAsset projectileDefinition{};
    EnemyFormationDefinition formationDefinition{};
    bool previewOnly = false;
    bool suppressFire = false;
};

struct CourseObstacleActorDesc {
    std::string id;
    std::string meshId = "rock_gate";
    std::string vfxCueId;
    std::string payload;
    float spawnDistance = 0.0f;
    float distanceOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float forwardSpeed = 0.0f;
    float lifetime = 12.0f;
    float hitPoints = 80.0f;
    bool breakable = true;
    Vector3 halfExtents{3.5f, 3.0f, 3.5f};
    Vector4 color{1.0f, 0.62f, 0.12f, 1.0f};
};

struct CourseVfxCueDesc {
    std::string id;
    std::string effectName = "hit_ring";
    std::string payload;
    float spawnDistance = 0.0f;
    float distanceOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float radius = 2.5f;
    float lifetime = 4.0f;
    Vector4 color{0.30f, 0.82f, 1.0f, 1.0f};
    Vector3 worldPosition{};
    bool hasWorldPosition = false;
};

struct CourseEnemyActor {
    EnemyWeaponMountFrame weaponMount{};
    CourseEnemyActorDesc desc;
    EnemyCombatDefinition combatDefinition{};
    EnemyCombatRuntimeState combatState{};
    EnemyBehaviorDefinition behaviorDefinition{};
    EnemyBehaviorRuntimeState behaviorState{};
    EnemyAttackRuntimeState attackState{};
    EnemyTargetingRuntimeState targetingState{};
    EnemyFormationMemberRuntimeState formationState{};
    EnemyEntranceExitRuntimeState entranceExitState{};
    // Environment eligibility is independent of the readable warning timer.
    bool fireEnvironmentReady = true;
    float age = 0.0f;
    float fireTimer = 0.0f;
    float fireVisibleTime = 0.0f;
    uint64_t fireSequence = 0;
    uint32_t bulletsEmittedThisFrame = 0;
    bool fireSafetyAllowed = false;
    std::string fireSafetyReason = "not evaluated";
    // Written by EnemyEncounterReadabilityDirector after presentation. The
    // following gameplay frame may commit an attack only after the actor (or
    // its warning) has remained readable for the configured exposure window.
    bool screenPresenceEvaluated = false;
    bool screenPresenceAttackAllowed = true;
    // Encounter pacing is independent from screen readability: Establish,
    // Threaten, Recovery and Resolve deliberately hold committed fire.
    bool encounterPacingEvaluated = false;
    bool encounterPacingAttackAllowed = true;
    uint32_t actorId = 0;

    // Repeating front drones stay alive until defeated, including after their
    // source section/Wave has completed. Only scene reset clears them outright.
    bool HoldsCombatPositionUntilResolved() const {
        return desc.meshId == "twin_shield_hull" &&
            behaviorDefinition.commercialBehavior &&
            behaviorDefinition.maintainForwardEngagementBand &&
            !behaviorDefinition.choreographedAttackPass;
    }
};

struct CourseObstacleActor {
    CourseObstacleActorDesc desc;
    float age = 0.0f;
    uint32_t actorId = 0;
};

struct CourseVfxCue {
    CourseVfxCueDesc desc;
    float age = 0.0f;
    uint32_t effectInstanceId = 0;
    bool submitted = false;
};

// Retry-safe snapshot of gameplay actors. Transient VFX are deliberately not
// captured; hostile projectiles are retained in the snapshot only so a mode
// may explicitly opt into restoring them. Rail-shooter retries clear them by
// default to guarantee a readable recovery frame.
struct CourseSpawnRuntimeCheckpoint final {
    std::vector<CourseEnemyActor> enemies;
    std::vector<CourseBulletActor> bullets;
    std::vector<CourseObstacleActor> obstacles;
    uint32_t nextActorId = 1;
};

class CourseSpawnRuntime {
public:
    // 敵・弾・障害物の実体はこのクラスが所有する。
    // 戦闘/AIの公開関数はここへ処理を委譲し、Runtimeだけが非公開の
    // 更新アルゴリズムへ一時的なspanを渡す。UIや描画にはconst参照だけを渡す。
    // Purpose-specific dispatch: callers never receive mutable owned storage.
    void UpdateEnemyCombat(EnemyCombatSystem& system,
        const EnemyCombatFrameInput& input);
    bool NotifyEnemyDamage(EnemyCombatSystem& system,
        const DamageResult& damageResult,
        const WeaponFeedbackEvent* feedbackEvent);
    bool DefeatEnemy(EnemyCombatSystem& system,
        uint32_t actorId);
    void UpdateEnemyBehavior(EnemyBehaviorSystem& system,
        const EnemyBehaviorFrameInput& input);
    bool MarkEnemyBehaviorTelegraph(EnemyBehaviorSystem& system,
        uint32_t actorId,
        uint64_t attackIntentSequence);
    void RebuildEnemyAttacks(EnemyAttackCoordinator& system);
    void UpdateEnemyAttacks(EnemyAttackCoordinator& system,
        const EnemyBehaviorFrame& behaviorFrame,
        float deltaTime);
    bool MarkEnemyCoordinatorTelegraph(EnemyAttackCoordinator& system,
        uint32_t actorId,
        uint64_t intentSequence);
    void BeginEnemyFormationFrame(EnemyFormationSystem& system);
    void UpdateEnemyFormations(EnemyFormationSystem& system,
        float deltaTime, const RailPath* railPath = nullptr,
        const CourseEnemyFireSafetyFrameInput* spatialContext = nullptr);
    void BeginEnemyEntranceExitFrame(EnemyEntranceExitDirector& system);
    void UpdateEnemyEntranceExit(EnemyEntranceExitDirector& system,
        float deltaTime);
    void UpdateEnemyTargeting(EnemyTargetingSystem& system,
        const EnemyTargetingFrameInput& input);
    EnemyAttackInterruptResult InterruptEnemyAttack(EnemyAttackInterruptSystem& system,
        const DamageResult& damageResult);
    void ExecuteEnemyAttacks(EnemyAttackExecutionSystem& system,
        EnemyAttackCoordinator& coordinator,
        EnemyBehaviorSystem& behaviorSystem);
    DamageResult ApplyWeaponHit(CourseActorDamageReceiver& system,
        const CourseAsset* course,
        const WeaponHitRequest& request);
    uint32_t CommitEnemyVolley(uint32_t actorId, uint64_t intentSequence, uint64_t tokenId);
    // 消費済み・非アクティブ・寿命の状態をまとめて更新し、二重消費を拒否する。
    bool ConsumeProjectile(uint64_t projectileId);
    bool SpawnProjectile(CourseBulletActor projectile, std::string* errorMessage = nullptr);
    EnemyProjectileShootDownResult ShootDownProjectile(EnemyProjectileShootDownSystem& system,
        const RailPath& railPath, const EnemyProjectileShootDownRequest& request);
    void ClearProjectiles();
    void SetEnemyScreenPresence(uint32_t actorId, bool evaluated, bool attackAllowed);
    void SetEnemyEncounterPacing(uint32_t actorId, bool evaluated, bool attackAllowed);
    void ResetEnemyScreenPresence();
    void ResetEnemyEncounterPacing();
    bool SetEnemyRailPose(uint32_t actorId, float distanceOffset, float lateralOffset,
        float verticalOffset, float forwardSpeed);
    void RetireEnemies(std::span<const uint32_t> actorIds, bool playAuthoredExit = true);
    // 敵を消す場合、その敵が所有する弾も停止する。
    void ClearEnemies();
    void ClearObstacles();
    bool SynchronizePreviewEnemy(uint32_t actorId, CourseEnemyActorDesc desc);

    void Reset();
    CourseSpawnRuntimeCheckpoint CaptureCheckpoint() const;
    static bool ValidateCheckpoint(const CourseSpawnRuntimeCheckpoint& checkpoint,
        bool restoreProjectiles = false, std::string* errorMessage = nullptr);
    // 候補全体のID・HP・座標などを先に検証する。失敗時は現在の状態を維持する。
    bool RestoreCheckpoint(
        const CourseSpawnRuntimeCheckpoint& checkpoint,
        bool restoreProjectiles = false,
        std::string* errorMessage = nullptr);
    void Update(float deltaTime);
    void Update(float deltaTime, const CourseEnemyFireSafetyFrameInput& safetyInput);
    void EnforceEnemyEngagementClearance(const CourseEnemyFireSafetyFrameInput& safetyInput);
    bool InvalidateEnemyAttackWarning(uint32_t actorId);
    bool DeferEnemyAttackForUnreachableLaunch(uint32_t actorId);

    bool SpawnEnemyActor(CourseEnemyActorDesc desc, std::string* errorMessage = nullptr);
    bool SpawnObstacle(CourseObstacleActorDesc desc, std::string* errorMessage = nullptr);
    void SpawnVfxCue(CourseVfxCueDesc desc);
    void SubmitPendingVfx(EffectRuntime& effectRuntime, const RailPath& railPath);
    void AppendDebugDraw(ge3::debug::DebugDrawSystem& debugDraw, const RailPath& railPath) const;

    size_t ActiveEnemyCount() const { return enemies_.size(); }
    size_t ActiveBulletCount() const { return bullets_.size(); }
    size_t ActiveObstacleCount() const { return obstacles_.size(); }
    size_t ActiveVfxCueCount() const { return vfxCues_.size(); }
    // 読み取り専用。追加・削除・HP変更は上の目的別操作を使う。
    const std::vector<CourseEnemyActor>& Enemies() const { return enemies_; }
    const std::vector<CourseBulletActor>& Bullets() const { return bullets_; }
    const std::vector<CourseObstacleActor>& Obstacles() const { return obstacles_; }
    const std::vector<CourseVfxCue>& VfxCues() const { return vfxCues_; }
    void PruneDestroyedActors();
    const CourseEnemyFireSafetySettings& FireSafetySettings() const { return fireSafetySettings_; }
    bool ConfigureFireSafety(const CourseEnemyFireSafetySettings& settings,
        std::string* errorMessage = nullptr);
    const CourseEnemyFireSafetyStats& LastFireSafetyStats() const { return fireSafetyStats_; }
    const EnemyCombatSystem& EnemyCombat() const noexcept { return enemyCombatSystem_; }
    EnemyCombatSystem& EnemyCombat() noexcept { return enemyCombatSystem_; }
    const EnemyBehaviorSystem& EnemyBehavior() const noexcept { return enemyBehaviorSystem_; }
    EnemyBehaviorSystem& EnemyBehavior() noexcept { return enemyBehaviorSystem_; }
    const EnemyAttackCoordinator& EnemyAttacks() const noexcept {
        return enemyAttackCoordinator_;
    }
    EnemyAttackCoordinator& EnemyAttacks() noexcept {
        return enemyAttackCoordinator_;
    }
    const EnemyAttackExecutionSystem& EnemyAttackExecution() const noexcept {
        return enemyAttackExecutionSystem_;
    }
    const EnemyTargetingSystem& EnemyTargeting() const noexcept {
        return enemyTargetingSystem_;
    }
    const EnemyProjectileSystem& EnemyProjectiles() const noexcept {
        return enemyProjectileSystem_;
    }
    const EnemyFormationSystem& EnemyFormations() const noexcept {
        return enemyFormationSystem_;
    }
    EnemyFormationSystem& EnemyFormations() noexcept {
        return enemyFormationSystem_;
    }
    const EnemyEntranceExitDirector& EnemyEntranceExit() const noexcept {
        return enemyEntranceExitDirector_;
    }
    EnemyEntranceExitDirector& EnemyEntranceExit() noexcept {
        return enemyEntranceExitDirector_;
    }
    bool MarkEnemyAttackTelegraphPresented(
        uint32_t actorId,
        uint64_t attackIntentSequence);

private:
    bool CanEnemyFire(CourseEnemyActor& enemy, const CourseEnemyFireSafetyFrameInput& safetyInput, float dt);
    bool UpdateEnemyFireEnvironment(CourseEnemyActor& enemy, const CourseEnemyFireSafetyFrameInput& safetyInput, float dt);
    uint32_t EmitEnemyBullets(const CourseEnemyActor& enemy);

    std::vector<CourseEnemyActor> enemies_;
    std::vector<CourseBulletActor> bullets_;
    std::vector<CourseObstacleActor> obstacles_;
    std::vector<CourseVfxCue> vfxCues_;
    CourseEnemyFireSafetySettings fireSafetySettings_{};
    CourseEnemyFireSafetyStats fireSafetyStats_{};
    EnemyCombatSystem enemyCombatSystem_{};
    EnemyBehaviorSystem enemyBehaviorSystem_{};
    EnemyAttackCoordinator enemyAttackCoordinator_{};
    EnemyAttackExecutionSystem enemyAttackExecutionSystem_{};
    EnemyTargetingSystem enemyTargetingSystem_{};
    EnemyProjectileSystem enemyProjectileSystem_{};
    EnemyFormationSystem enemyFormationSystem_{};
    EnemyEntranceExitDirector enemyEntranceExitDirector_{};
    uint32_t nextActorId_ = 1;
};
