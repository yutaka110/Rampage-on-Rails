#include "EnemyBehaviorSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace {
constexpr float kTau = 6.28318530717958647692f;

bool FiniteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = message;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool Contains(const std::string& text, const char* token) {
    return Lower(text).find(token) != std::string::npos;
}

EnemyBehaviorArchetype ArchetypeForDescription(
    const CourseEnemyActorDesc& description) noexcept {
    const std::string identity = description.actorAssetId + " " + description.role;
    if (Contains(identity, "boss") || Contains(identity, "gatekeeper")) {
        return EnemyBehaviorArchetype::Boss;
    }
    if (Contains(identity, "turret") || Contains(identity, "anchor")) {
        return EnemyBehaviorArchetype::Turret;
    }
    if (Contains(identity, "interceptor") || Contains(identity, "cross")) {
        return EnemyBehaviorArchetype::Interceptor;
    }
    if (Contains(identity, "sniper") || Contains(identity, "probe")) {
        return EnemyBehaviorArchetype::Sniper;
    }
    if (Contains(identity, "chaser") || Contains(identity, "flank") ||
        Contains(identity, "pursuit")) {
        return EnemyBehaviorArchetype::Flanker;
    }
    if (Contains(identity, "leader") || Contains(identity, "support")) {
        return EnemyBehaviorArchetype::Support;
    }
    return EnemyBehaviorArchetype::Assault;
}

float Saturate(float value) noexcept {
    return (std::clamp)(value, 0.0f, 1.0f);
}

float SmoothStep(float value) noexcept {
    const float t = Saturate(value);
    return t * t * (3.0f - 2.0f * t);
}

float SeverityForArchetype(EnemyBehaviorArchetype archetype) noexcept {
    switch (archetype) {
    case EnemyBehaviorArchetype::Assault: return 0.45f;
    case EnemyBehaviorArchetype::Flanker: return 0.56f;
    case EnemyBehaviorArchetype::Sniper: return 0.74f;
    case EnemyBehaviorArchetype::Interceptor: return 0.64f;
    case EnemyBehaviorArchetype::Turret: return 0.60f;
    case EnemyBehaviorArchetype::Support: return 0.52f;
    case EnemyBehaviorArchetype::Boss: return 1.0f;
    }
    return 0.5f;
}

bool HasAttackPassTurn(
    const CourseEnemyActor& actor, const CourseSpawnRuntime& runtime) {
    // Entry must finish before a readable stationary pose can begin.
    if (actor.entranceExitState.initialized &&
        actor.entranceExitState.phase != EnemyEntranceExitPhase::Active) {
        return false;
    }
    for (const CourseEnemyActor& peer : runtime.Enemies()) {
        if (peer.actorId == actor.actorId || peer.desc.waveId != actor.desc.waveId ||
            !peer.behaviorDefinition.choreographedAttackPass ||
            peer.desc.hitPoints <= 0.0f ||
            peer.combatState.phase == EnemyCombatPhase::Dying ||
            peer.combatState.phase == EnemyCombatPhase::Retired) continue;
        const EnemyBehaviorState peerState = peer.behaviorState.state;
        if (peerState == EnemyBehaviorState::Aiming) return false;
        // Prepare the next shooter during the tail of a visible warning.
        // Its own full warning still runs, under the shared attack budget.
        if (peerState == EnemyBehaviorState::RequestingAttack &&
            (!peer.attackState.tokenReserved || !peer.behaviorState.telegraphPresented ||
             peer.behaviorState.attackTimeRemaining > peer.behaviorDefinition.attackPassHandoffLeadSeconds)) return false;
        if (peerState == EnemyBehaviorState::Evading &&
            peer.behaviorDefinition.attackPassHandoffLeadSeconds <= 0.0f) return false;
        // Preserve leader/left/right order even if a preceding actor is still
        // establishing its band. A defeated or departing actor yields its turn.
        if (peerState != EnemyBehaviorState::Retreating &&
            peerState != EnemyBehaviorState::Disabled &&
            peerState != EnemyBehaviorState::RequestingAttack &&
            peerState != EnemyBehaviorState::Evading &&
            (peer.behaviorDefinition.attackPassStartDelaySeconds <
                actor.behaviorDefinition.attackPassStartDelaySeconds ||
             (peer.behaviorDefinition.attackPassStartDelaySeconds ==
                actor.behaviorDefinition.attackPassStartDelaySeconds &&
              peer.actorId < actor.actorId))) return false;
    }
    return true;
}
} // namespace

EnemyBehaviorDefinition EnemyBehaviorDefinition::LegacyDirect() {
    EnemyBehaviorDefinition result{};
    result.definitionId = "legacy_direct";
    result.entryDurationSeconds = 0.0f;
    result.positioningDurationSeconds = 0.0f;
    result.aimingDurationSeconds = 0.0f;
    result.attackLeadSeconds = 0.0f;
    result.evadeDurationSeconds = 0.0f;
    result.repositionDurationSeconds = 0.0f;
    result.lateralAmplitude = 0.0f;
    result.verticalAmplitude = 0.0f;
    result.movementFrequency = 0.0f;
    result.commercialBehavior = false;
    result.movementEnabled = false;
    result.requireTelegraphPresentation = false;
    return result;
}

