#include "EnemyEntranceExitDirector.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CourseSpawnRuntime.h"

namespace {
float Saturate(float value) noexcept {
    return (std::clamp)(value, 0.0f, 1.0f);
}
float SmoothStep(float value) noexcept {
    const float t = Saturate(value);
    return t * t * (3.0f - 2.0f * t);
}
std::string FormationId(const CourseEnemyActor& actor) {
    if (actor.formationState.initialized &&
        !actor.formationState.formationId.empty()) {
        return actor.formationState.formationId;
    }
    if (!actor.desc.formationDefinition.definitionId.empty()) {
        return actor.desc.formationDefinition.definitionId;
    }
    return actor.desc.waveId.empty()
        ? "actor:" + std::to_string(actor.actorId) : actor.desc.waveId;
}
EnemyFormationDefinition Definition(
    const CourseEnemyActor& actor, const CourseSpawnRuntime& runtime) {
    if (!actor.desc.formationDefinition.definitionId.empty() &&
        actor.desc.formationDefinition.Validate(nullptr)) {
        return actor.desc.formationDefinition;
    }
    const std::string formationId = FormationId(actor);
    if (const EnemyFormationDefinition* definition =
            runtime.EnemyFormations().FindDefinition(formationId);
        definition != nullptr && definition->Validate(nullptr)) {
        return *definition;
    }
    return EnemyFormationDefinition::CommercialDefault(formationId);
}

void EntranceOffset(const EnemyFormationDefinition& definition,
                    const CourseEnemyActor& actor, float remaining,
                    float& forward, float& lateral, float& vertical) {
    const float side = (actor.formationState.slotIndex & 1u) != 0u ? -1.0f : 1.0f;
    forward = 0.0f; lateral = 0.0f; vertical = 0.0f;
    switch (definition.entranceStyle) {
    case EnemyEntranceStyle::FromAhead:
        forward = definition.entranceForwardDistance * remaining;
        lateral = side * definition.entranceSideDistance * 0.20f * remaining;
        break;
    case EnemyEntranceStyle::SweepLeft:
        forward = definition.entranceForwardDistance * 0.35f * remaining;
        lateral = -definition.entranceSideDistance * remaining;
        break;
    case EnemyEntranceStyle::SweepRight:
        forward = definition.entranceForwardDistance * 0.35f * remaining;
        lateral = definition.entranceSideDistance * remaining;
        break;
    case EnemyEntranceStyle::Dive:
        forward = definition.entranceForwardDistance * 0.25f * remaining;
        vertical = definition.entranceSideDistance * remaining;
        break;
    case EnemyEntranceStyle::Rise:
        forward = definition.entranceForwardDistance * 0.25f * remaining;
        vertical = -definition.entranceSideDistance * remaining;
        break;
    case EnemyEntranceStyle::RearChase:
        forward = -definition.entranceForwardDistance * remaining;
        lateral = side * definition.entranceSideDistance * 0.35f * remaining;
        break;
    }
}

void ExitOffset(const EnemyFormationDefinition& definition,
                const CourseEnemyActor& actor, float progress,
                float& forward, float& lateral, float& vertical) {
    // A departing wing keeps its authored side when a defeated/pruned leader
    // causes formation indices to be reassigned.
    const float side = actor.behaviorDefinition.maintainForwardEngagementBand
        ? (actor.behaviorState.authoredLateralOffset < -0.1f ? -1.0f : 1.0f)
        : ((actor.formationState.slotIndex & 1u) != 0u ? -1.0f : 1.0f);
    forward = 0.0f; lateral = 0.0f; vertical = 0.0f;
    switch (definition.exitStyle) {
    case EnemyExitStyle::ForwardBreak:
        forward = definition.exitForwardDistance * progress;
        break;
    case EnemyExitStyle::SplitSides:
        forward = definition.exitForwardDistance * 0.35f * progress;
        lateral = side * definition.exitSideDistance * progress;
        break;
    case EnemyExitStyle::Climb:
        forward = definition.exitForwardDistance * 0.20f * progress;
        vertical = definition.exitSideDistance * progress;
        break;
    case EnemyExitStyle::Dive:
        forward = definition.exitForwardDistance * 0.20f * progress;
        vertical = -definition.exitSideDistance * progress;
        break;
    case EnemyExitStyle::RearRetreat:
        forward = -definition.exitForwardDistance * progress;
        lateral = side * definition.exitSideDistance * 0.30f * progress;
        break;
    }
}
}

