#include "EnemyAttackCoordinator.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
bool OccupiesToken(const EnemyAttackRuntimeState& state) noexcept {
    return state.tokenReserved &&
        (state.phase == EnemyAttackRuntimePhase::Reserved ||
         state.phase == EnemyAttackRuntimePhase::Telegraphing ||
         state.phase == EnemyAttackRuntimePhase::Ready ||
         state.phase == EnemyAttackRuntimePhase::Executing ||
         state.phase == EnemyAttackRuntimePhase::Recovery);
}

int SectorFor(const CourseEnemyActor& actor) noexcept {
    constexpr float kSectorBoundary = 1.25f;
    if (actor.desc.lateralOffset < -kSectorBoundary) return -1;
    if (actor.desc.lateralOffset > kSectorBoundary) return 1;
    return 0;
}

float ArchetypePriority(EnemyBehaviorArchetype archetype) noexcept {
    switch (archetype) {
    case EnemyBehaviorArchetype::Boss: return 0.48f;
    case EnemyBehaviorArchetype::Sniper: return 0.28f;
    case EnemyBehaviorArchetype::Interceptor: return 0.22f;
    case EnemyBehaviorArchetype::Flanker: return 0.18f;
    case EnemyBehaviorArchetype::Turret: return 0.14f;
    case EnemyBehaviorArchetype::Assault: return 0.10f;
    case EnemyBehaviorArchetype::Support: return 0.04f;
    }
    return 0.0f;
}

float ThreatCost(float severity, EnemyBehaviorArchetype archetype) noexcept {
    const float base = 0.55f + (std::clamp)(severity, 0.0f, 1.0f) * 0.65f;
    return archetype == EnemyBehaviorArchetype::Boss
        ? (std::max)(1.45f, base)
        : base;
}

uint64_t MixSeed(uint32_t actorId, uint64_t intentSequence) noexcept {
    uint64_t value = (static_cast<uint64_t>(actorId) << 32u) ^ intentSequence;
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31u);
}

bool ActorAvailable(const CourseEnemyActor& actor) noexcept {
    return actor.fireEnvironmentReady && actor.desc.hitPoints > 0.0f && !actor.desc.suppressFire &&
        (!(actor.behaviorDefinition.choreographedAttackPass ||
           actor.behaviorDefinition.maintainForwardEngagementBand) ||
         !actor.entranceExitState.initialized ||
         !actor.entranceExitState.attackSuppressed) &&
        (!actor.combatState.initialized || actor.combatState.canFire);
}
} // namespace

void EnemyAttackCoordinator::Reset() {
    frame_ = {};
    nextTokenId_ = 1;
    revision_ = 0;
}

bool EnemyAttackCoordinatorSettings::Validate(std::string* errorMessage) const {
    // Zero capacity intentionally holds queued attacks without disabling arbitration.
    const bool valid = maximumConcurrentAttackers <= 4096 &&
        maximumAttackersPerWave <= 4096 && maximumAttackersPerSector <= 4096 &&
        std::isfinite(maximumThreatBudget) && maximumThreatBudget >= 0.0f &&
        std::isfinite(maximumReservationSeconds) && maximumReservationSeconds > 0.0f &&
        std::isfinite(tokenRecoverySeconds) && tokenRecoverySeconds >= 0.0f &&
        std::isfinite(waitingPriorityPerSecond) && waitingPriorityPerSecond >= 0.0f;
    if (errorMessage != nullptr) *errorMessage = valid ? "" :
        "Enemy attack settings require bounded attacker counts and finite nonnegative budgets/durations (positive reservation timeout).";
    return valid;
}

bool EnemyAttackCoordinator::Configure(
    const EnemyAttackCoordinatorSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    // Existing reservations finish normally; the next admission uses the new budget.
    settings_ = settings;
    return true;
}

void EnemyAttackCoordinator::RebuildFromRuntime(CourseSpawnRuntime& runtime) {
    runtime.RebuildEnemyAttacks(*this);
}

void EnemyAttackCoordinator::RebuildFromRuntimeActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime) {
    frame_ = {};
    nextTokenId_ = 1;
    for (CourseEnemyActor& actor : actors) {
        EnemyAttackRuntimeState& state = actor.attackState;
        state.committedThisFrame = false;
        if (!actor.behaviorDefinition.commercialBehavior) {
            state = {};
            continue;
        }
        if (state.tokenReserved && state.tokenId != 0) {
            nextTokenId_ = (std::max)(nextTokenId_, state.tokenId + 1);
        } else if (state.phase == EnemyAttackRuntimePhase::Reserved ||
                   state.phase == EnemyAttackRuntimePhase::Telegraphing ||
                   state.phase == EnemyAttackRuntimePhase::Ready ||
                   state.phase == EnemyAttackRuntimePhase::Executing) {
            state.phase = actor.behaviorState.attackIntentActive
                ? EnemyAttackRuntimePhase::Queued
                : EnemyAttackRuntimePhase::Idle;
            state.tokenId = 0;
            state.tokenReserved = false;
            state.telegraphPresented = false;
        }
        state.revision = ++revision_;
    }
    frame_.revision = revision_;
}