EnemyBehaviorDefinition EnemyBehaviorDefinition::Commercial(
    EnemyBehaviorArchetype archetype) {
    EnemyBehaviorDefinition result{};
    result.archetype = archetype;
    result.commercialBehavior = true;
    result.definitionId = std::string("commercial_") + ToString(archetype);
    switch (archetype) {
    case EnemyBehaviorArchetype::Assault:
        break;
    case EnemyBehaviorArchetype::Flanker:
        result.entryDurationSeconds = 0.28f;
        result.positioningDurationSeconds = 0.24f;
        result.lateralAmplitude = 4.2f;
        result.verticalAmplitude = 0.85f;
        result.movementFrequency = 0.58f;
        result.maximumBankRadians = 0.42f;
        break;
    case EnemyBehaviorArchetype::Sniper:
        result.positioningDurationSeconds = 0.46f;
        result.aimingDurationSeconds = 0.52f;
        result.attackLeadSeconds = 0.82f;
        result.attackCooldownSeconds = 1.35f;
        result.lateralAmplitude = 0.65f;
        result.verticalAmplitude = 0.34f;
        result.movementFrequency = 0.32f;
        break;
    case EnemyBehaviorArchetype::Interceptor:
        result.entryDurationSeconds = 0.22f;
        result.positioningDurationSeconds = 0.18f;
        result.aimingDurationSeconds = 0.18f;
        result.attackLeadSeconds = 0.46f;
        result.lateralAmplitude = 5.5f;
        result.verticalAmplitude = 1.25f;
        result.movementFrequency = 0.92f;
        result.maximumBankRadians = 0.50f;
        break;
    case EnemyBehaviorArchetype::Turret:
        result.entryDurationSeconds = 0.18f;
        result.positioningDurationSeconds = 0.18f;
        result.aimingDurationSeconds = 0.34f;
        result.attackLeadSeconds = 0.68f;
        result.attackCooldownSeconds = 1.05f;
        result.lateralAmplitude = 0.0f;
        result.verticalAmplitude = 0.0f;
        result.movementFrequency = 0.0f;
        result.movementEnabled = false;
        result.maximumBankRadians = 0.0f;
        break;
    case EnemyBehaviorArchetype::Support:
        result.positioningDurationSeconds = 0.38f;
        result.attackCooldownSeconds = 1.10f;
        result.lateralAmplitude = 2.6f;
        result.verticalAmplitude = 1.4f;
        result.movementFrequency = 0.44f;
        break;
    case EnemyBehaviorArchetype::Boss:
        result.entryDurationSeconds = 0.52f;
        result.positioningDurationSeconds = 0.42f;
        result.aimingDurationSeconds = 0.38f;
        result.attackLeadSeconds = 0.76f;
        result.attackCooldownSeconds = 0.92f;
        result.evadeDurationSeconds = 0.34f;
        result.repositionDurationSeconds = 0.48f;
        result.lateralAmplitude = 3.4f;
        result.verticalAmplitude = 1.8f;
        result.movementFrequency = 0.28f;
        result.maximumBankRadians = 0.18f;
        break;
    }
    return result;
}

bool EnemyBehaviorDefinition::Validate(std::string* errorMessage) const {
    if (definitionId.empty()) {
        SetError(errorMessage, "EnemyBehaviorDefinition.definitionId must not be empty.");
        return false;
    }
    if (!FiniteNonNegative(entryDurationSeconds) ||
        !FiniteNonNegative(positioningDurationSeconds) ||
        !FiniteNonNegative(aimingDurationSeconds) ||
        !FiniteNonNegative(attackLeadSeconds) ||
        !FiniteNonNegative(attackCooldownSeconds) ||
        !FiniteNonNegative(evadeDurationSeconds) ||
        !FiniteNonNegative(repositionDurationSeconds) ||
        !FiniteNonNegative(lateralAmplitude) ||
        !FiniteNonNegative(verticalAmplitude) ||
        !FiniteNonNegative(movementFrequency) ||
        !FiniteNonNegative(forwardMotionScale) ||
        !FiniteNonNegative(maximumBankRadians) ||
        !FiniteNonNegative(engagementBandMinimumForwardDistance) ||
        !FiniteNonNegative(engagementBandPreferredForwardDistance) ||
        !FiniteNonNegative(engagementBandMaximumForwardDistance) ||
        !FiniteNonNegative(engagementBandDisengageForwardDistance) ||
        !FiniteNonNegative(engagementBandPositionGain) ||
        !FiniteNonNegative(engagementBandMaximumCorrectionSpeed) ||
        !FiniteNonNegative(engagementBandVelocityResponse) ||
        !FiniteNonNegative(attackPassStartDelaySeconds) ||
        !FiniteNonNegative(attackPassRecoilSeconds) ||
        !FiniteNonNegative(attackPassHandoffLeadSeconds) ||
        attackPassHandoffLeadSeconds > attackLeadSeconds) {
        SetError(errorMessage, "EnemyBehaviorDefinition contains an invalid value.");
        return false;
    }
    if (maintainForwardEngagementBand &&
        (!(engagementBandDisengageForwardDistance <
                engagementBandMinimumForwardDistance) ||
         !(engagementBandMinimumForwardDistance <
                engagementBandPreferredForwardDistance) ||
         !(engagementBandPreferredForwardDistance <
                engagementBandMaximumForwardDistance) ||
         engagementBandPositionGain <= 0.0f ||
         engagementBandMaximumCorrectionSpeed <= 0.0f ||
         engagementBandVelocityResponse <= 0.0f)) {
        SetError(errorMessage, "EnemyBehaviorDefinition engagement band is invalid.");
        return false;
    }
    if (choreographedAttackPass &&
        (!commercialBehavior || !maintainForwardEngagementBand ||
         aimingDurationSeconds <= 0.0f || attackPassRecoilSeconds <= 0.0f)) {
        SetError(errorMessage, "Attack pass requires commercial forward-band behavior and positive pose/recoil durations.");
        return false;
    }
    return true;
}

