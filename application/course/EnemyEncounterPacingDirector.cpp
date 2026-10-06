#include "EnemyEncounterPacingDirector.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"
#include "EnemyEncounterReadabilityDirector.h"

namespace {
bool IsLivingActor(const CourseEnemyActor& actor) noexcept {
    if (actor.entranceExitState.exitComplete ||
        actor.entranceExitState.phase == EnemyEntranceExitPhase::Exited) {
        return false;
    }
    if (actor.combatState.initialized) {
        return actor.combatState.currentHitPoints > 0.0f &&
            actor.combatState.phase != EnemyCombatPhase::Dying &&
            actor.combatState.phase != EnemyCombatPhase::Retired;
    }
    return actor.desc.hitPoints > 0.0f;
}

bool MatchesWave(
    const CourseEnemyActor& actor,
    const EnemyEncounterBeatDefinition& definition) noexcept {
    return actor.desc.waveId == definition.waveGuid;
}
} // namespace

void EnemyEncounterPacingDirector::Reset(CourseSpawnRuntime* runtime) {
    RestoreCoordinator(runtime);
    if (runtime != nullptr) {
        runtime->ResetEnemyEncounterPacing();
    }
    frame_ = {};
    activeDefinition_ = nullptr;
    completedBeatGuids_.clear();
    formationExitRequested_ = false;
    resolveStallReported_ = false;
    revision_ = 0;
}

