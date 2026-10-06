#include "EnemyCombatSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool FiniteNonNegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

float Saturate(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
}

CourseEnemyActor* FindEnemy(std::span<CourseEnemyActor> actors, uint32_t actorId) {
    for (CourseEnemyActor& actor : actors) {
        if (actor.actorId == actorId) {
            return &actor;
        }
    }
    return nullptr;
}
} // namespace

EnemyCombatDefinition EnemyCombatDefinition::LegacyCompatible() {
    EnemyCombatDefinition result{};
    result.definitionId = "legacy_compatible";
    result.spawnScale = 1.0f;
    result.minimumDeathScale = 1.0f;
    result.targetableWhileSpawning = true;
    result.damageableWhileSpawning = true;
    return result;
}

EnemyCombatDefinition EnemyCombatDefinition::CommercialStandard() {
    EnemyCombatDefinition result{};
    result.definitionId = "commercial_standard";
    result.spawnDurationSeconds = 0.34f;
    result.engageDurationSeconds = 0.18f;
    result.telegraphLeadSeconds = 0.72f;
    result.attackCommitSeconds = 0.12f;
    result.recoverySeconds = 0.42f;
    result.hitReactSeconds = 0.11f;
    result.deathDurationSeconds = 0.46f;
    result.spawnScale = 0.72f;
    result.minimumDeathScale = 0.08f;
    result.commercialStateMachine = true;
    return result;
}

bool EnemyCombatDefinition::Validate(std::string* errorMessage) const {
    if (definitionId.empty()) {
        SetError(errorMessage, "EnemyCombatDefinition.definitionId must not be empty.");
        return false;
    }
    if (!std::isfinite(maximumHitPoints) || maximumHitPoints <= 0.0f) {
        SetError(errorMessage, "EnemyCombatDefinition.maximumHitPoints must be positive.");
        return false;
    }
    if (!FiniteNonNegative(spawnDurationSeconds) ||
        !FiniteNonNegative(engageDurationSeconds) ||
        !FiniteNonNegative(telegraphLeadSeconds) ||
        !FiniteNonNegative(attackCommitSeconds) ||
        !FiniteNonNegative(recoverySeconds) ||
        !FiniteNonNegative(hitReactSeconds) ||
        !FiniteNonNegative(deathDurationSeconds)) {
        SetError(errorMessage, "EnemyCombatDefinition contains an invalid duration.");
        return false;
    }
    if (!std::isfinite(spawnScale) || spawnScale <= 0.0f ||
        !std::isfinite(minimumDeathScale) || minimumDeathScale < 0.0f ||
        minimumDeathScale > 1.0f) {
        SetError(errorMessage, "EnemyCombatDefinition contains an invalid presentation scale.");
        return false;
    }
    return true;
}

EnemyCombatDefinition ResolveEnemyCombatDefinition(
    const CourseEnemyActorDesc& actorDescription) {
    const bool explicitDefinition =
        !actorDescription.combatDefinition.definitionId.empty();
    EnemyCombatDefinition definition = explicitDefinition
        ? actorDescription.combatDefinition
        : !actorDescription.actorAssetId.empty()
            ? EnemyCombatDefinition::CommercialStandard()
            : EnemyCombatDefinition::LegacyCompatible();

    definition.maximumHitPoints = definition.maximumHitPoints > 0.0f
        ? definition.maximumHitPoints
        : (std::max)(1.0f, actorDescription.hitPoints);
    if (!explicitDefinition) {
        definition.telegraphLeadSeconds = (std::max)(
            definition.commercialStateMachine ? 0.45f : 0.02f,
            actorDescription.firstShotDelay);
        if (definition.commercialStateMachine) {
            definition.recoverySeconds = (std::max)(
                0.18f,
                actorDescription.fireInterval -
                    definition.telegraphLeadSeconds -
                    definition.attackCommitSeconds);
        }
    }
    std::string validationError;
    if (!definition.Validate(&validationError)) {
        definition = EnemyCombatDefinition::LegacyCompatible();
        definition.maximumHitPoints = (std::max)(1.0f, actorDescription.hitPoints);
        definition.telegraphLeadSeconds = (std::max)(0.02f, actorDescription.firstShotDelay);
    }
    return definition;
}