EnemyBehaviorDefinition ResolveEnemyBehaviorDefinition(
    const CourseEnemyActorDesc& actorDescription) {
    const bool explicitDefinition =
        !actorDescription.behaviorDefinition.definitionId.empty();
    EnemyBehaviorDefinition result = explicitDefinition
        ? actorDescription.behaviorDefinition
        : actorDescription.actorAssetId.empty()
            ? EnemyBehaviorDefinition::LegacyDirect()
            : EnemyBehaviorDefinition::Commercial(
                ArchetypeForDescription(actorDescription));
    if (result.commercialBehavior) {
        result.attackLeadSeconds = (std::max)(
            result.attackLeadSeconds,
            actorDescription.combatDefinition.telegraphLeadSeconds > 0.0f
                ? actorDescription.combatDefinition.telegraphLeadSeconds
                : 0.45f);
        result.attackCooldownSeconds = (std::max)(
            result.attackCooldownSeconds,
            actorDescription.fireInterval);
    }
    std::string error;
    if (!result.Validate(&error)) {
        result = actorDescription.actorAssetId.empty()
            ? EnemyBehaviorDefinition::LegacyDirect()
            : EnemyBehaviorDefinition::Commercial(
                ArchetypeForDescription(actorDescription));
    }
    return result;
}

void EnemyBehaviorSystem::Reset() {
    frame_ = {};
    pendingEvents_.clear();
    revision_ = 0;
    previousPlayerDistance_ = 0.0f;
    hasPreviousPlayerDistance_ = false;
}

void EnemyBehaviorSystem::InitializeActor(CourseEnemyActor& actor) {
    actor.behaviorDefinition = ResolveEnemyBehaviorDefinition(actor.desc);
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    state = {};
    state.initialized = true;
    state.authoredForwardOffset = actor.desc.distanceOffset;
    state.authoredLateralOffset = actor.desc.lateralOffset;
    state.authoredVerticalOffset = actor.desc.verticalOffset;
    state.deterministicPhase =
        static_cast<float>((actor.actorId * 2654435761u) % 1024u) / 1024.0f * kTau;
    if (actor.desc.meshId == "twin_shield_hull" &&
        actor.behaviorDefinition.maintainForwardEngagementBand &&
        !actor.behaviorDefinition.choreographedAttackPass) {
        uint32_t bits=actor.actorId^0x74e132abu;
        bits^=bits>>16;bits*=0x7feb352du;bits^=bits>>15;
        bits*=0x846ca68bu;bits^=bits>>16;
        const float span=actor.behaviorDefinition.engagementBandMaximumForwardDistance-
            actor.behaviorDefinition.engagementBandMinimumForwardDistance;
        const float nearHome=actor.behaviorDefinition.engagementBandMinimumForwardDistance+
            (std::min)(6.0f,span*0.2f);
        const float farHome=(std::max)(nearHome,
            actor.behaviorDefinition.engagementBandMaximumForwardDistance-(std::min)(7.0f,span*0.2f));
        state.hoverPreferredForwardDistance=nearHome+(farHome-nearHome)*
            static_cast<float>(bits&0xffffu)/65535.0f;
    }
    state.attackCooldownRemaining = (std::max)(
        actor.desc.firstShotDelay,
        actor.behaviorDefinition.positioningDurationSeconds);
    if (actor.behaviorDefinition.choreographedAttackPass) {
        state.attackCooldownRemaining = (std::max)(
            state.attackCooldownRemaining,
            actor.behaviorDefinition.attackPassStartDelaySeconds);
    }
    state.engagementBandVelocity =
        actor.desc.forwardSpeed * actor.behaviorDefinition.forwardMotionScale;
    state.state = actor.behaviorDefinition.commercialBehavior
        ? EnemyBehaviorState::Dormant
        : EnemyBehaviorState::Aiming;
    state.revision = ++revision_;
}