const EnemyEncounterPacingFrame& EnemyEncounterPacingDirector::Update(
    const EnemyEncounterPacingInput& input) {
    EnemyEncounterPacingFrame next{};
    next.revision = ++revision_;
    if (input.runtime == nullptr || input.course == nullptr) {
        RestoreCoordinator(input.runtime);
        frame_ = std::move(next);
        return frame_;
    }

    CourseSpawnRuntime& runtime = *input.runtime;
    const auto hasPendingConfiguredBeat =
        [this, &input](std::string_view waveGuid) {
            return !waveGuid.empty() && std::any_of(
                input.course->encounterBeats.begin(),
                input.course->encounterBeats.end(),
                [this, waveGuid](
                    const EnemyEncounterBeatDefinition& definition) {
                    return definition.enabled &&
                        definition.waveGuid == waveGuid &&
                        !completedBeatGuids_.contains(definition.editorGuid);
                });
        };
    if (activeDefinition_ == nullptr && input.gameplayActive) {
        const EnemyEncounterBeatDefinition* selected = nullptr;
        for (const EnemyEncounterBeatDefinition& definition :
             input.course->encounterBeats) {
            if (!definition.enabled ||
                completedBeatGuids_.contains(definition.editorGuid) ||
                input.currentDistance + definition.prewarmDistance + 0.001f <
                    definition.triggerRailDistance) {
                continue;
            }
            if (selected == nullptr || definition.priority > selected->priority ||
                (definition.priority == selected->priority &&
                 definition.triggerRailDistance < selected->triggerRailDistance)) {
                selected = &definition;
            }
        }
        if (selected != nullptr) BeginBeat(*selected, runtime, next);
    }

    if (activeDefinition_ == nullptr) {
        RestoreCoordinator(&runtime);
        for (const CourseEnemyActor& actor : runtime.Enemies()) {
            const bool holdForAuthoredBeat = input.gameplayActive &&
                hasPendingConfiguredBeat(actor.desc.waveId);
            runtime.SetEnemyEncounterPacing(actor.actorId, holdForAuthoredBeat, !holdForAuthoredBeat);
        }
        frame_ = std::move(next);
        return frame_;
    }

    const EnemyEncounterBeatDefinition& definition = *activeDefinition_;
    next.active = true;
    next.activeBeatGuid = definition.editorGuid;
    next.encounterId = definition.encounterId;
    next.waveGuid = definition.waveGuid;
    next.displayName = definition.displayName;
    next.phase = frame_.active ? frame_.phase : EnemyEncounterBeatPhase::Establish;
    next.phaseElapsedSeconds = frame_.active ? frame_.phaseElapsedSeconds : 0.0f;
    next.totalElapsedSeconds = frame_.active ? frame_.totalElapsedSeconds : 0.0f;
    next.definition = definition;
    next.maximumConcurrentAttackers = definition.maximumConcurrentAttackers;
    next.maximumThreatBudget = definition.maximumThreatBudget;

    if (input.gameplayActive) {
        const float dt = std::isfinite(input.deltaTime)
            ? (std::clamp)(input.deltaTime, 0.0f, 0.25f) : 0.0f;
        next.totalElapsedSeconds += dt;
        bool hasEligibleActor = false;
        for (const CourseEnemyActor& actor : runtime.Enemies()) {
            if (MatchesWave(actor, definition) && IsLivingActor(actor)) {
                hasEligibleActor = true;
                break;
            }
        }
        if (next.phase != EnemyEncounterBeatPhase::Establish ||
            hasEligibleActor) {
            next.phaseElapsedSeconds += dt;
        }
    }

    for (const CourseEnemyActor& actor : runtime.Enemies()) {
        if (!MatchesWave(actor, definition) || !IsLivingActor(actor)) {
            const bool holdForAuthoredBeat = input.gameplayActive &&
                IsLivingActor(actor) &&
                hasPendingConfiguredBeat(actor.desc.waveId);
            runtime.SetEnemyEncounterPacing(actor.actorId, holdForAuthoredBeat, !holdForAuthoredBeat);
            continue;
        }
        ++next.eligibleActors;
        bool actorReadable = false;
        if (input.readability != nullptr) {
            const auto found = std::find_if(
                input.readability->actors.begin(),
                input.readability->actors.end(),
                [&actor](const EnemyEncounterActorReadability& value) {
                    return value.actorId == actor.actorId;
                });
            actorReadable = found != input.readability->actors.end() &&
                (found->screenReadable || found->warningReadable);
        }
        if (actorReadable) ++next.readableActors;
    }
    next.readableRatio = next.eligibleActors > 0
        ? static_cast<float>(next.readableActors) /
            static_cast<float>(next.eligibleActors)
        : 0.0f;
    next.attackWindowOpen = next.phase == EnemyEncounterBeatPhase::Attack;
    next.cameraCompositionRequested =
        next.phase != EnemyEncounterBeatPhase::Complete &&
        next.phase != EnemyEncounterBeatPhase::Failed;

    EnemyAttackCoordinatorSettings coordinator = runtime.EnemyAttacks().Settings();
    coordinator.maximumConcurrentAttackers =
        definition.maximumConcurrentAttackers;
    coordinator.maximumAttackersPerWave =
        definition.maximumConcurrentAttackers;
    coordinator.maximumThreatBudget = definition.maximumThreatBudget;
    (void)runtime.EnemyAttacks().Configure(coordinator);

    const bool readableEnough =
        next.readableRatio + 0.0001f >= definition.requiredReadableRatio;
    const bool triggerReached =
        input.currentDistance + 0.001f >= definition.triggerRailDistance;
    if (input.currentDistance >= definition.endRailDistance &&
        (next.phase == EnemyEncounterBeatPhase::Establish ||
         next.phase == EnemyEncounterBeatPhase::Threaten ||
         next.phase == EnemyEncounterBeatPhase::Attack)) {
        EnterPhase(EnemyEncounterBeatPhase::Recovery, next,
            "authored encounter distance interval ended");
    }
    switch (next.phase) {
    case EnemyEncounterBeatPhase::Establish:
        if (triggerReached && next.eligibleActors > 0 &&
            ((next.phaseElapsedSeconds >= definition.establishMinimumSeconds &&
              readableEnough) ||
             next.phaseElapsedSeconds >= definition.establishMaximumSeconds)) {
            EnterPhase(EnemyEncounterBeatPhase::Threaten, next,
                readableEnough ? "formation established and readable"
                               : "establish maximum elapsed");
        }
        break;
    case EnemyEncounterBeatPhase::Threaten:
        if ((next.phaseElapsedSeconds >= definition.threatenMinimumSeconds &&
             readableEnough) ||
            next.phaseElapsedSeconds >= definition.threatenMaximumSeconds) {
            EnterPhase(EnemyEncounterBeatPhase::Attack, next,
                readableEnough ? "threat exposure satisfied"
                               : "threaten maximum elapsed");
        }
        break;
    case EnemyEncounterBeatPhase::Attack:
        if (next.phaseElapsedSeconds >= definition.attackMaximumSeconds ||
            (next.eligibleActors == 0 &&
             next.phaseElapsedSeconds >= definition.attackMinimumSeconds)) {
            EnterPhase(EnemyEncounterBeatPhase::Recovery, next,
                next.eligibleActors == 0 ? "encounter actors defeated"
                                         : "attack window elapsed");
        }
        break;
    case EnemyEncounterBeatPhase::Recovery:
        if (next.phaseElapsedSeconds >= definition.recoverySeconds) {
            EnterPhase(EnemyEncounterBeatPhase::ExitResolve, next,
                "recovery complete");
        }
        break;
    case EnemyEncounterBeatPhase::ExitResolve: {
        if (definition.exitSurvivorsOnResolve && !formationExitRequested_) {
            formationExitRequested_ =
                runtime.EnemyEntranceExit().RequestFormationExit(
                    definition.waveGuid);
            if (formationExitRequested_) {
                next.events.push_back({
                    EnemyEncounterPacingEventKind::FormationExitRequested,
                    definition.editorGuid,
                    definition.encounterId,
                    EnemyEncounterBeatPhase::ExitResolve,
                    EnemyEncounterBeatPhase::ExitResolve,
                    "surviving formation exit requested"});
            }
        }
        const bool truthClear = input.readability != nullptr &&
            input.readability->truth.safeToResolveSession;
        const bool canComplete = next.eligibleActors == 0 &&
            (!definition.requireCombatTruthForCompletion || truthClear);
        if (canComplete) {
            EnterPhase(EnemyEncounterBeatPhase::Complete, next,
                "actors exited and Combat Truth resolved");
        } else if (next.phaseElapsedSeconds >=
                   definition.resolveTimeoutSeconds) {
            next.resolveStalled = true;
            if (!resolveStallReported_) {
                resolveStallReported_ = true;
                next.events.push_back({
                    EnemyEncounterPacingEventKind::ResolveStalled,
                    definition.editorGuid,
                    definition.encounterId,
                    EnemyEncounterBeatPhase::ExitResolve,
                    EnemyEncounterBeatPhase::ExitResolve,
                    "resolve timeout reached; fail-closed until Combat Truth clears"});
            }
        }
        break;
    }
    case EnemyEncounterBeatPhase::Complete:
    case EnemyEncounterBeatPhase::Failed:
    case EnemyEncounterBeatPhase::Dormant:
        break;
    }

    // Publish the final phase gate after transitions so the frame's
    // attackWindowOpen and actor authorization cannot disagree for one tick.
    next.gatedActors = 0;
    for (const CourseEnemyActor& actor : runtime.Enemies()) {
        if (!MatchesWave(actor, definition) || !IsLivingActor(actor)) continue;
        runtime.SetEnemyEncounterPacing(actor.actorId, input.gameplayActive,
            !input.gameplayActive || next.phase == EnemyEncounterBeatPhase::Attack);
        if (!actor.encounterPacingAttackAllowed) ++next.gatedActors;
    }
    next.attackWindowOpen = next.phase == EnemyEncounterBeatPhase::Attack;

    if (next.phase == EnemyEncounterBeatPhase::Complete) {
        completedBeatGuids_.insert(definition.editorGuid);
        next.events.push_back({
            EnemyEncounterPacingEventKind::BeatCompleted,
            definition.editorGuid,
            definition.encounterId,
            frame_.phase,
            EnemyEncounterBeatPhase::Complete,
            "encounter Beat completed"});
        for (const CourseEnemyActor& actor : runtime.Enemies()) {
            const bool holdForNextBeat = input.gameplayActive &&
                !MatchesWave(actor, definition) && IsLivingActor(actor) &&
                hasPendingConfiguredBeat(actor.desc.waveId);
            runtime.SetEnemyEncounterPacing(actor.actorId, holdForNextBeat, !holdForNextBeat);
        }
        RestoreCoordinator(&runtime);
        activeDefinition_ = nullptr;
        formationExitRequested_ = false;
        resolveStallReported_ = false;
        next.active = false;
        next.attackWindowOpen = false;
        next.cameraCompositionRequested = false;
    }
    frame_ = std::move(next);
    return frame_;
}