void EnemyCombatSystem::Reset() {
    pendingEvents_.clear();
    frameStats_ = {};
    revision_ = 0;
}

void EnemyCombatSystem::InitializeActor(CourseEnemyActor& actor) {
    actor.combatDefinition = ResolveEnemyCombatDefinition(actor.desc);
    EnemyCombatRuntimeState& state = actor.combatState;
    state = {};
    state.initialized = true;
    state.currentHitPoints = actor.combatDefinition.maximumHitPoints;
    state.observedFireSequence = actor.fireSequence;
    state.phase = actor.combatDefinition.commercialStateMachine
        ? EnemyCombatPhase::Spawning
        : EnemyCombatPhase::Telegraphing;
    state.revision = ++revision_;
    actor.desc.hitPoints = state.currentHitPoints;
    if (actor.combatDefinition.commercialStateMachine) {
        actor.fireTimer = (std::max)(actor.fireTimer,
            actor.combatDefinition.telegraphLeadSeconds);
    }
    ApplyPhaseGates(actor);
    QueueEvent(actor, EnemyCombatEventKind::Spawned);
}

void EnemyCombatSystem::Update(CourseSpawnRuntime& runtime,
    const EnemyCombatFrameInput& input) {
    runtime.UpdateEnemyCombat(*this, input);
}

void EnemyCombatSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyCombatFrameInput& input) {
    const float dt = (std::max)(0.0f, input.deltaTime);
    frameStats_ = {};

    for (CourseEnemyActor& actor : actors) {
        if (!actor.combatState.initialized) {
            InitializeActor(actor);
        }
        EnemyCombatRuntimeState& state = actor.combatState;
        const EnemyCombatDefinition& definition = actor.combatDefinition;
        ++frameStats_.activeActors;

        state.phaseElapsedSeconds += dt;
        state.hitFlash = (std::max)(0.0f, state.hitFlash - dt * 8.0f);
        if (actor.desc.hitPoints < state.currentHitPoints) {
            state.currentHitPoints = (std::max)(0.0f, actor.desc.hitPoints);
        } else {
            actor.desc.hitPoints = state.currentHitPoints;
        }

        if (state.currentHitPoints <= 0.0f &&
            state.phase != EnemyCombatPhase::Dying &&
            state.phase != EnemyCombatPhase::Retired) {
            EnterPhase(actor, EnemyCombatPhase::Dying, EnemyCombatEventKind::Defeated);
            state.defeatEventPublished = true;
        }

        const bool fired = actor.fireSequence > state.observedFireSequence;
        state.observedFireSequence = actor.fireSequence;
        if (fired && state.phase != EnemyCombatPhase::Dying &&
            state.phase != EnemyCombatPhase::Retired) {
            EnterPhase(actor, EnemyCombatPhase::Attacking,
                EnemyCombatEventKind::AttackCommitted);
        }

        switch (state.phase) {
        case EnemyCombatPhase::Spawning:
            ++frameStats_.spawningActors;
            if (definition.spawnDurationSeconds <= 0.0f ||
                state.phaseElapsedSeconds >= definition.spawnDurationSeconds) {
                EnterPhase(actor, EnemyCombatPhase::Engaging,
                    EnemyCombatEventKind::Engaged);
            }
            break;
        case EnemyCombatPhase::Engaging:
            if (definition.engageDurationSeconds <= 0.0f ||
                state.phaseElapsedSeconds >= definition.engageDurationSeconds) {
                EnterPhase(actor, EnemyCombatPhase::Telegraphing,
                    EnemyCombatEventKind::TelegraphStarted);
            }
            break;
        case EnemyCombatPhase::Telegraphing:
            ++frameStats_.telegraphingActors;
            break;
        case EnemyCombatPhase::Attacking:
            ++frameStats_.attackingActors;
            if (state.phaseElapsedSeconds >= definition.attackCommitSeconds) {
                EnterPhase(actor, EnemyCombatPhase::Recovering,
                    EnemyCombatEventKind::TelegraphStarted, false);
            }
            break;
        case EnemyCombatPhase::Recovering:
            if (state.phaseElapsedSeconds >= definition.recoverySeconds) {
                EnterPhase(actor, EnemyCombatPhase::Telegraphing,
                    EnemyCombatEventKind::TelegraphStarted);
            }
            break;
        case EnemyCombatPhase::HitReact:
            ++frameStats_.reactingActors;
            if (state.phaseElapsedSeconds >= definition.hitReactSeconds) {
                EnterPhase(actor, state.resumePhase,
                    state.resumePhase == EnemyCombatPhase::Telegraphing
                        ? EnemyCombatEventKind::TelegraphStarted
                        : EnemyCombatEventKind::Engaged,
                    state.resumePhase == EnemyCombatPhase::Telegraphing);
            }
            break;
        case EnemyCombatPhase::Dying:
            ++frameStats_.dyingActors;
            state.deathProgress = definition.deathDurationSeconds > 0.0f
                ? Saturate(state.phaseElapsedSeconds / definition.deathDurationSeconds)
                : 1.0f;
            if (state.deathProgress >= 1.0f) {
                EnterPhase(actor, EnemyCombatPhase::Retired,
                    EnemyCombatEventKind::Retired);
            }
            break;
        case EnemyCombatPhase::Retired:
            break;
        }

        ApplyPhaseGates(actor);
        state.revision = ++revision_;
    }

    frameStats_.eventsPublished = static_cast<uint32_t>(pendingEvents_.size());
    frameStats_.revision = revision_;
}