void EnemyAttackCoordinator::InitializeActor(CourseEnemyActor& actor) {
    actor.attackState = {};
    actor.attackState.bulletPatternId = actor.desc.bulletPatternId;
    actor.attackState.deterministicSeed = MixSeed(actor.actorId, 0);
    actor.attackState.revision = ++revision_;
}

void EnemyAttackCoordinator::Update(CourseSpawnRuntime& runtime,
    const EnemyBehaviorFrame& behaviorFrame,
    float deltaTime) {
    runtime.UpdateEnemyAttacks(*this, behaviorFrame, deltaTime);
}

void EnemyAttackCoordinator::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyBehaviorFrame& behaviorFrame,
        float deltaTime) {
    const float dt = (std::max)(0.0f, deltaTime);
    frame_ = {};

    std::unordered_map<uint32_t, const EnemyAttackIntent*> intents;
    intents.reserve(behaviorFrame.attackIntents.size());
    for (const EnemyAttackIntent& intent : behaviorFrame.attackIntents) {
        intents[intent.actorId] = &intent;
    }

    for (CourseEnemyActor& actor : actors) {
        EnemyAttackRuntimeState& state = actor.attackState;
        state.committedThisFrame = false;
        if (!actor.behaviorDefinition.commercialBehavior) continue;

        if (state.phase == EnemyAttackRuntimePhase::Recovery) {
            state.recoveryRemaining = (std::max)(0.0f, state.recoveryRemaining - dt);
            if (state.recoveryRemaining <= 0.0f) ReleaseToken(actor);
        }

        const auto intentIt = intents.find(actor.actorId);
        const EnemyAttackIntent* intent = intentIt != intents.end()
            ? intentIt->second
            : nullptr;
        if (!ActorAvailable(actor)) {
            CancelActor(actor, EnemyAttackCancelReason::ActorUnavailable);
            continue;
        }
        if (intent == nullptr) {
            if (state.phase != EnemyAttackRuntimePhase::Idle &&
                state.phase != EnemyAttackRuntimePhase::Recovery &&
                state.phase != EnemyAttackRuntimePhase::Cancelled) {
                CancelActor(actor, EnemyAttackCancelReason::IntentSuperseded);
            }
            continue;
        }
        if (state.intentSequence != intent->sequence) {
            if (state.tokenReserved) ReleaseToken(actor);
            QueueIntent(actor, *intent);
        } else if (state.phase == EnemyAttackRuntimePhase::Cancelled) {
            QueueIntent(actor, *intent);
        } else if (state.phase == EnemyAttackRuntimePhase::Queued) {
            state.queuedSeconds += dt;
            state.priority = intent->severity +
                ArchetypePriority(intent->archetype) +
                state.queuedSeconds * settings_.waitingPriorityPerSecond;
            state.revision = ++revision_;
        } else if (OccupiesToken(state) &&
                   state.phase != EnemyAttackRuntimePhase::Recovery) {
            state.reservedSeconds += dt;
            if (state.reservedSeconds > settings_.maximumReservationSeconds) {
                CancelActor(actor, EnemyAttackCancelReason::ReservationExpired);
                QueueIntent(actor, *intent);
                continue;
            }
            if (state.telegraphPresented) {
                state.phase = actor.behaviorState.attackTimeRemaining <= 0.0f
                    ? EnemyAttackRuntimePhase::Ready
                    : EnemyAttackRuntimePhase::Telegraphing;
            } else {
                state.phase = EnemyAttackRuntimePhase::Reserved;
            }
            state.revision = ++revision_;
        }
    }

    uint32_t occupiedCount = 0;
    float occupiedThreat = 0.0f;
    std::unordered_map<std::string, uint32_t> waveCounts;
    uint32_t sectorCounts[3]{};
    std::vector<CourseEnemyActor*> candidates;
    for (CourseEnemyActor& actor : actors) {
        EnemyAttackRuntimeState& state = actor.attackState;
        if (OccupiesToken(state)) {
            ++occupiedCount;
            occupiedThreat += state.threatCost;
            ++waveCounts[actor.desc.waveId];
            ++sectorCounts[SectorFor(actor) + 1];
        } else if (state.phase == EnemyAttackRuntimePhase::Queued) {
            candidates.push_back(&actor);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const CourseEnemyActor* left, const CourseEnemyActor* right) {
            if (left->attackState.priority != right->attackState.priority) {
                return left->attackState.priority > right->attackState.priority;
            }
            if (left->attackState.queuedSeconds != right->attackState.queuedSeconds) {
                return left->attackState.queuedSeconds > right->attackState.queuedSeconds;
            }
            return left->actorId < right->actorId;
        });

    if (settings_.enabled) {
        for (CourseEnemyActor* actor : candidates) {
            EnemyAttackRuntimeState& state = actor->attackState;
            const int sectorIndex = SectorFor(*actor) + 1;
            const bool countBlocked =
                occupiedCount >= settings_.maximumConcurrentAttackers;
            const bool waveBlocked = settings_.maximumAttackersPerWave > 0 &&
                waveCounts[actor->desc.waveId] >= settings_.maximumAttackersPerWave;
            const bool sectorBlocked = settings_.maximumAttackersPerSector > 0 &&
                sectorCounts[sectorIndex] >= settings_.maximumAttackersPerSector;
            const bool threatBlocked = occupiedCount > 0 &&
                occupiedThreat + state.threatCost > settings_.maximumThreatBudget;
            if (countBlocked || waveBlocked || sectorBlocked || threatBlocked) {
                ++frame_.budgetBlockedThisFrame;
                continue;
            }
            GrantToken(*actor);
            ++occupiedCount;
            occupiedThreat += state.threatCost;
            ++waveCounts[actor->desc.waveId];
            ++sectorCounts[sectorIndex];
        }
    } else {
        for (CourseEnemyActor* actor : candidates) GrantToken(*actor);
    }

    for (const CourseEnemyActor& actor : runtime.Enemies()) {
        const EnemyAttackRuntimeState& state = actor.attackState;
        if (state.phase == EnemyAttackRuntimePhase::Queued) ++frame_.queuedAttacks;
        if (OccupiesToken(state)) ++frame_.reservedAttacks;
        if (state.phase == EnemyAttackRuntimePhase::Ready) ++frame_.readyAttacks;
        if (OccupiesToken(state)) frame_.occupiedThreatBudget += state.threatCost;
    }
    frame_.revision = revision_;
}

