#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "WeaponDamageSystem.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;

struct EnemyAttackInterruptDefinition final {
    float minimumSingleHitDamage = 4.0f;
    float accumulatedDamageThreshold = 8.0f;
    float interruptedCooldownSeconds = 0.65f;
    bool weakPointAlwaysInterrupts = true;

    bool Validate() const noexcept;
};

struct EnemyAttackInterruptResult final {
    uint32_t actorId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    uint64_t shotId = 0;
    float accumulatedDamage = 0.0f;
    bool eligible = false;
    bool interrupted = false;
};

struct EnemyAttackInterruptFrame final {
    std::vector<EnemyAttackInterruptResult> results;
    uint32_t eligibleHits = 0;
    uint32_t interruptedAttacks = 0;
    uint64_t revision = 0;
};

// DamageResult-driven cancellation boundary. Only attacks that have not
// emitted a volley may be interrupted.
class EnemyAttackInterruptSystem final {
public:
    EnemyAttackInterruptSystem();

    bool Initialize(const EnemyAttackInterruptDefinition& definition);
    void Reset();
    void BeginFrame();
    EnemyAttackInterruptResult Submit(
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult);

    const EnemyAttackInterruptFrame& Frame() const noexcept { return frame_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    EnemyAttackInterruptResult SubmitActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult);

    struct Accumulator final {
        uint64_t intentSequence = 0;
        float damage = 0.0f;
    };
    EnemyAttackInterruptDefinition definition_{};
    std::unordered_map<uint32_t, Accumulator> accumulatedDamage_;
    EnemyAttackInterruptFrame frame_{};
    uint64_t revision_ = 0;
};