bool EnemyCombatSystem::SubmitDamageResult(CourseSpawnRuntime& runtime,
    const DamageResult& damageResult,
    const WeaponFeedbackEvent* feedbackEvent) {
    return runtime.NotifyEnemyDamage(*this, damageResult, feedbackEvent);
}

bool EnemyCombatSystem::SubmitDamageResultActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult,
        const WeaponFeedbackEvent* feedbackEvent) {
    if (!damageResult.requestAccepted || !damageResult.targetResolved ||
        damageResult.hitKind != RailAimHitKind::Enemy ||
        !damageResult.damageApplied || damageResult.targetActorId == 0) {
        return false;
    }
    CourseEnemyActor* actor = FindEnemy(actors, damageResult.targetActorId);
    if (actor == nullptr) {
        return false;
    }
    const float currentHealth = actor->combatState.initialized
        ? actor->combatState.currentHitPoints : actor->desc.hitPoints;
    if (!std::isfinite(damageResult.remainingHitPoints) || damageResult.remainingHitPoints < 0.0f ||
        !std::isfinite(damageResult.appliedDamage) || damageResult.appliedDamage <= 0.0f ||
        damageResult.remainingHitPoints > currentHealth ||
        (damageResult.shotId != 0 && actor->combatState.lastDamageShotId == damageResult.shotId)) {
        return false;
    }
    if (!actor->combatState.initialized) {
        InitializeActor(*actor);
    }

    EnemyCombatRuntimeState& state = actor->combatState;
    state.currentHitPoints = (std::max)(0.0f, damageResult.remainingHitPoints);
    state.lastDamageShotId = damageResult.shotId;
    state.hitFlash = 1.0f;
    actor->desc.hitPoints = state.currentHitPoints;
    const HitFeedbackKind feedbackKind = feedbackEvent != nullptr
        ? feedbackEvent->feedbackKind
        : damageResult.destroyed
            ? HitFeedbackKind::Destroyed
            : damageResult.weakPointHit
                ? HitFeedbackKind::WeakPointHit
                : HitFeedbackKind::NormalHit;

    if (damageResult.destroyed || state.currentHitPoints <= 0.0f) {
        if (state.phase != EnemyCombatPhase::Dying &&
            state.phase != EnemyCombatPhase::Retired) {
            EnterPhase(*actor, EnemyCombatPhase::Dying,
                EnemyCombatEventKind::Defeated, false);
        }
        if (!state.defeatEventPublished) {
            QueueEvent(*actor, EnemyCombatEventKind::Defeated,
                damageResult.shotId, damageResult.appliedDamage, feedbackKind);
            state.defeatEventPublished = true;
        }
    } else {
        state.resumePhase = EnemyCombatPhase::Recovering;
        EnterPhase(*actor, EnemyCombatPhase::HitReact,
            EnemyCombatEventKind::HitReacted, false);
        QueueEvent(*actor, EnemyCombatEventKind::HitReacted,
            damageResult.shotId, damageResult.appliedDamage, feedbackKind);
    }
    ApplyPhaseGates(*actor);
    state.revision = ++revision_;
    return true;
}