EnemyEncounterPacingCheckpoint
EnemyEncounterPacingDirector::CaptureCheckpoint() const {
    EnemyEncounterPacingCheckpoint checkpoint{};
    checkpoint.activeBeatGuid = activeDefinition_ != nullptr
        ? activeDefinition_->editorGuid : std::string{};
    checkpoint.phase = frame_.phase;
    checkpoint.phaseElapsedSeconds = frame_.phaseElapsedSeconds;
    checkpoint.totalElapsedSeconds = frame_.totalElapsedSeconds;
    checkpoint.completedBeatGuids.assign(
        completedBeatGuids_.begin(), completedBeatGuids_.end());
    std::sort(checkpoint.completedBeatGuids.begin(),
              checkpoint.completedBeatGuids.end());
    return checkpoint;
}

bool EnemyEncounterPacingDirector::RestoreCheckpoint(
    const EnemyEncounterPacingCheckpoint& checkpoint,
    const CourseAsset& course,
    std::string* errorMessage) {
    activeDefinition_ = nullptr;
    if (!checkpoint.activeBeatGuid.empty()) {
        const auto found = std::find_if(
            course.encounterBeats.begin(), course.encounterBeats.end(),
            [&checkpoint](const EnemyEncounterBeatDefinition& definition) {
                return definition.editorGuid == checkpoint.activeBeatGuid;
            });
        if (found == course.encounterBeats.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Encounter Beat checkpoint references a missing GUID.";
            }
            return false;
        }
        activeDefinition_ = &*found;
    }
    completedBeatGuids_.clear();
    completedBeatGuids_.insert(
        checkpoint.completedBeatGuids.begin(),
        checkpoint.completedBeatGuids.end());
    frame_ = {};
    frame_.active = activeDefinition_ != nullptr;
    frame_.phase = checkpoint.phase;
    frame_.phaseElapsedSeconds = (std::max)(
        0.0f, checkpoint.phaseElapsedSeconds);
    frame_.totalElapsedSeconds = (std::max)(
        0.0f, checkpoint.totalElapsedSeconds);
    formationExitRequested_ = false;
    resolveStallReported_ = false;
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void EnemyEncounterPacingDirector::BeginBeat(
    const EnemyEncounterBeatDefinition& definition,
    CourseSpawnRuntime& runtime,
    EnemyEncounterPacingFrame& next) {
    activeDefinition_ = &definition;
    formationExitRequested_ = false;
    resolveStallReported_ = false;
    if (!hasCoordinatorBaseline_) {
        coordinatorBaseline_ = runtime.EnemyAttacks().Settings();
        hasCoordinatorBaseline_ = true;
    }
    next.active = true;
    next.phase = EnemyEncounterBeatPhase::Establish;
    next.events.push_back({
        EnemyEncounterPacingEventKind::BeatStarted,
        definition.editorGuid,
        definition.encounterId,
        EnemyEncounterBeatPhase::Dormant,
        EnemyEncounterBeatPhase::Establish,
        "encounter Beat triggered at authored rail distance"});
}

