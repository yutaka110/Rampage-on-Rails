#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EnemyProjectileDefinitionAsset.h"
#include "utils/math/Vector.h"

struct CourseEnemyActor;

// Runtime projectile representation. Legacy CourseBulletActor is retained as
// an alias so existing render/collision consumers migrate without duplication.
struct EnemyProjectileRuntimeState final {
    uint64_t projectileId = 0;
    uint32_t ownerActorId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    uint64_t deterministicSeed = 0;
    std::string definitionId;
    std::string sourceRole;
    EnemyProjectileTrajectory trajectory = EnemyProjectileTrajectory::Direct;
    float spawnDistance = 0.0f;
    float distanceOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float previousDistanceOffset = 0.0f;
    float previousLateralOffset = 0.0f;
    float previousVerticalOffset = 0.0f;
    float forwardSpeed = -48.0f;
    float lateralSpeed = 0.0f;
    float verticalSpeed = 0.0f;
    float acceleration = 0.0f;
    float maximumSpeed = 48.0f;
    float homingTurnRateRadians = 0.0f;
    float arcGravity = 0.0f;
    float radius = 0.34f;
    float lifetime = 4.0f;
    float age = 0.0f;
    float damage = 8.0f;
    Vector4 color{1.0f, 0.18f, 0.08f, 1.0f};
    std::string trailEffectId;
    std::string impactEffectId = "ice_impact";
    EnemyAttackDefenseResponse defenseResponses =
        EnemyAttackDefenseResponse::ShootDown |
        EnemyAttackDefenseResponse::Interrupt |
        EnemyAttackDefenseResponse::LeanLeft |
        EnemyAttackDefenseResponse::LeanRight;
    float shootDownHitPoints = 1.0f;
    float shootDownMaximumHitPoints = 1.0f;
    float shootDownRadiusScale = 1.65f;
    bool shootDownEnabled = true;
    float lockedTargetDistance = 0.0f;
    float lockedTargetLateralOffset = 0.0f;
    float lockedTargetVerticalOffset = 0.0f;
    bool initialized = false;
    bool active = true;
    bool hitConsumed = false;
};

using CourseBulletActor = EnemyProjectileRuntimeState;

// Side-effect-free launch template consumed by both warnings and SpawnVolley.
EnemyProjectileRuntimeState ResolveEnemyProjectileLaunch(const CourseEnemyActor& actor, int index);
struct EnemyProjectileLaunchSolution final {
    EnemyProjectileRuntimeState projectile;
    float flightSeconds = 0.0f;
};
// A preview at the scheduled launch time. The execution template above calls
// this same resolver with delay zero; neither resolver consumes projectile IDs.
EnemyProjectileLaunchSolution ResolveEnemyProjectileScheduledLaunch(
    const CourseEnemyActor& actor, int index, float launchDelaySeconds);
// Shared fixed-trajectory sampling/integration. Homing still uses its live
// target in Update; ordinary Direct/Predictive shots never reacquire a target.
void AdvanceEnemyProjectileTrajectory(EnemyProjectileRuntimeState& projectile, float deltaTime);

struct EnemyProjectileFrameInput final {
    float deltaTime = 0.0f;
    float playerDistance = 0.0f;
    float playerLateralOffset = 0.0f;
    float playerVerticalOffset = 4.0f;
};

struct EnemyProjectileFrame final {
    uint32_t activeProjectiles = 0;
    uint32_t spawnedThisFrame = 0;
    uint32_t homingProjectiles = 0;
    uint32_t expiredThisFrame = 0;
    uint64_t revision = 0;
};

class EnemyProjectileSystem final {
public:
    void Reset();
    void RebuildFromProjectiles(
        std::vector<EnemyProjectileRuntimeState>& projectiles);
    bool SpawnProjectile(EnemyProjectileRuntimeState projectile,
        std::vector<EnemyProjectileRuntimeState>& projectiles);
    uint32_t SpawnVolley(
        const CourseEnemyActor& actor,
        std::vector<EnemyProjectileRuntimeState>& projectiles);
    void Update(
        std::vector<EnemyProjectileRuntimeState>& projectiles,
        const EnemyProjectileFrameInput& input);

    const EnemyProjectileFrame& Frame() const noexcept { return frame_; }

private:
    void InitializeLegacy(EnemyProjectileRuntimeState& projectile);

    EnemyProjectileFrame frame_{};
    uint32_t pendingSpawned_ = 0;
    uint64_t nextProjectileId_ = 1;
    uint64_t revision_ = 0;
};
