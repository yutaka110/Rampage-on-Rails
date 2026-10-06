#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class CourseSpawnRuntime;
struct CourseEnemyActor;
struct CourseEnemyActorDesc;

enum class EnemyBehaviorArchetype : uint8_t {
    Assault,
    Flanker,
    Sniper,
    Interceptor,
    Turret,
    Support,
    Boss,
};

enum class EnemyBehaviorState : uint8_t {
    Dormant,
    Entering,
    Positioning,
    Aiming,
    RequestingAttack,
    Evading,
    Repositioning,
    Retreating,
    Disabled,
};

// Immutable behavior contract resolved from an ActorAsset before gameplay.
struct EnemyBehaviorDefinition final {
    std::string definitionId;
    EnemyBehaviorArchetype archetype = EnemyBehaviorArchetype::Assault;
    float entryDurationSeconds = 0.34f;
    float positioningDurationSeconds = 0.30f;
    float aimingDurationSeconds = 0.24f;
    float attackLeadSeconds = 0.55f;
    float attackCooldownSeconds = 0.80f;
    float evadeDurationSeconds = 0.26f;
    float repositionDurationSeconds = 0.34f;
    float lateralAmplitude = 1.8f;
    float verticalAmplitude = 0.65f;
    float movementFrequency = 0.72f;
    float forwardMotionScale = 1.0f;
    float maximumBankRadians = 0.28f;
    float engagementBandMinimumForwardDistance = 24.0f;
    float engagementBandPreferredForwardDistance = 46.0f;
    float engagementBandMaximumForwardDistance = 68.0f;
    float engagementBandDisengageForwardDistance = 10.0f;
    float engagementBandPositionGain = 2.0f;
    float engagementBandMaximumCorrectionSpeed = 30.0f;
    float engagementBandVelocityResponse = 8.0f;
    // Single-volley encounter choreography. Delays do not extend the warning.
    float attackPassStartDelaySeconds = 0.0f;
    float attackPassRecoilSeconds = 0.20f;
    // Allow the next actor to prepare during this final portion of the warning.
    // Zero preserves fully sequential setpiece choreography.
    float attackPassHandoffLeadSeconds = 0.0f;
    bool commercialBehavior = false;
    bool movementEnabled = true;
    bool requireTelegraphPresentation = true;
    bool maintainForwardEngagementBand = false;
    bool choreographedAttackPass = false;

    static EnemyBehaviorDefinition LegacyDirect();
    static EnemyBehaviorDefinition Commercial(
        EnemyBehaviorArchetype archetype);
    bool Validate(std::string* errorMessage = nullptr) const;
};

struct EnemyBehaviorRuntimeState final {
    EnemyBehaviorState state = EnemyBehaviorState::Dormant;
    float stateElapsedSeconds = 0.0f;
    float attackCooldownRemaining = 0.0f;
    float attackTimeRemaining = 0.0f;
    float authoredForwardOffset = 0.0f;
    float authoredLateralOffset = 0.0f;
    float authoredVerticalOffset = 0.0f;
    float integratedForwardOffset = 0.0f;
    float behaviorForwardOffset = 0.0f;
    float behaviorLateralOffset = 0.0f;
    float behaviorVerticalOffset = 0.0f;
    // Persistent outward clearance correction for emergency side departure.
    float safetyLateralOffset = 0.0f;
    float presentationYawRadians = 0.0f;
    float presentationPitchRadians = 0.0f;
    float presentationBankRadians = 0.0f;
    float deterministicPhase = 0.0f;
    float engagementBandForwardDistance = 0.0f;
    float engagementBandVelocity = 0.0f;
    float attackPassHoldForwardDistance = 0.0f;
    uint64_t attackIntentSequence = 0;
    uint64_t committedAttackSequence = 0;
    uint64_t revision = 0;
    bool initialized = false;
    bool attackIntentActive = false;
    bool telegraphPresented = false;
    bool engagementBandAttackAllowed = true;
    bool engagementBandExitRequested = false;
};

struct EnemyAttackIntent final {
    uint32_t actorId = 0;
    uint64_t sequence = 0;
    EnemyBehaviorArchetype archetype = EnemyBehaviorArchetype::Assault;
    float timeToCommit = 0.0f;
    float severity = 0.0f;
    std::string waveId;
    std::string placementGuid;
    bool telegraphPresented = false;
    bool readyToCommit = false;
};

enum class EnemyBehaviorEventKind : uint8_t {
    StateChanged,
    AttackRequested,
    AttackCommitted,
    Disabled,
};

struct EnemyBehaviorEvent final {
    EnemyBehaviorEventKind kind = EnemyBehaviorEventKind::StateChanged;
    uint32_t actorId = 0;
    uint64_t attackIntentSequence = 0;
    EnemyBehaviorState state = EnemyBehaviorState::Dormant;
    std::string waveId;
    std::string placementGuid;
};

struct EnemyBehaviorFrameInput final {
    float deltaTime = 0.0f;
    float playerDistance = 0.0f;
};

struct EnemyBehaviorFrame final {
    std::vector<EnemyAttackIntent> attackIntents;
    uint32_t activeActors = 0;
    uint32_t movingActors = 0;
    uint32_t waitingForTelegraph = 0;
    uint32_t readyAttacks = 0;
    uint32_t engagementBandActors = 0;
    uint32_t engagementBandCorrections = 0;
    uint32_t engagementBandSuppressedAttacks = 0;
    uint32_t engagementBandForcedExits = 0;
    uint64_t revision = 0;
};

class EnemyBehaviorSystem final {
public:
    void Reset();
    void InitializeActor(CourseEnemyActor& actor);
    void Update(CourseSpawnRuntime& runtime, const EnemyBehaviorFrameInput& input);

    bool MarkTelegraphPresented(
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t attackIntentSequence);
    bool CanCommitAttack(const CourseEnemyActor& actor) const noexcept;
    bool NotifyAttackCommitted(CourseEnemyActor& actor);
    bool CancelAttackIntent(
        CourseEnemyActor& actor,
        float cooldownSeconds);

    const EnemyBehaviorFrame& Frame() const noexcept { return frame_; }
    std::vector<EnemyBehaviorEvent> ConsumeEvents();

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyBehaviorFrameInput& input);
    bool MarkTelegraphPresentedActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t attackIntentSequence);

    void EnterState(
        CourseEnemyActor& actor,
        EnemyBehaviorState state,
        EnemyBehaviorEventKind eventKind = EnemyBehaviorEventKind::StateChanged);
    void BeginAttackIntent(CourseEnemyActor& actor);
    void ApplyMovement(
        CourseEnemyActor& actor,
        float deltaTime,
        float playerDistance,
        float playerForwardSpeed);
    void QueueEvent(CourseEnemyActor& actor, EnemyBehaviorEventKind kind);

    EnemyBehaviorFrame frame_{};
    std::vector<EnemyBehaviorEvent> pendingEvents_;
    uint64_t revision_ = 0;
    float previousPlayerDistance_ = 0.0f;
    bool hasPreviousPlayerDistance_ = false;
};

EnemyBehaviorDefinition ResolveEnemyBehaviorDefinition(
    const CourseEnemyActorDesc& actorDescription);
bool TryParseEnemyBehaviorArchetype(
    const std::string& text,
    EnemyBehaviorArchetype& archetype) noexcept;
const char* ToString(EnemyBehaviorArchetype archetype) noexcept;
const char* ToString(EnemyBehaviorState state) noexcept;
const char* ToString(EnemyBehaviorEventKind kind) noexcept;