void EnemyBehaviorSystem::Update(CourseSpawnRuntime& runtime,
    const EnemyBehaviorFrameInput& input) {
    runtime.UpdateEnemyBehavior(*this, input);
}

void EnemyBehaviorSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyBehaviorFrameInput& input) {
    frame_ = {};
    const float dt = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    const float playerDistance = std::isfinite(input.playerDistance)
        ? input.playerDistance : 0.0f;
    float playerForwardSpeed = 0.0f;
    if (hasPreviousPlayerDistance_ && dt > 0.0001f) {
        const float distanceDelta = playerDistance - previousPlayerDistance_;
        const float discontinuityThreshold = (std::max)(24.0f, dt * 96.0f);
        if (distanceDelta >= 0.0f && distanceDelta <= discontinuityThreshold) {
            playerForwardSpeed = (std::clamp)(
                distanceDelta / dt, 0.0f, 72.0f);
        }
    }
    previousPlayerDistance_ = playerDistance;
    hasPreviousPlayerDistance_ = true;
    for (CourseEnemyActor& actor : actors) {
        if (!actor.behaviorState.initialized) InitializeActor(actor);
        EnemyBehaviorRuntimeState& state = actor.behaviorState;
        const EnemyBehaviorDefinition& definition = actor.behaviorDefinition;
        ++frame_.activeActors;
        if (!definition.commercialBehavior) continue;

        const bool combatDisabled = actor.combatState.initialized &&
            (actor.combatState.phase == EnemyCombatPhase::Dying ||
             actor.combatState.phase == EnemyCombatPhase::Retired);
        if (combatDisabled) {
            if (state.state != EnemyBehaviorState::Disabled) {
                EnterState(actor, EnemyBehaviorState::Disabled,
                    EnemyBehaviorEventKind::Disabled);
            }
            state.attackIntentActive = false;
            actor.fireTimer = definition.attackCooldownSeconds;
            state.revision = ++revision_;
            continue;
        }
        const bool combatSpawning = actor.combatState.initialized &&
            actor.combatState.phase == EnemyCombatPhase::Spawning;
        if (definition.maintainForwardEngagementBand &&
            ((actor.entranceExitState.initialized && actor.entranceExitState.exitRequested) ||
             state.engagementBandExitRequested)) {
            state.attackIntentActive = false;
            state.telegraphPresented = false;
            state.attackTimeRemaining = 0.0f;
            state.engagementBandExitRequested = true;
            EnterState(actor, EnemyBehaviorState::Retreating);
        }
        if (combatSpawning) {
            // Spawning visuals must follow the cart too; otherwise a delayed
            // entrance can already be overtaken when combat becomes active.
            if (definition.maintainForwardEngagementBand) {
                ApplyMovement(actor, dt, playerDistance, playerForwardSpeed);
            }
            state.state = EnemyBehaviorState::Dormant;
            state.stateElapsedSeconds = 0.0f;
            actor.fireTimer = (std::max)(actor.fireTimer, definition.attackLeadSeconds);
            state.revision = ++revision_;
            continue;
        }
        if (state.state == EnemyBehaviorState::Dormant) {
            EnterState(actor, EnemyBehaviorState::Entering);
        }

        state.stateElapsedSeconds += dt;
        state.attackCooldownRemaining = (std::max)(
            0.0f, state.attackCooldownRemaining - dt);
        switch (state.state) {
        case EnemyBehaviorState::Entering:
            if (state.stateElapsedSeconds >= definition.entryDurationSeconds) {
                EnterState(actor, EnemyBehaviorState::Positioning);
            }
            break;
        case EnemyBehaviorState::Positioning:
            if (state.stateElapsedSeconds >= definition.positioningDurationSeconds &&
                state.attackCooldownRemaining <= 0.0f &&
                (!definition.maintainForwardEngagementBand ||
                    state.engagementBandAttackAllowed) &&
                (!definition.choreographedAttackPass ||
                    HasAttackPassTurn(actor, runtime))) {
                EnterState(actor, EnemyBehaviorState::Aiming);
            }
            break;
        case EnemyBehaviorState::Aiming:
            if (state.stateElapsedSeconds >= definition.aimingDurationSeconds &&
                (!definition.maintainForwardEngagementBand ||
                    state.engagementBandAttackAllowed)) {
                BeginAttackIntent(actor);
            }
            break;
        case EnemyBehaviorState::RequestingAttack:
            // Queuing must never consume the readable charge-up interval.
            if (actor.fireEnvironmentReady &&
                actor.attackState.tokenReserved && state.telegraphPresented) {
                state.attackTimeRemaining = (std::max)(
                    0.0f, state.attackTimeRemaining - dt);
            }
            actor.fireTimer = state.attackTimeRemaining;
            break;
        case EnemyBehaviorState::Evading:
            if (definition.choreographedAttackPass &&
                state.stateElapsedSeconds >= definition.attackPassRecoilSeconds) {
                EnterState(actor, EnemyBehaviorState::Retreating);
                runtime.EnemyEntranceExit().RequestActorExit(actor.actorId);
                state.engagementBandExitRequested = true;
            } else if (!definition.choreographedAttackPass &&
                state.stateElapsedSeconds >= definition.evadeDurationSeconds) {
                EnterState(actor, EnemyBehaviorState::Repositioning);
            }
            break;
        case EnemyBehaviorState::Repositioning:
            if (state.stateElapsedSeconds >= definition.repositionDurationSeconds) {
                EnterState(actor, EnemyBehaviorState::Positioning);
            }
            break;
        case EnemyBehaviorState::Retreating:
        case EnemyBehaviorState::Disabled:
        case EnemyBehaviorState::Dormant:
            break;
        }

        ApplyMovement(actor, dt, playerDistance, playerForwardSpeed);
        if (definition.maintainForwardEngagementBand) {
            ++frame_.engagementBandActors;
            if (!state.engagementBandAttackAllowed &&
                state.attackIntentActive) {
                if (CancelAttackIntent(actor, 0.18f)) {
                    ++frame_.engagementBandSuppressedAttacks;
                }
            }
            if (!actor.HoldsCombatPositionUntilResolved() && !state.engagementBandExitRequested &&
                state.engagementBandForwardDistance <=
                    definition.engagementBandDisengageForwardDistance) {
                state.attackIntentActive = false;
                state.telegraphPresented = false;
                state.attackTimeRemaining = 0.0f;
                actor.fireTimer = (std::max)(
                    actor.fireTimer, definition.attackCooldownSeconds);
                EnterState(actor, EnemyBehaviorState::Retreating,
                    EnemyBehaviorEventKind::StateChanged);
                runtime.EnemyEntranceExit().RequestActorExit(actor.actorId);
                state.engagementBandExitRequested = true;
                ++frame_.engagementBandForcedExits;
                state.revision = ++revision_;
            }
        }
        if (state.attackIntentActive) {
            EnemyAttackIntent intent{};
            intent.actorId = actor.actorId;
            intent.sequence = state.attackIntentSequence;
            intent.archetype = definition.archetype;
            intent.timeToCommit = state.attackTimeRemaining;
            intent.severity = SeverityForArchetype(definition.archetype);
            intent.waveId = actor.desc.waveId;
            intent.placementGuid = actor.desc.sourcePlacementGuid;
            intent.telegraphPresented = state.telegraphPresented;
            intent.readyToCommit = CanCommitAttack(actor);
            frame_.attackIntents.push_back(std::move(intent));
            if (state.telegraphPresented) {
                if (state.attackTimeRemaining <= 0.0f) ++frame_.readyAttacks;
            } else {
                ++frame_.waitingForTelegraph;
            }
        }
        if (definition.movementEnabled) ++frame_.movingActors;
        state.revision = ++revision_;
    }
    frame_.revision = revision_;
}

