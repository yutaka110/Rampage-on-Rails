#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "EnemyCombatSystem.h"
#include "EnemyAttackDefenseResult.h"
#include "EnemyEncounterPacingDirector.h"
#include "GrazeScoreSystem.h"
#include "PlayerDamageSystem.h"
#include "RailLockVolley.h"
#include "EnemyProjectileShootDownSystem.h"

class CourseSpawnRuntime;
class WeaponFeedbackSystem;

enum class EncounterPerformanceScoreKind : uint8_t {
    EnemyDefeat,
    EncounterClear,
    CleanClear,
    LockSweep,
    BatteryBreak,
    ProjectileIntercept,
};

struct EncounterPerformanceScoreSettings final {
    uint32_t defeatBaseScore = 90;
    uint32_t defeatConcurrencyStep = 20;
    uint32_t defeatThreatStep = 20;
    uint32_t clearBaseScore = 220;
    uint32_t clearConcurrencyStep = 70;
    uint32_t clearThreatStep = 60;
    uint32_t fullSweepBonus = 120;
    float cleanClearMultiplier = 1.50f;
    size_t maximumResultsPerFrame = 16;
    uint32_t lockSweepScore = 350;
    uint32_t batteryBreakScore = 250;
    uint32_t projectileInterceptScore = 220;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct EncounterPerformanceScoreResult final {
    EncounterPerformanceScoreKind kind =
        EncounterPerformanceScoreKind::EnemyDefeat;
    uint32_t actorId = 0;
    std::string beatGuid;
    std::string encounterId;
    uint32_t scoreAwarded = 0;
    uint32_t chainAfter = 0;
    uint32_t defeatedActors = 0;
    uint32_t peakEligibleActors = 0;
    bool fullSweep = false;
    bool accepted = false;
};

struct EncounterPerformanceScoreRuntimeState final {
    uint64_t totalScore = 0;
    uint32_t chain = 0;
    uint32_t maximumChain = 0;
    std::string activeBeatGuid;
    std::string activeEncounterId;
    std::string activeWaveGuid;
    uint32_t maximumConcurrentAttackers = 1;
    float maximumThreatBudget = 1.0f;
    uint32_t activeDefeatedActors = 0;
    uint32_t activePeakEligibleActors = 0;
    bool activeBeatNoDamage = true;
    std::vector<uint32_t> defeatedActorIds;
    std::vector<std::string> completedBeatGuids;
    uint64_t revision = 0;
    std::vector<std::string> showcaseKeys;
    std::vector<uint32_t> interceptOwnerIds;
    std::vector<uint64_t> interceptProjectileIds;
    float interceptWindowRemaining = 0.0f;
    uint32_t interceptCount = 0;
    EncounterPerformanceScoreKind showcaseKind = EncounterPerformanceScoreKind::EnemyDefeat;
    float showcaseSecondsRemaining = 0.0f;
    uint32_t showcaseCount = 0;
    uint32_t showcaseScore = 0;
};

struct EncounterPerformanceScoreInput final {
    const EnemyEncounterPacingFrame* pacing = nullptr;
    std::span<const EnemyCombatEvent> combatEvents{};
    std::span<const EnemyAttackDefenseResult> defenseResults{};
    std::span<const GrazeScoreResult> grazeResults{};
    std::span<const PlayerDamageResult> damageResults{};
    bool gameplayActive = true;
    bool showcasesEnabled = false;
    float deltaTime = 0.0f;
    const CourseSpawnRuntime* runtime = nullptr;
    const WeaponFeedbackSystem* weaponFeedback = nullptr;
    std::span<const RailLockVolleyResult> lockVolleys{};
    std::span<const EnemyProjectileShootDownResult> shootDownResults{};
};

// Adds the missing run-level reward layer above hit/graze scoring. Difficulty
// comes from the authored encounter concurrency and threat budget, so a harder
// Beat automatically pays more without duplicating per-course score tables.
class EncounterPerformanceScoreSystem final {
public:
    void Reset();
    void Update(
        const EncounterPerformanceScoreInput& input,
        const EncounterPerformanceScoreSettings& settings = {});
    bool RestoreState(
        const EncounterPerformanceScoreRuntimeState& state,
        std::string* errorMessage = nullptr);

    const EncounterPerformanceScoreRuntimeState& State() const noexcept {
        return state_;
    }
    const std::vector<EncounterPerformanceScoreResult>& ResultsThisFrame()
        const noexcept {
        return resultsThisFrame_;
    }

private:
    void BeginBeat(
        const EnemyEncounterPacingFrame& pacing,
        const EnemyEncounterPacingEvent& event);
    void AddResult(EncounterPerformanceScoreResult result, size_t maximumResults);
    void UpdateShowcases(const EncounterPerformanceScoreInput& input,
        const EncounterPerformanceScoreSettings& settings);
    void AwardShowcase(EncounterPerformanceScoreKind kind, const std::string& key,
        uint32_t count, uint32_t score, size_t maximumResults);

    EncounterPerformanceScoreRuntimeState state_{};
    std::vector<EncounterPerformanceScoreResult> resultsThisFrame_;
};

const char* ToString(EncounterPerformanceScoreKind kind) noexcept;