void EnemyEntranceExitDirector::Reset() {
    actorExitRequests_.clear();
    formationExitRequests_.clear();
    frame_ = {};
    eventSequence_ = 1;
    revision_ = 0;
}

void EnemyEntranceExitDirector::BeginFrame(CourseSpawnRuntime& runtime) {
    runtime.BeginEnemyEntranceExitFrame(*this);
}

void EnemyEntranceExitDirector::BeginFrameActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime) {
    for (CourseEnemyActor& actor : actors) {
        EnemyEntranceExitRuntimeState& state = actor.entranceExitState;
        if (!state.initialized) continue;
        actor.desc.distanceOffset -= state.appliedForwardOffset;
        actor.desc.lateralOffset -= state.appliedLateralOffset;
        actor.desc.verticalOffset -= state.appliedVerticalOffset;
        state.appliedForwardOffset = 0.0f;
        state.appliedLateralOffset = 0.0f;
        state.appliedVerticalOffset = 0.0f;
    }
}

bool EnemyEntranceExitDirector::RequestActorExit(uint32_t actorId) {
    if (actorId == 0) return false;
    return actorExitRequests_.insert(actorId).second;
}

bool EnemyEntranceExitDirector::RequestFormationExit(std::string formationId) {
    if (formationId.empty()) return false;
    return formationExitRequests_.insert(std::move(formationId)).second;
}

void EnemyEntranceExitDirector::QueueEvent(
    EnemyEntranceExitEventKind kind, uint32_t actorId,
    const std::string& formationId) {
    constexpr size_t kMaximumEvents = 64;
    if (frame_.events.size() >= kMaximumEvents) return;
    frame_.events.push_back({kind, actorId, formationId, eventSequence_++});
}

void EnemyEntranceExitDirector::Update(CourseSpawnRuntime& runtime, float deltaTime) {
    runtime.UpdateEnemyEntranceExit(*this, deltaTime);
}

