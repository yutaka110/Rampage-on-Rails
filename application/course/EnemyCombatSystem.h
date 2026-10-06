#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "WeaponFeedbackSystem.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;
struct CourseEnemyActorDesc;

enum class EnemyCombatPhase : uint8_t {
    Spawning,
    Engaging,
    Telegraphing,
    Attacking,
    Recovering,
    HitReact,
    Dying,
    Retired,
};

// Immutable, runtime-resolved combat contract for one enemy actor. Production
// ActorAssets automatically receive the commercial defaults; legacy/event-only
// actors retain their existing timing unless they provide an explicit override.
struct EnemyCombatDefinition final {
    std::string definitionId;
    float maximumHitPoints = 0.0f;
    float spawnDurationSeconds = 0.0f;
    float engageDurationSeconds = 0.0f;
    float telegraphLeadSeconds = 0.35f;
    float attackCommitSeconds = 0.10f;
    float recoverySeconds = 0.0f;
    float hitReactSeconds = 0.10f;
    float deathDurationSeconds = 0.0f;
    float spawnScale = 0.72f;
    float minimumDeathScale = 0.08f;
    bool commercialStateMachine = false;
    bool targetableWhileSpawning = false;
    bool damageableWhileSpawning = false;
    bool pauseAttackDuringHitReact = true;

    static EnemyCombatDefinition LegacyCompatible();
    static EnemyCombatDefinition CommercialStandard();
    bool Validate(std::string* errorMessage = nullptr) const;
};

struct EnemyCombatRuntimeState final {
    EnemyCombatPhase phase = EnemyCombatPhase::Spawning;
    EnemyCombatPhase resumePhase = EnemyCombatPhase::Telegraphing;
    float phaseElapsedSeconds = 0.0f;
    float currentHitPoints = 0.0f;
    float presentationScale = 1.0f;
    float presentationAlpha = 1.0f;
    float hitFlash = 0.0f;
    float deathProgress = 0.0f;
    uint64_t observedFireSequence = 0;
    uint64_t lastDamageShotId = 0;
    uint64_t revision = 0;
    bool initialized = false;
    bool canBeTargeted = true;
    bool canReceiveDamage = true;
    bool canTelegraph = true;
    bool canFire = true;
    bool defeatEventPublished = false;
};

enum class EnemyCombatEventKind : uint8_t {
    Spawned,
    Engaged,
    TelegraphStarted,
    AttackCommitted,
    HitReacted,
    Defeated,
    Retired,
};

struct EnemyCombatEvent final {
    EnemyCombatEventKind kind = EnemyCombatEventKind::Spawned;
    uint32_t actorId = 0;
    uint64_t shotId = 0;
    std::string definitionId;
    std::string actorAssetId;
    std::string placementGuid;
    std::string waveId;
    float appliedDamage = 0.0f;
    float remainingHitPoints = 0.0f;
    HitFeedbackKind feedbackKind = HitFeedbackKind::None;
};

struct EnemyCombatFrameStats final {
    uint32_t activeActors = 0;
    uint32_t spawningActors = 0;
    uint32_t telegraphingActors = 0;
    uint32_t attackingActors = 0;
    uint32_t reactingActors = 0;
    uint32_t dyingActors = 0;
    uint32_t eventsPublished = 0;
    uint64_t revision = 0;
};

struct EnemyCombatFrameInput final {
    float deltaTime = 0.0f;
    float playerDistance = 0.0f;
};

// Authoritative enemy lifecycle state machine. CourseSpawnRuntime still owns
// actor storage and projectile integration, while every combat eligibility
// decision is derived from EnemyCombatRuntimeState.
class EnemyCombatSystem final {
public:
    void Reset();
    void InitializeActor(CourseEnemyActor& actor);
    void Update(CourseSpawnRuntime& runtime, const EnemyCombatFrameInput& input);

    bool SubmitDamageResult(
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult,
        const WeaponFeedbackEvent* feedbackEvent = nullptr);
    bool ForceDefeat(CourseSpawnRuntime& runtime, uint32_t actorId);

    std::vector<EnemyCombatEvent> ConsumeEvents();
    const EnemyCombatFrameStats& FrameStats() const noexcept { return frameStats_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyCombatFrameInput& input);
    bool SubmitDamageResultActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult,
        const WeaponFeedbackEvent* feedbackEvent);
    bool ForceDefeatActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId);

    void EnterPhase(
        CourseEnemyActor& actor,
        EnemyCombatPhase phase,
        EnemyCombatEventKind eventKind,
        bool publishEvent = true);
    void ApplyPhaseGates(CourseEnemyActor& actor);
    void QueueEvent(
        const CourseEnemyActor& actor,
        EnemyCombatEventKind kind,
        uint64_t shotId = 0,
        float appliedDamage = 0.0f,
        HitFeedbackKind feedbackKind = HitFeedbackKind::None);

    std::vector<EnemyCombatEvent> pendingEvents_;
    EnemyCombatFrameStats frameStats_{};
    uint64_t revision_ = 0;
};

EnemyCombatDefinition ResolveEnemyCombatDefinition(
    const CourseEnemyActorDesc& actorDescription);
const char* ToString(EnemyCombatPhase phase) noexcept;
const char* ToString(EnemyCombatEventKind kind) noexcept;