bool EnemyBehaviorSystem::MarkTelegraphPresented(CourseSpawnRuntime& runtime,
    uint32_t actorId,
    uint64_t attackIntentSequence) {
    return runtime.MarkEnemyBehaviorTelegraph(*this, actorId, attackIntentSequence);
}

bool EnemyBehaviorSystem::MarkTelegraphPresentedActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        uint32_t actorId,
        uint64_t attackIntentSequence) {
    for (CourseEnemyActor& actor : actors) {
        EnemyBehaviorRuntimeState& state = actor.behaviorState;
        if (actor.actorId == actorId && state.attackIntentActive &&
            state.attackIntentSequence == attackIntentSequence) {
            state.telegraphPresented = true;
            state.revision = ++revision_;
            return true;
        }
    }
    return false;
}

bool EnemyBehaviorSystem::CanCommitAttack(
    const CourseEnemyActor& actor) const noexcept {
    const EnemyBehaviorDefinition& definition = actor.behaviorDefinition;
    const EnemyBehaviorRuntimeState& state = actor.behaviorState;
    if (!definition.commercialBehavior) return true;
    return actor.fireEnvironmentReady && state.attackIntentActive && state.attackTimeRemaining <= 0.0f &&
        (!definition.maintainForwardEngagementBand ||
            state.engagementBandAttackAllowed) &&
        (!definition.requireTelegraphPresentation || state.telegraphPresented);
}

bool EnemyBehaviorSystem::NotifyAttackCommitted(CourseEnemyActor& actor) {
    if (!actor.behaviorDefinition.commercialBehavior) return true;
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    if (!CanCommitAttack(actor)) return false;
    state.committedAttackSequence = state.attackIntentSequence;
    state.attackIntentActive = false;
    state.telegraphPresented = false;
    state.attackTimeRemaining = 0.0f;
    state.attackCooldownRemaining = (std::max)(
        actor.behaviorDefinition.attackCooldownSeconds,
        actor.desc.fireInterval);
    actor.fireTimer = state.attackCooldownRemaining;
    EnterState(actor, EnemyBehaviorState::Evading,
        EnemyBehaviorEventKind::AttackCommitted);
    return true;
}