void EnemyEntranceExitDirector::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        float deltaTime) {
    frame_ = {};
    const float dt = std::isfinite(deltaTime)
        ? (std::clamp)(deltaTime, 0.0f, 0.25f) : 0.0f;
    for (CourseEnemyActor& actor : actors) {
        const std::string formationId = FormationId(actor);
        const bool explicitlyAuthored =
            !actor.desc.formationDefinition.definitionId.empty();
        EnemyEntranceExitRuntimeState& state = actor.entranceExitState;
        const bool explicitlyRequested = state.exitRequested ||
            actorExitRequests_.contains(actor.actorId) ||
            formationExitRequests_.contains(formationId);
        if (!explicitlyAuthored &&
            runtime.EnemyFormations().FindDefinition(formationId) == nullptr &&
            !explicitlyRequested &&
            !actor.behaviorDefinition.maintainForwardEngagementBand) {
            continue;
        }
        const EnemyFormationDefinition definition = Definition(actor, runtime);
        if (!state.initialized) {
            state = {};
            state.initialized = true;
            state.phase = EnemyEntranceExitPhase::Pending;
            state.delayRemainingSeconds = definition.entranceStaggerSeconds *
                static_cast<float>(actor.formationState.slotIndex);
            state.attackSuppressed = true;
            state.targetable = false;
            state.revision = ++revision_;
        }
        const bool requested = explicitlyRequested;
        const bool lifetimeExit = actor.desc.lifetime > definition.exitDurationSeconds &&
            actor.age >= actor.desc.lifetime - definition.exitDurationSeconds;
        if ((requested || lifetimeExit) &&
            state.phase != EnemyEntranceExitPhase::Exiting &&
            state.phase != EnemyEntranceExitPhase::Exited) {
            state.phase = EnemyEntranceExitPhase::Exiting;
            state.phaseElapsedSeconds = 0.0f;
            state.exitRequested = true;
            state.attackSuppressed = true;
            state.targetable = false;
            QueueEvent(EnemyEntranceExitEventKind::ExitStarted,
                actor.actorId, formationId);
        }

        switch (state.phase) {
        case EnemyEntranceExitPhase::Pending:
            state.delayRemainingSeconds = (std::max)(
                0.0f, state.delayRemainingSeconds - dt);
            if (state.delayRemainingSeconds <= 0.0f) {
                state.phase = EnemyEntranceExitPhase::Entering;
                state.phaseElapsedSeconds = 0.0f;
                QueueEvent(EnemyEntranceExitEventKind::EntranceStarted,
                    actor.actorId, formationId);
            }
            [[fallthrough]];
        case EnemyEntranceExitPhase::Entering: {
            if (state.phase != EnemyEntranceExitPhase::Entering) break;
            state.phaseElapsedSeconds += dt;
            const float progress = SmoothStep(
                state.phaseElapsedSeconds / definition.entranceDurationSeconds);
            EntranceOffset(definition, actor, 1.0f - progress,
                state.appliedForwardOffset, state.appliedLateralOffset,
                state.appliedVerticalOffset);
            state.presentationAlpha = progress;
            state.presentationScale = 0.72f + progress * 0.28f;
            state.targetable = progress >= 0.68f;
            state.attackSuppressed = progress < 0.98f;
            ++frame_.enteringActors;
            if (progress >= 1.0f) {
                state.phase = EnemyEntranceExitPhase::Active;
                state.phaseElapsedSeconds = 0.0f;
                state.presentationAlpha = 1.0f;
                state.presentationScale = 1.0f;
                state.attackSuppressed = false;
                state.targetable = true;
                QueueEvent(EnemyEntranceExitEventKind::EntranceCompleted,
                    actor.actorId, formationId);
            }
            break;
        }
        case EnemyEntranceExitPhase::Active:
            state.presentationAlpha = 1.0f;
            state.presentationScale = 1.0f;
            state.attackSuppressed = false;
            state.targetable = true;
            ++frame_.activeActors;
            break;
        case EnemyEntranceExitPhase::Exiting: {
            state.phaseElapsedSeconds += dt;
            const float progress = SmoothStep(
                state.phaseElapsedSeconds / definition.exitDurationSeconds);
            ExitOffset(definition, actor, progress,
                state.appliedForwardOffset, state.appliedLateralOffset,
                state.appliedVerticalOffset);
            // Keep a splitting wing visible while it actually peels away.
            // The final fade hides its removal after it reaches the edge.
            state.presentationAlpha = definition.exitStyle == EnemyExitStyle::SplitSides
                ? 1.0f - SmoothStep((progress - 0.60f) / 0.40f)
                : 1.0f - progress;
            state.presentationScale = 1.0f - progress * 0.18f;
            state.attackSuppressed = true;
            state.targetable = false;
            ++frame_.exitingActors;
            if (progress >= 1.0f) {
                state.phase = EnemyEntranceExitPhase::Exited;
                state.exitComplete = true;
                state.presentationAlpha = 0.0f;
                state.presentationScale = 0.0f;
                QueueEvent(EnemyEntranceExitEventKind::ExitCompleted,
                    actor.actorId, formationId);
            }
            break;
        }
        case EnemyEntranceExitPhase::Exited:
            state.attackSuppressed = true;
            state.targetable = false;
            state.exitComplete = true;
            ++frame_.exitedActors;
            break;
        }
        actor.desc.distanceOffset += state.appliedForwardOffset;
        actor.desc.lateralOffset += state.appliedLateralOffset;
        actor.desc.verticalOffset += state.appliedVerticalOffset;
        state.revision = ++revision_;
    }
    actorExitRequests_.clear();
    formationExitRequests_.clear();
    frame_.revision = revision_;
}

const char* ToString(EnemyEntranceExitPhase phase) noexcept {
    switch (phase) {
    case EnemyEntranceExitPhase::Pending: return "Pending";
    case EnemyEntranceExitPhase::Entering: return "Entering";
    case EnemyEntranceExitPhase::Active: return "Active";
    case EnemyEntranceExitPhase::Exiting: return "Exiting";
    case EnemyEntranceExitPhase::Exited: return "Exited";
    }
    return "Unknown";
}

const char* ToString(EnemyEntranceExitEventKind kind) noexcept {
    switch (kind) {
    case EnemyEntranceExitEventKind::EntranceStarted: return "EntranceStarted";
    case EnemyEntranceExitEventKind::EntranceCompleted: return "EntranceCompleted";
    case EnemyEntranceExitEventKind::ExitStarted: return "ExitStarted";
    case EnemyEntranceExitEventKind::ExitCompleted: return "ExitCompleted";
    }
    return "Unknown";
}