bool EnemyAttackCoordinator::MarkTelegraphPresented(CourseSpawnRuntime& runtime,
    uint32_t actorId,
    uint64_t intentSequence) {
    return runtime.MarkEnemyCoordinatorTelegraph(*this, actorId, intentSequence);
}

bool EnemyAttackCoordinator::MarkTelegraphPresentedActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t intentSequence) {
    for (CourseEnemyActor& actor : actors) {
        EnemyAttackRuntimeState& state = actor.attackState;
        if (actor.actorId != actorId || !state.tokenReserved ||
            state.intentSequence != intentSequence) {
            continue;
        }
        state.telegraphPresented = true;
        state.phase = actor.behaviorState.attackTimeRemaining <= 0.0f
            ? EnemyAttackRuntimePhase::Ready
            : EnemyAttackRuntimePhase::Telegraphing;
        state.revision = ++revision_;
        return true;
    }
    return false;
}

bool EnemyAttackCoordinator::CanExecute(
    const CourseEnemyActor& actor) const noexcept {
    const EnemyAttackRuntimeState& state = actor.attackState;
    return state.phase == EnemyAttackRuntimePhase::Ready &&
        state.tokenReserved && state.tokenId != 0 && state.telegraphPresented &&
        actor.behaviorState.attackIntentActive &&
        actor.behaviorState.attackIntentSequence == state.intentSequence &&
        actor.behaviorState.telegraphPresented &&
        actor.targetingState.solutionLocked &&
        actor.targetingState.attackIntentSequence == state.intentSequence &&
        actor.targetingState.attackTokenId == state.tokenId &&
        actor.behaviorState.attackTimeRemaining <= 0.0f;
}

bool EnemyAttackCoordinator::NotifyExecutionStarted(CourseEnemyActor& actor) {
    if (!CanExecute(actor)) return false;
    actor.attackState.phase = EnemyAttackRuntimePhase::Executing;
    actor.attackState.revision = ++revision_;
    return true;
}

bool EnemyAttackCoordinator::NotifyExecutionCommitted(
    CourseEnemyActor& actor,
    uint32_t emittedProjectiles) {
    EnemyAttackRuntimeState& state = actor.attackState;
    if (state.phase != EnemyAttackRuntimePhase::Executing ||
        emittedProjectiles == 0) {
        return false;
    }
    state.emittedProjectiles += emittedProjectiles;
    ++state.emittedVolleys;
    state.committedThisFrame = true;
    state.phase = EnemyAttackRuntimePhase::Recovery;
    state.recoveryRemaining = (std::max)(
        0.0f, settings_.tokenRecoverySeconds);
    state.revision = ++revision_;
    return true;
}