bool EnemyCombatSystem::ForceDefeat(CourseSpawnRuntime& runtime,
    uint32_t actorId) {
    return runtime.DefeatEnemy(*this, actorId);
}

bool EnemyCombatSystem::ForceDefeatActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId) {
    CourseEnemyActor* actor = FindEnemy(actors, actorId);
    if (actor == nullptr) {
        return false;
    }
    if (!actor->combatState.initialized) {
        InitializeActor(*actor);
    }
    EnemyCombatRuntimeState& state = actor->combatState;
    if (state.phase == EnemyCombatPhase::Dying ||
        state.phase == EnemyCombatPhase::Retired) {
        return false;
    }
    state.currentHitPoints = 0.0f;
    actor->desc.hitPoints = 0.0f;
    EnterPhase(*actor, EnemyCombatPhase::Dying,
        EnemyCombatEventKind::Defeated, false);
    if (!state.defeatEventPublished) {
        QueueEvent(*actor, EnemyCombatEventKind::Defeated);
        state.defeatEventPublished = true;
    }
    ApplyPhaseGates(*actor);
    return true;
}

std::vector<EnemyCombatEvent> EnemyCombatSystem::ConsumeEvents() {
    std::vector<EnemyCombatEvent> result = std::move(pendingEvents_);
    pendingEvents_.clear();
    return result;
}

void EnemyCombatSystem::EnterPhase(
    CourseEnemyActor& actor,
    EnemyCombatPhase phase,
    EnemyCombatEventKind eventKind,
    bool publishEvent) {
    EnemyCombatRuntimeState& state = actor.combatState;
    if (state.phase == phase) {
        return;
    }
    state.phase = phase;
    state.phaseElapsedSeconds = 0.0f;
    if (phase == EnemyCombatPhase::Telegraphing &&
        actor.combatDefinition.commercialStateMachine) {
        actor.fireTimer = (std::max)(
            actor.fireTimer,
            actor.combatDefinition.telegraphLeadSeconds);
    }
    ApplyPhaseGates(actor);
    if (publishEvent) {
        QueueEvent(actor, eventKind);
    }
}