void EnemyEncounterPacingDirector::EnterPhase(
    EnemyEncounterBeatPhase phase,
    EnemyEncounterPacingFrame& next,
    const char* reason) {
    const EnemyEncounterBeatPhase previous = next.phase;
    next.phase = phase;
    next.phaseElapsedSeconds = 0.0f;
    resolveStallReported_ = false;
    next.attackWindowOpen = phase == EnemyEncounterBeatPhase::Attack;
    next.events.push_back({
        EnemyEncounterPacingEventKind::PhaseChanged,
        next.activeBeatGuid,
        next.encounterId,
        previous,
        phase,
        reason != nullptr ? reason : "phase changed"});
}

void EnemyEncounterPacingDirector::RestoreCoordinator(
    CourseSpawnRuntime* runtime) {
    if (runtime != nullptr && hasCoordinatorBaseline_) {
        (void)runtime->EnemyAttacks().Configure(coordinatorBaseline_);
    }
    hasCoordinatorBaseline_ = false;
}

const char* ToString(EnemyEncounterPacingEventKind kind) noexcept {
    switch (kind) {
    case EnemyEncounterPacingEventKind::BeatStarted: return "BeatStarted";
    case EnemyEncounterPacingEventKind::PhaseChanged: return "PhaseChanged";
    case EnemyEncounterPacingEventKind::FormationExitRequested:
        return "FormationExitRequested";
    case EnemyEncounterPacingEventKind::BeatCompleted: return "BeatCompleted";
    case EnemyEncounterPacingEventKind::ResolveStalled: return "ResolveStalled";
    }
    return "PhaseChanged";
}