bool EnemyAttackCoordinator::CancelActor(
    CourseEnemyActor& actor,
    EnemyAttackCancelReason reason) {
    EnemyAttackRuntimeState& state = actor.attackState;
    if (state.phase == EnemyAttackRuntimePhase::Idle && !state.tokenReserved) {
        return false;
    }
    if (state.tokenReserved) ++frame_.releasedThisFrame;
    state.phase = EnemyAttackRuntimePhase::Cancelled;
    state.cancelReason = reason;
    state.tokenId = 0;
    state.tokenReserved = false;
    state.telegraphPresented = false;
    actor.behaviorState.telegraphPresented = false;
    if (reason == EnemyAttackCancelReason::ReservationExpired &&
        actor.behaviorDefinition.choreographedAttackPass &&
        actor.behaviorState.attackIntentActive) {
        // Re-admission starts a new complete tell, never an overdue .1s cue.
        actor.behaviorState.attackTimeRemaining = actor.behaviorDefinition.attackLeadSeconds;
        actor.fireTimer = actor.behaviorState.attackTimeRemaining;
    }
    state.recoveryRemaining = 0.0f;
    state.revision = ++revision_;
    ++frame_.cancelledThisFrame;
    return true;
}

void EnemyAttackCoordinator::QueueIntent(
    CourseEnemyActor& actor,
    const EnemyAttackIntent& intent) {
    EnemyAttackRuntimeState& state = actor.attackState;
    state.phase = EnemyAttackRuntimePhase::Queued;
    state.cancelReason = EnemyAttackCancelReason::None;
    state.bulletPatternId = actor.desc.bulletPatternId;
    state.intentSequence = intent.sequence;
    state.tokenId = 0;
    state.deterministicSeed = MixSeed(actor.actorId, intent.sequence);
    state.queuedSeconds = 0.0f;
    state.reservedSeconds = 0.0f;
    state.recoveryRemaining = 0.0f;
    state.priority = intent.severity + ArchetypePriority(intent.archetype);
    state.threatCost = ThreatCost(intent.severity, intent.archetype);
    state.tokenReserved = false;
    state.telegraphPresented = false;
    state.committedThisFrame = false;
    state.revision = ++revision_;
}

void EnemyAttackCoordinator::GrantToken(CourseEnemyActor& actor) {
    EnemyAttackRuntimeState& state = actor.attackState;
    state.phase = EnemyAttackRuntimePhase::Reserved;
    state.cancelReason = EnemyAttackCancelReason::None;
    state.tokenId = nextTokenId_++;
    if (nextTokenId_ == 0) nextTokenId_ = 1;
    state.tokenReserved = true;
    state.telegraphPresented = false;
    state.reservedSeconds = 0.0f;
    actor.behaviorState.telegraphPresented = false;
    actor.behaviorState.attackTimeRemaining = (std::max)(
        0.05f, actor.behaviorDefinition.attackLeadSeconds);
    actor.fireTimer = actor.behaviorState.attackTimeRemaining;
    state.revision = ++revision_;
    ++frame_.grantedThisFrame;
}

void EnemyAttackCoordinator::ReleaseToken(CourseEnemyActor& actor) {
    EnemyAttackRuntimeState& state = actor.attackState;
    if (state.tokenReserved) ++frame_.releasedThisFrame;
    state.phase = EnemyAttackRuntimePhase::Idle;
    state.cancelReason = EnemyAttackCancelReason::None;
    state.tokenId = 0;
    state.tokenReserved = false;
    state.telegraphPresented = false;
    state.recoveryRemaining = 0.0f;
    state.revision = ++revision_;
}

const char* ToString(EnemyAttackRuntimePhase phase) noexcept {
    switch (phase) {
    case EnemyAttackRuntimePhase::Idle: return "Idle";
    case EnemyAttackRuntimePhase::Queued: return "Queued";
    case EnemyAttackRuntimePhase::Reserved: return "Reserved";
    case EnemyAttackRuntimePhase::Telegraphing: return "Telegraphing";
    case EnemyAttackRuntimePhase::Ready: return "Ready";
    case EnemyAttackRuntimePhase::Executing: return "Executing";
    case EnemyAttackRuntimePhase::Recovery: return "Recovery";
    case EnemyAttackRuntimePhase::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* ToString(EnemyAttackCancelReason reason) noexcept {
    switch (reason) {
    case EnemyAttackCancelReason::None: return "None";
    case EnemyAttackCancelReason::IntentSuperseded: return "IntentSuperseded";
    case EnemyAttackCancelReason::ActorUnavailable: return "ActorUnavailable";
    case EnemyAttackCancelReason::ReservationExpired: return "ReservationExpired";
    case EnemyAttackCancelReason::RuntimeReset: return "RuntimeReset";
    case EnemyAttackCancelReason::PlayerInterrupted: return "PlayerInterrupted";
    }
    return "Unknown";
}