bool EnemyBehaviorSystem::CancelAttackIntent(
    CourseEnemyActor& actor,
    float cooldownSeconds) {
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    if (!state.initialized || !state.attackIntentActive) return false;
    state.attackIntentActive = false;
    state.telegraphPresented = false;
    state.attackTimeRemaining = 0.0f;
    state.attackCooldownRemaining = (std::max)(
        state.attackCooldownRemaining,
        (std::max)(0.0f, cooldownSeconds));
    actor.fireTimer = state.attackCooldownRemaining;
    EnterState(actor, EnemyBehaviorState::Repositioning,
        EnemyBehaviorEventKind::StateChanged);
    state.revision = ++revision_;
    return true;
}

std::vector<EnemyBehaviorEvent> EnemyBehaviorSystem::ConsumeEvents() {
    std::vector<EnemyBehaviorEvent> result = std::move(pendingEvents_);
    pendingEvents_.clear();
    return result;
}

void EnemyBehaviorSystem::EnterState(
    CourseEnemyActor& actor,
    EnemyBehaviorState newState,
    EnemyBehaviorEventKind eventKind) {
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    if (state.state == newState) return;
    if (newState == EnemyBehaviorState::Aiming &&
        actor.behaviorDefinition.choreographedAttackPass) {
        state.attackPassHoldForwardDistance = state.engagementBandForwardDistance;
    }
    state.state = newState;
    state.stateElapsedSeconds = 0.0f;
    QueueEvent(actor, eventKind);
}

void EnemyBehaviorSystem::BeginAttackIntent(CourseEnemyActor& actor) {
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    ++state.attackIntentSequence;
    if (state.attackIntentSequence == 0) state.attackIntentSequence = 1;
    EnterState(actor, EnemyBehaviorState::RequestingAttack,
        EnemyBehaviorEventKind::AttackRequested);
    state.attackIntentActive = true;
    state.telegraphPresented = false;
    state.attackTimeRemaining = (std::max)(
        0.05f, actor.behaviorDefinition.attackLeadSeconds);
    actor.fireTimer = state.attackTimeRemaining;
}