void EnemyCombatSystem::ApplyPhaseGates(CourseEnemyActor& actor) {
    EnemyCombatRuntimeState& state = actor.combatState;
    const EnemyCombatDefinition& definition = actor.combatDefinition;
    state.canBeTargeted = true;
    state.canReceiveDamage = true;
    state.canTelegraph = true;
    state.canFire = true;
    state.presentationAlpha = 1.0f;
    state.presentationScale = 1.0f;

    if (!definition.commercialStateMachine) {
        if (state.phase == EnemyCombatPhase::Dying ||
            state.phase == EnemyCombatPhase::Retired) {
            state.canBeTargeted = false;
            state.canReceiveDamage = false;
            state.canTelegraph = false;
            state.canFire = false;
        }
        return;
    }

    switch (state.phase) {
    case EnemyCombatPhase::Spawning: {
        const float progress = definition.spawnDurationSeconds > 0.0f
            ? Saturate(state.phaseElapsedSeconds / definition.spawnDurationSeconds)
            : 1.0f;
        state.canBeTargeted = definition.targetableWhileSpawning;
        state.canReceiveDamage = definition.damageableWhileSpawning;
        state.canTelegraph = false;
        state.canFire = false;
        state.presentationAlpha = progress;
        state.presentationScale = definition.spawnScale +
            (1.0f - definition.spawnScale) * progress;
        break;
    }
    case EnemyCombatPhase::Engaging:
        state.canTelegraph = false;
        state.canFire = false;
        break;
    case EnemyCombatPhase::Telegraphing:
        break;
    case EnemyCombatPhase::Attacking:
        state.canFire = false;
        break;
    case EnemyCombatPhase::Recovering:
        state.canTelegraph = false;
        state.canFire = false;
        break;
    case EnemyCombatPhase::HitReact:
        state.canTelegraph = false;
        state.canFire = !definition.pauseAttackDuringHitReact;
        break;
    case EnemyCombatPhase::Dying:
        state.canBeTargeted = false;
        state.canReceiveDamage = false;
        state.canTelegraph = false;
        state.canFire = false;
        state.presentationAlpha = 1.0f - state.deathProgress;
        state.presentationScale = 1.0f -
            (1.0f - definition.minimumDeathScale) * state.deathProgress;
        break;
    case EnemyCombatPhase::Retired:
        state.canBeTargeted = false;
        state.canReceiveDamage = false;
        state.canTelegraph = false;
        state.canFire = false;
        state.presentationAlpha = 0.0f;
        state.presentationScale = 0.0f;
        break;
    }
    if (state.phase == EnemyCombatPhase::Dying || state.phase == EnemyCombatPhase::Retired) {
        // Damage can arrive after the attack update. Cancel immediately in the
        // same frame, so no retained warning or muzzle light survives defeat.
        actor.behaviorState.attackIntentActive = false;
        actor.behaviorState.telegraphPresented = false;
        actor.behaviorState.attackTimeRemaining = 0;
        actor.attackState.tokenReserved = false;
        actor.attackState.tokenId = 0;
        actor.attackState.telegraphPresented = false;
        actor.attackState.committedThisFrame = false;
        actor.attackState.phase = EnemyAttackRuntimePhase::Cancelled;
        actor.targetingState.solutionLocked = false;
        actor.fireEnvironmentReady = actor.fireSafetyAllowed = false;
    }
}

void EnemyCombatSystem::QueueEvent(
    const CourseEnemyActor& actor,
    EnemyCombatEventKind kind,
    uint64_t shotId,
    float appliedDamage,
    HitFeedbackKind feedbackKind) {
    constexpr size_t kMaximumPendingEvents = 512;
    if (pendingEvents_.size() >= kMaximumPendingEvents) {
        pendingEvents_.erase(pendingEvents_.begin());
    }
    EnemyCombatEvent event{};
    event.kind = kind;
    event.actorId = actor.actorId;
    event.shotId = shotId;
    event.definitionId = actor.combatDefinition.definitionId;
    event.actorAssetId = actor.desc.actorAssetId;
    event.placementGuid = actor.desc.sourcePlacementGuid;
    event.waveId = actor.desc.waveId;
    event.appliedDamage = appliedDamage;
    event.remainingHitPoints = actor.combatState.currentHitPoints;
    event.feedbackKind = feedbackKind;
    pendingEvents_.push_back(std::move(event));
}

const char* ToString(EnemyCombatPhase phase) noexcept {
    switch (phase) {
    case EnemyCombatPhase::Spawning: return "Spawning";
    case EnemyCombatPhase::Engaging: return "Engaging";
    case EnemyCombatPhase::Telegraphing: return "Telegraphing";
    case EnemyCombatPhase::Attacking: return "Attacking";
    case EnemyCombatPhase::Recovering: return "Recovering";
    case EnemyCombatPhase::HitReact: return "HitReact";
    case EnemyCombatPhase::Dying: return "Dying";
    case EnemyCombatPhase::Retired: return "Retired";
    }
    return "Unknown";
}

const char* ToString(EnemyCombatEventKind kind) noexcept {
    switch (kind) {
    case EnemyCombatEventKind::Spawned: return "Spawned";
    case EnemyCombatEventKind::Engaged: return "Engaged";
    case EnemyCombatEventKind::TelegraphStarted: return "TelegraphStarted";
    case EnemyCombatEventKind::AttackCommitted: return "AttackCommitted";
    case EnemyCombatEventKind::HitReacted: return "HitReacted";
    case EnemyCombatEventKind::Defeated: return "Defeated";
    case EnemyCombatEventKind::Retired: return "Retired";
    }
    return "Unknown";
}
