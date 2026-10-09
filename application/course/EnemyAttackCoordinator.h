#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "EnemyBehaviorSystem.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;

enum class EnemyAttackRuntimePhase : uint8_t {
    Idle,
    Queued,
    Reserved,
    Telegraphing,
    Ready,
    Executing,
    Recovery,
    Cancelled,
};

enum class EnemyAttackCancelReason : uint8_t {
    None,
    IntentSuperseded,
    ActorUnavailable,
    ReservationExpired,
    RuntimeReset,
    PlayerInterrupted,
};

// Checkpoint-safe per-actor attack state. The coordinator owns admission and
// token lifetime; the execution system is the only writer of committed fire.
struct EnemyAttackRuntimeState final {
    EnemyAttackRuntimePhase phase = EnemyAttackRuntimePhase::Idle;
    EnemyAttackCancelReason cancelReason = EnemyAttackCancelReason::None;
    std::string bulletPatternId;
    uint64_t intentSequence = 0;
    uint64_t tokenId = 0;
    uint64_t deterministicSeed = 0;
    uint64_t revision = 0;
    float queuedSeconds = 0.0f;
    float reservedSeconds = 0.0f;
    float recoveryRemaining = 0.0f;
    float priority = 0.0f;
    float threatCost = 0.0f;
    uint32_t emittedProjectiles = 0;
    uint32_t emittedVolleys = 0;
    bool tokenReserved = false;
    bool telegraphPresented = false;
    bool committedThisFrame = false;
};

struct EnemyAttackCoordinatorSettings final {
    bool enabled = true;
    uint32_t maximumConcurrentAttackers = 3;
    uint32_t maximumAttackersPerWave = 2;
    uint32_t maximumAttackersPerSector = 2;
    float maximumThreatBudget = 3.4f;
    float maximumReservationSeconds = 3.0f;
    float tokenRecoverySeconds = 0.18f;
    float waitingPriorityPerSecond = 0.12f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct EnemyAttackCoordinatorFrame final {
    uint32_t queuedAttacks = 0;
    uint32_t reservedAttacks = 0;
    uint32_t readyAttacks = 0;
    uint32_t grantedThisFrame = 0;
    uint32_t releasedThisFrame = 0;
    uint32_t cancelledThisFrame = 0;
    uint32_t budgetBlockedThisFrame = 0;
    float occupiedThreatBudget = 0.0f;
    uint64_t revision = 0;
};

// Arbitrates global attack pressure. It deliberately owns no actor container;
// every reservation is reconstructible from checkpointed actor runtime state.
class EnemyAttackCoordinator final {
public:
    void Reset();
    void RebuildFromRuntime(CourseSpawnRuntime& runtime);
    void InitializeActor(CourseEnemyActor& actor);
    void Update(
        CourseSpawnRuntime& runtime,
        const EnemyBehaviorFrame& behaviorFrame,
        float deltaTime);

    bool MarkTelegraphPresented(
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t intentSequence);
    bool CanExecute(const CourseEnemyActor& actor) const noexcept;
    void DeferUnreachableAttack(CourseEnemyActor& actor);
    void RestartTelegraphForAimChange(CourseEnemyActor& actor);
    bool NotifyExecutionStarted(CourseEnemyActor& actor);
    bool NotifyExecutionCommitted(
        CourseEnemyActor& actor,
        uint32_t emittedProjectiles);
    bool CancelActor(
        CourseEnemyActor& actor,
        EnemyAttackCancelReason reason);

    const EnemyAttackCoordinatorSettings& Settings() const noexcept {
        return settings_;
    }
    bool Configure(const EnemyAttackCoordinatorSettings& settings,
        std::string* errorMessage = nullptr);
    const EnemyAttackCoordinatorFrame& Frame() const noexcept { return frame_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void RebuildFromRuntimeActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime);
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyBehaviorFrame& behaviorFrame,
        float deltaTime);
    bool MarkTelegraphPresentedActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t intentSequence);

    void QueueIntent(CourseEnemyActor& actor, const EnemyAttackIntent& intent);
    void GrantToken(CourseEnemyActor& actor);
    void ReleaseToken(CourseEnemyActor& actor);

    EnemyAttackCoordinatorSettings settings_{};
    EnemyAttackCoordinatorFrame frame_{};
    uint64_t nextTokenId_ = 1;
    uint64_t revision_ = 0;
};

const char* ToString(EnemyAttackRuntimePhase phase) noexcept;
const char* ToString(EnemyAttackCancelReason reason) noexcept;