void EnemyBehaviorSystem::ApplyMovement(
    CourseEnemyActor& actor,
    float deltaTime,
    float playerDistance,
    float playerForwardSpeed) {
    EnemyBehaviorRuntimeState& state = actor.behaviorState;
    const EnemyBehaviorDefinition& definition = actor.behaviorDefinition;
    if (definition.maintainForwardEngagementBand) {
        const float baseForwardDistance = actor.desc.spawnDistance +
            state.authoredForwardOffset + state.integratedForwardOffset -
            playerDistance;
        const bool holdingPose = definition.choreographedAttackPass &&
            state.attackPassHoldForwardDistance > 0.0f &&
            (state.state == EnemyBehaviorState::Aiming ||
             state.state == EnemyBehaviorState::RequestingAttack ||
             state.state == EnemyBehaviorState::Evading ||
             state.state == EnemyBehaviorState::Retreating);
        const float desiredForwardDistance = holdingPose
            ? state.attackPassHoldForwardDistance
            : state.hoverPreferredForwardDistance > 0.0f
                ? state.hoverPreferredForwardDistance : definition.engagementBandPreferredForwardDistance;
        const float error = desiredForwardDistance - baseForwardDistance;
        const float correction = (std::clamp)(
            error * definition.engagementBandPositionGain,
            -definition.engagementBandMaximumCorrectionSpeed,
            definition.engagementBandMaximumCorrectionSpeed);
        const float targetVelocity = (std::clamp)(
            playerForwardSpeed + correction,
            0.0f,
            playerForwardSpeed +
                definition.engagementBandMaximumCorrectionSpeed);
        const float velocityBlend = 1.0f - std::exp(
            -definition.engagementBandVelocityResponse * deltaTime);
        state.engagementBandVelocity +=
            (targetVelocity - state.engagementBandVelocity) *
            (std::clamp)(velocityBlend, 0.0f, 1.0f);
        if (holdingPose) {
            // Once aiming, match cart travel exactly; residual approach velocity
            // must not pull the muzzle through its newly acquired firing pose.
            state.integratedForwardOffset += error;
            state.engagementBandVelocity = playerForwardSpeed;
        } else {
            state.integratedForwardOffset += state.engagementBandVelocity * deltaTime;
        }
        if (std::abs(correction) > 0.10f) {
            ++frame_.engagementBandCorrections;
        }
    } else {
        state.integratedForwardOffset +=
            actor.desc.forwardSpeed * definition.forwardMotionScale * deltaTime;
    }
    const float entryBlend = definition.entryDurationSeconds > 0.0f
        ? SmoothStep(actor.age / definition.entryDurationSeconds)
        : 1.0f;
    const float phase = actor.age * definition.movementFrequency * kTau +
        state.deterministicPhase;
    const bool individualHover = actor.desc.meshId == "twin_shield_hull" &&
        definition.maintainForwardEngagementBand && !definition.choreographedAttackPass;
    float lateral = 0.0f;
    float vertical = 0.0f;
    float forward = 0.0f;
    if (definition.movementEnabled && !definition.choreographedAttackPass) {
        switch (definition.archetype) {
        case EnemyBehaviorArchetype::Assault:
            lateral = std::sin(phase) * definition.lateralAmplitude;
            vertical = std::sin(phase * 0.53f) * definition.verticalAmplitude;
            break;
        case EnemyBehaviorArchetype::Flanker: {
            const float side = (actor.actorId & 1u) != 0 ? 1.0f : -1.0f;
            lateral = side * definition.lateralAmplitude *
                (0.58f + 0.42f * std::sin(phase));
            vertical = std::cos(phase * 0.64f) * definition.verticalAmplitude;
            break;
        }
        case EnemyBehaviorArchetype::Sniper:
            lateral = std::sin(phase) * definition.lateralAmplitude;
            vertical = std::cos(phase * 0.72f) * definition.verticalAmplitude;
            break;
        case EnemyBehaviorArchetype::Interceptor:
            lateral = std::sin(phase) * definition.lateralAmplitude;
            vertical = std::sin(phase * 2.0f) * definition.verticalAmplitude;
            forward = std::cos(phase) * 1.1f;
            break;
        case EnemyBehaviorArchetype::Turret:
            break;
        case EnemyBehaviorArchetype::Support:
            lateral = std::sin(phase) * definition.lateralAmplitude;
            vertical = std::cos(phase) * definition.verticalAmplitude;
            break;
        case EnemyBehaviorArchetype::Boss:
            lateral = std::sin(phase) * definition.lateralAmplitude;
            vertical = std::sin(phase * 0.5f) * definition.verticalAmplitude;
            forward = std::cos(phase * 0.35f) * 0.8f;
            break;
        }
    }
    if (individualHover && definition.movementEnabled) {
        const bool aiming = state.state == EnemyBehaviorState::Aiming ||
            (state.attackIntentActive && actor.attackState.tokenReserved);
        state.hoverMotionRate += ((aiming ? 0.24f : 1.0f) - state.hoverMotionRate) *
            (1.0f - std::exp(-3.0f * deltaTime));
        state.hoverMotionSeconds += deltaTime * state.hoverMotionRate;
        const float identity = state.deterministicPhase;
        const float rate = 0.72f + 0.55f * (0.5f + 0.5f * std::sin(identity * 2.17f));
        const float t = state.hoverMotionSeconds * definition.movementFrequency * kTau * rate;
        // Different phases and incommensurate frequencies prevent a shared
        // pendulum motion, while keeping the authored movement envelope.
        lateral = definition.lateralAmplitude * (0.65f * std::sin(t + identity) +
            0.28f * std::sin(t * 0.417f + identity * 2.31f));
        vertical = definition.verticalAmplitude * (0.60f * std::sin(t * 0.63f + identity * 1.73f) +
            0.32f * std::sin(t * 1.17f + identity * 0.59f));
        forward = definition.lateralAmplitude + definition.verticalAmplitude > 0.0f
            ? 1.25f * std::sin(t * 0.31f + identity * 1.37f) +
                0.55f * std::sin(t * 0.77f + identity * 2.13f) : 0.0f;
    }
    if (state.state == EnemyBehaviorState::Evading &&
        !definition.choreographedAttackPass && !individualHover) {
        const float evadeProgress = actor.behaviorDefinition.evadeDurationSeconds > 0.0f
            ? Saturate(state.stateElapsedSeconds /
                actor.behaviorDefinition.evadeDurationSeconds)
            : 1.0f;
        const float side = (actor.actorId & 1u) != 0 ? 1.0f : -1.0f;
        lateral += side * std::sin(evadeProgress * 3.14159265f) *
            (std::max)(0.8f, definition.lateralAmplitude * 0.42f);
    }
    const float lateralVelocity = deltaTime > 0.000001f
        ? (lateral * entryBlend - state.behaviorLateralOffset) / deltaTime : 0.0f;
    const float verticalVelocity = deltaTime > 0.000001f
        ? (vertical * entryBlend - state.behaviorVerticalOffset) / deltaTime : 0.0f;
    state.behaviorForwardOffset = forward * entryBlend;
    state.behaviorLateralOffset = lateral * entryBlend;
    state.behaviorVerticalOffset = vertical * entryBlend;
    const float previousBank = state.presentationBankRadians;
    state.presentationYawRadians = lateral * 0.025f;
    state.presentationPitchRadians = -forward * 0.035f;
    state.presentationBankRadians = (std::clamp)(
        -std::cos(phase) * definition.maximumBankRadians,
        -definition.maximumBankRadians,
        definition.maximumBankRadians);
    if (individualHover) {
        const float response = 1.0f - std::exp(-3.5f * deltaTime);
        const float desiredBank = (std::clamp)(-lateralVelocity * 0.055f, -0.10f, 0.10f);
        state.presentationBankRadians = previousBank + (desiredBank - previousBank) * response;
        state.presentationYawRadians = (std::clamp)(lateralVelocity * 0.045f, -0.10f, 0.10f);
        state.presentationPitchRadians = (std::clamp)(verticalVelocity * 0.05f, -0.065f, 0.065f);
    }
    if (definition.choreographedAttackPass) {
        state.presentationYawRadians = 0.0f;
        state.presentationPitchRadians = 0.0f;
        state.presentationBankRadians = 0.0f;
        if (state.state == EnemyBehaviorState::Retreating) {
            const float side = state.authoredLateralOffset < -0.1f ? -1.0f : 1.0f;
            const float bank = SmoothStep(state.stateElapsedSeconds / 0.35f);
            state.presentationBankRadians = -side * bank * 0.55f;
            state.presentationYawRadians = side * bank * 0.24f;
        }
    }
    actor.desc.distanceOffset = state.authoredForwardOffset +
        state.integratedForwardOffset + state.behaviorForwardOffset;
    actor.desc.lateralOffset = state.authoredLateralOffset +
        state.behaviorLateralOffset + state.safetyLateralOffset;
    actor.desc.verticalOffset = state.authoredVerticalOffset +
        state.behaviorVerticalOffset;
    state.engagementBandForwardDistance =
        actor.desc.spawnDistance + actor.desc.distanceOffset - playerDistance;
    state.engagementBandAttackAllowed =
        !definition.maintainForwardEngagementBand ||
        (state.engagementBandForwardDistance >=
                definition.engagementBandMinimumForwardDistance &&
         state.engagementBandForwardDistance <=
                definition.engagementBandMaximumForwardDistance &&
         !state.engagementBandExitRequested);
}

void EnemyBehaviorSystem::QueueEvent(
    CourseEnemyActor& actor,
    EnemyBehaviorEventKind kind) {
    constexpr size_t kMaximumPendingEvents = 512;
    if (pendingEvents_.size() >= kMaximumPendingEvents) {
        pendingEvents_.erase(pendingEvents_.begin());
    }
    pendingEvents_.push_back({
        kind,
        actor.actorId,
        actor.behaviorState.attackIntentSequence,
        actor.behaviorState.state,
        actor.desc.waveId,
        actor.desc.sourcePlacementGuid});
}

bool TryParseEnemyBehaviorArchetype(
    const std::string& text,
    EnemyBehaviorArchetype& archetype) noexcept {
    const std::string value = Lower(text);
    if (value == "assault") archetype = EnemyBehaviorArchetype::Assault;
    else if (value == "flanker") archetype = EnemyBehaviorArchetype::Flanker;
    else if (value == "sniper") archetype = EnemyBehaviorArchetype::Sniper;
    else if (value == "interceptor") archetype = EnemyBehaviorArchetype::Interceptor;
    else if (value == "turret") archetype = EnemyBehaviorArchetype::Turret;
    else if (value == "support") archetype = EnemyBehaviorArchetype::Support;
    else if (value == "boss") archetype = EnemyBehaviorArchetype::Boss;
    else return false;
    return true;
}

const char* ToString(EnemyBehaviorArchetype archetype) noexcept {
    switch (archetype) {
    case EnemyBehaviorArchetype::Assault: return "Assault";
    case EnemyBehaviorArchetype::Flanker: return "Flanker";
    case EnemyBehaviorArchetype::Sniper: return "Sniper";
    case EnemyBehaviorArchetype::Interceptor: return "Interceptor";
    case EnemyBehaviorArchetype::Turret: return "Turret";
    case EnemyBehaviorArchetype::Support: return "Support";
    case EnemyBehaviorArchetype::Boss: return "Boss";
    }
    return "Unknown";
}

const char* ToString(EnemyBehaviorState state) noexcept {
    switch (state) {
    case EnemyBehaviorState::Dormant: return "Dormant";
    case EnemyBehaviorState::Entering: return "Entering";
    case EnemyBehaviorState::Positioning: return "Positioning";
    case EnemyBehaviorState::Aiming: return "Aiming";
    case EnemyBehaviorState::RequestingAttack: return "RequestingAttack";
    case EnemyBehaviorState::Evading: return "Evading";
    case EnemyBehaviorState::Repositioning: return "Repositioning";
    case EnemyBehaviorState::Retreating: return "Retreating";
    case EnemyBehaviorState::Disabled: return "Disabled";
    }
    return "Unknown";
}

const char* ToString(EnemyBehaviorEventKind kind) noexcept {
    switch (kind) {
    case EnemyBehaviorEventKind::StateChanged: return "StateChanged";
    case EnemyBehaviorEventKind::AttackRequested: return "AttackRequested";
    case EnemyBehaviorEventKind::AttackCommitted: return "AttackCommitted";
    case EnemyBehaviorEventKind::Disabled: return "Disabled";
    }
    return "Unknown";
}
