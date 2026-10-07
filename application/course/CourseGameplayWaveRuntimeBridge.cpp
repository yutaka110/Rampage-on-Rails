#include "CourseGameplayWaveRuntimeBridge.h"
#include "GameplaySettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

bool CourseGameplayWaveRuntimeSettings::Validate(std::string* errorMessage) const {
    return gameplay::settings::Result(
        maximumStateTransitionsPerFrame > 0 && maximumStateTransitionsPerFrame <= 65536,
        errorMessage, "Wave transition budget must be between 1 and 65536.");
}

bool CourseGameplayWaveRuntimeBridge::Configure(
    const CourseGameplayWaveRuntimeSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    // 設定の変更で進行中のウェーブをリセットしない。
    settings_ = settings;
    return true;
}

bool CourseGameplayWaveRuntimeBridge::Bind(
    const CourseRuntimeProgramAsset* program,
    CourseSpawnRuntime* spawnRuntime,
    float startDistance,
    std::string* errorMessage) {
    if (program == nullptr || spawnRuntime == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Gameplay Wave bridge requires a ProgramAsset and SpawnRuntime.";
        }
        return false;
    }
    std::string validationError;
    if (!program->Validate(&validationError)) {
        if (errorMessage != nullptr) *errorMessage = validationError;
        return false;
    }
    RemoveOwnedActors();
    program_ = program;
    runtime_ = spawnRuntime;
    InitializeState(startDistance);
    Update({0.0f, startDistance, {}});
    stats_.message = "Gameplay Wave bridge bound to cooked ProgramAsset.";
    return true;
}

void CourseGameplayWaveRuntimeBridge::Unbind() {
    RemoveOwnedActors();
    program_ = nullptr;
    runtime_ = nullptr;
    wavePhases_.clear();
    actorPhases_.clear();
    waveActiveSeconds_.clear();
    signaledEventIds_.clear();
    forcedWaveIndices_.clear();
    events_.clear();
    playerDistance_ = 0.0f;
    stats_ = {};
}

void CourseGameplayWaveRuntimeBridge::Reset(float startDistance) {
    if (!IsBound()) {
        stats_ = {};
        return;
    }
    RemoveOwnedActors();
    InitializeState(startDistance);
    Update({0.0f, startDistance, {}});
}

void CourseGameplayWaveRuntimeBridge::Update(
    const CourseGameplayWaveFrameInput& input) {
    events_.clear();
    stats_.spawnedThisFrame = 0;
    stats_.completedThisFrame = 0;
    if (!IsBound()) {
        stats_ = {};
        stats_.message = "Gameplay Wave bridge is not bound.";
        return;
    }
    for (const std::string& eventId : input.signaledEventIds) SignalEvent(eventId);
    playerDistance_ = (std::clamp)(input.playerDistance, 0.0f, program_->railLength);
    ReconcileDefeatedActors();
    UpdateActiveWaves((std::max)(0.0f, input.deltaTime));
    UpdatePrewarmAndActivation();
    // Zero-member and event-completed forced chains are allowed, but bounded
    // so malformed content can never stall a gameplay frame.
    for (uint32_t iteration = 0;
         iteration < settings_.maximumStateTransitionsPerFrame;
         ++iteration) {
        const uint32_t beforeCompleted = stats_.completedThisFrame;
        const std::size_t beforeForced = forcedWaveIndices_.size();
        UpdateActiveWaves(0.0f);
        UpdatePrewarmAndActivation();
        if (beforeCompleted == stats_.completedThisFrame &&
            beforeForced == forcedWaveIndices_.size()) {
            break;
        }
    }
    RefreshStats();
}

bool CourseGameplayWaveRuntimeBridge::NotifyEnemyDefeated(
    std::string_view placementGuid) {
    if (!IsBound()) return false;
    const CourseRuntimeActorRecord* record = program_->FindActor(placementGuid);
    if (record == nullptr) return false;
    const std::size_t actorIndex = static_cast<std::size_t>(record - program_->actors.data());
    if (actorPhases_[actorIndex] == CourseGameplayActorPhase::Defeated ||
        actorPhases_[actorIndex] == CourseGameplayActorPhase::Retired) {
        return false;
    }
    actorPhases_[actorIndex] = CourseGameplayActorPhase::Defeated;
    for (const CourseEnemyActor& actor : runtime_->Enemies()) {
        if (actor.desc.sourcePlacementGuid == placementGuid) {
            runtime_->EnemyCombat().ForceDefeat(*runtime_, actor.actorId);
        }
    }
    runtime_->PruneDestroyedActors();
    PushEvent(CourseGameplayWaveEventType::ActorDefeated,
        std::string(placementGuid), "Cooked Wave Actor was defeated.");
    return true;
}

void CourseGameplayWaveRuntimeBridge::SignalEvent(std::string eventId) {
    if (!eventId.empty()) signaledEventIds_.insert(std::move(eventId));
}

CourseGameplayWaveCheckpoint
CourseGameplayWaveRuntimeBridge::CaptureCheckpoint() const {
    CourseGameplayWaveCheckpoint checkpoint{};
    if (!IsBound()) return checkpoint;
    checkpoint.programFingerprint = program_->sourceFingerprint;
    checkpoint.playerDistance = playerDistance_;
    checkpoint.wavePhases = wavePhases_;
    checkpoint.waveActiveSeconds = waveActiveSeconds_;
    checkpoint.signaledEventIds.assign(
        signaledEventIds_.begin(), signaledEventIds_.end());
    std::sort(checkpoint.signaledEventIds.begin(), checkpoint.signaledEventIds.end());
    for (std::size_t index = 0; index < actorPhases_.size(); ++index) {
        if (actorPhases_[index] == CourseGameplayActorPhase::Defeated ||
            actorPhases_[index] == CourseGameplayActorPhase::Retired) {
            checkpoint.defeatedPlacementGuids.push_back(
                program_->actors[index].placementGuid);
        }
    }
    return checkpoint;
}

bool CourseGameplayWaveRuntimeBridge::RestoreCheckpoint(
    const CourseGameplayWaveCheckpoint& checkpoint,
    std::string* errorMessage) {
    if (!IsBound() || checkpoint.programFingerprint != program_->sourceFingerprint ||
        checkpoint.wavePhases.size() != program_->waves.size() ||
        checkpoint.waveActiveSeconds.size() != program_->waves.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Gameplay Wave checkpoint does not match the bound ProgramAsset.";
        }
        return false;
    }
    RemoveOwnedActors();
    wavePhases_ = checkpoint.wavePhases;
    waveActiveSeconds_ = checkpoint.waveActiveSeconds;
    actorPhases_.assign(program_->actors.size(), CourseGameplayActorPhase::Dormant);
    signaledEventIds_.clear();
    signaledEventIds_.insert(
        checkpoint.signaledEventIds.begin(), checkpoint.signaledEventIds.end());
    forcedWaveIndices_.clear();
    events_.clear();
    playerDistance_ = (std::clamp)(checkpoint.playerDistance, 0.0f, program_->railLength);
    const std::unordered_set<std::string> defeated(
        checkpoint.defeatedPlacementGuids.begin(),
        checkpoint.defeatedPlacementGuids.end());
    for (std::size_t index = 0; index < program_->actors.size(); ++index) {
        const CourseRuntimeActorRecord& actor = program_->actors[index];
        if (defeated.contains(actor.placementGuid)) {
            actorPhases_[index] = CourseGameplayActorPhase::Defeated;
            continue;
        }
        const CourseGameplayWavePhase wavePhase = wavePhases_[actor.waveIndex];
        if (wavePhase == CourseGameplayWavePhase::Active) {
            SpawnActor(static_cast<uint32_t>(index));
        } else if (wavePhase == CourseGameplayWavePhase::Prewarming ||
                   wavePhase == CourseGameplayWavePhase::Blocked) {
            actorPhases_[index] = CourseGameplayActorPhase::Prewarmed;
        } else if (wavePhase == CourseGameplayWavePhase::Completed) {
            CourseEnemyActor candidate{};
            candidate.desc = actor.actor;
            candidate.behaviorDefinition = actor.actor.behaviorDefinition;
            if (candidate.HoldsCombatPositionUntilResolved()) {
                SpawnActor(static_cast<uint32_t>(index));
            } else {
                actorPhases_[index] = CourseGameplayActorPhase::Retired;
            }
        }
    }
    RefreshStats();
    stats_.message = "Gameplay Wave checkpoint restored.";
    return true;
}

void CourseGameplayWaveRuntimeBridge::InitializeState(float startDistance) {
    wavePhases_.clear();
    wavePhases_.reserve(program_->waves.size());
    for (const CourseRuntimeWaveNode& wave : program_->waves) {
        wavePhases_.push_back(wave.enabled
            ? CourseGameplayWavePhase::Pending
            : CourseGameplayWavePhase::Disabled);
    }
    actorPhases_.assign(program_->actors.size(), CourseGameplayActorPhase::Dormant);
    waveActiveSeconds_.assign(program_->waves.size(), 0.0f);
    signaledEventIds_.clear();
    forcedWaveIndices_.clear();
    events_.clear();
    playerDistance_ = (std::clamp)(startDistance, 0.0f, program_->railLength);
    stats_ = {};
    stats_.bound = true;
    stats_.programFingerprint = program_->sourceFingerprint;
    RefreshStats();
}

void CourseGameplayWaveRuntimeBridge::ReconcileDefeatedActors() {
    for (std::size_t index = 0; index < actorPhases_.size(); ++index) {
        if (actorPhases_[index] != CourseGameplayActorPhase::Active) continue;
        const CourseRuntimeActorRecord& record = program_->actors[index];
        const auto found = std::find_if(
            runtime_->Enemies().begin(), runtime_->Enemies().end(),
            [&](const CourseEnemyActor& actor) {
                return actor.desc.sourcePlacementGuid == record.placementGuid;
            });
        const bool defeated = found != runtime_->Enemies().end() &&
            (found->desc.hitPoints <= 0.0f ||
             (found->combatState.initialized &&
              (found->combatState.phase == EnemyCombatPhase::Dying ||
               found->combatState.phase == EnemyCombatPhase::Retired)));
        const bool missing = found == runtime_->Enemies().end() &&
            settings_.detectMissingActiveActorsAsDefeated;
        if (defeated || missing) {
            actorPhases_[index] = CourseGameplayActorPhase::Defeated;
            PushEvent(CourseGameplayWaveEventType::ActorDefeated,
                record.placementGuid,
                defeated ? "Cooked Wave Actor reached zero HP."
                         : "Cooked Wave Actor left the SpawnRuntime.");
        }
    }
}

void CourseGameplayWaveRuntimeBridge::UpdateActiveWaves(float deltaTime) {
    std::vector<std::size_t> completed;
    for (std::size_t index = 0; index < wavePhases_.size(); ++index) {
        if (wavePhases_[index] != CourseGameplayWavePhase::Active) continue;
        const CourseRuntimeWaveNode& wave = program_->waves[index];
        waveActiveSeconds_[index] += deltaTime;
        bool isComplete = false;
        switch (wave.completionCondition) {
        case CourseWaveCompletionCondition::AllEnemiesDefeated:
            isComplete = std::all_of(
                wave.actorIndices.begin(), wave.actorIndices.end(),
                [&](uint32_t actorIndex) {
                    return actorPhases_[actorIndex] == CourseGameplayActorPhase::Defeated ||
                        actorPhases_[actorIndex] == CourseGameplayActorPhase::Retired;
                });
            break;
        case CourseWaveCompletionCondition::Timeout:
            isComplete = waveActiveSeconds_[index] >= wave.timeoutSeconds;
            break;
        case CourseWaveCompletionCondition::ReachRailDistance: {
            float completionDistance = wave.triggerRailDistance +
                (std::max)(wave.prewarmDistance, 1.0f);
            if (wave.nextWaveIndex >= 0) {
                const float nextDistance =
                    program_->waves[static_cast<std::size_t>(wave.nextWaveIndex)]
                        .triggerRailDistance;
                if (nextDistance > wave.triggerRailDistance) {
                    completionDistance = nextDistance;
                }
            }
            isComplete = playerDistance_ >= completionDistance;
            break;
        }
        case CourseWaveCompletionCondition::ScriptedEvent:
            isComplete = !wave.triggerEventId.empty() &&
                signaledEventIds_.contains(wave.triggerEventId);
            break;
        }
        if (isComplete) completed.push_back(index);
    }
    for (const std::size_t index : completed) CompleteWave(index);
}

void CourseGameplayWaveRuntimeBridge::UpdatePrewarmAndActivation() {
    for (std::size_t index = 0; index < wavePhases_.size(); ++index) {
        CourseGameplayWavePhase& phase = wavePhases_[index];
        if (phase == CourseGameplayWavePhase::Disabled ||
            phase == CourseGameplayWavePhase::Active ||
            phase == CourseGameplayWavePhase::Completed) {
            continue;
        }
        const CourseRuntimeWaveNode& wave = program_->waves[index];
        const bool forced = forcedWaveIndices_.contains(index);
        if (phase == CourseGameplayWavePhase::Pending &&
            (forced || playerDistance_ >=
                (std::max)(0.0f, wave.triggerRailDistance - wave.prewarmDistance))) {
            PrewarmWave(index);
        }
        if ((forced || playerDistance_ >= wave.triggerRailDistance) &&
            phase != CourseGameplayWavePhase::Active) {
            if (CanActivateWave(index)) {
                ActivateWave(index);
                forcedWaveIndices_.erase(index);
            } else {
                phase = CourseGameplayWavePhase::Blocked;
            }
        }
    }
}

bool CourseGameplayWaveRuntimeBridge::CanActivateWave(std::size_t index) const {
    const CourseRuntimeWaveNode& wave = program_->waves[index];
    if (wave.executionPolicy == CourseWaveExecutionPolicy::Parallel) return true;
    if (wave.executionPolicy == CourseWaveExecutionPolicy::Sequential) {
        for (std::size_t previous = index; previous > 0; --previous) {
            if (wavePhases_[previous - 1] == CourseGameplayWavePhase::Disabled) continue;
            return wavePhases_[previous - 1] == CourseGameplayWavePhase::Completed;
        }
        return true;
    }
    return std::none_of(
        wavePhases_.begin(), wavePhases_.end(),
        [](CourseGameplayWavePhase phase) {
            return phase == CourseGameplayWavePhase::Active;
        });
}

void CourseGameplayWaveRuntimeBridge::PrewarmWave(std::size_t index) {
    if (wavePhases_[index] != CourseGameplayWavePhase::Pending) return;
    wavePhases_[index] = CourseGameplayWavePhase::Prewarming;
    for (const uint32_t actorIndex : program_->waves[index].actorIndices) {
        if (actorPhases_[actorIndex] == CourseGameplayActorPhase::Dormant) {
            actorPhases_[actorIndex] = CourseGameplayActorPhase::Prewarmed;
        }
    }
    PushEvent(CourseGameplayWaveEventType::WavePrewarmed,
        program_->waves[index].waveGuid,
        program_->waves[index].displayName + " entered gameplay prewarm range.");
}

void CourseGameplayWaveRuntimeBridge::ActivateWave(std::size_t index) {
    if (wavePhases_[index] == CourseGameplayWavePhase::Active ||
        wavePhases_[index] == CourseGameplayWavePhase::Completed ||
        wavePhases_[index] == CourseGameplayWavePhase::Disabled) {
        return;
    }
    wavePhases_[index] = CourseGameplayWavePhase::Active;
    waveActiveSeconds_[index] = 0.0f;
    for (const uint32_t actorIndex : program_->waves[index].actorIndices) {
        if (actorPhases_[actorIndex] != CourseGameplayActorPhase::Defeated &&
            actorPhases_[actorIndex] != CourseGameplayActorPhase::Retired) {
            SpawnActor(actorIndex);
        }
    }
    PushEvent(CourseGameplayWaveEventType::WaveActivated,
        program_->waves[index].waveGuid,
        program_->waves[index].displayName + " activated in gameplay.");
}

void CourseGameplayWaveRuntimeBridge::CompleteWave(std::size_t index) {
    if (wavePhases_[index] != CourseGameplayWavePhase::Active) return;
    wavePhases_[index] = CourseGameplayWavePhase::Completed;
    ++stats_.completedThisFrame;
    if (settings_.retireActorsWhenWaveCompletes) RemoveActorsForWave(index);
    const int32_t next = program_->waves[index].nextWaveIndex;
    if (next >= 0) forcedWaveIndices_.insert(static_cast<std::size_t>(next));
    PushEvent(CourseGameplayWaveEventType::WaveCompleted,
        program_->waves[index].waveGuid,
        program_->waves[index].displayName + " completed in gameplay.");
}

void CourseGameplayWaveRuntimeBridge::SpawnActor(uint32_t actorIndex) {
    if (actorIndex >= program_->actors.size()) return;
    const CourseRuntimeActorRecord& record = program_->actors[actorIndex];
    if (!RuntimeContains(record.placementGuid)) {
        CourseEnemyActorDesc desc = record.actor;
        desc.previewOnly = false;
        desc.suppressFire = false;
        runtime_->SpawnEnemyActor(std::move(desc));
        ++stats_.spawnedThisFrame;
        PushEvent(CourseGameplayWaveEventType::ActorSpawned,
            record.placementGuid, record.actor.actorAssetId + " spawned from cooked Wave.");
    }
    actorPhases_[actorIndex] = CourseGameplayActorPhase::Active;
}

void CourseGameplayWaveRuntimeBridge::RemoveActorsForWave(std::size_t waveIndex) {
    std::vector<uint32_t> retiredActorIds;
    for (const CourseEnemyActor& actor : runtime_->Enemies()) {
        const CourseRuntimeActorRecord* record = program_->FindActor(actor.desc.sourcePlacementGuid);
        if (record != nullptr && record->waveIndex == waveIndex) retiredActorIds.push_back(actor.actorId);
    }
    runtime_->RetireEnemies(retiredActorIds);
    for (const uint32_t actorIndex : program_->waves[waveIndex].actorIndices) {
        const auto& record = program_->actors[actorIndex];
        const auto retained = std::find_if(runtime_->Enemies().begin(), runtime_->Enemies().end(), [&](const auto& actor) {
            return actor.desc.sourcePlacementGuid == record.placementGuid &&
                actor.HoldsCombatPositionUntilResolved() && actor.desc.hitPoints > 0.0f;
        });
        if (retained != runtime_->Enemies().end()) continue;
        if (actorPhases_[actorIndex] != CourseGameplayActorPhase::Defeated) {
            actorPhases_[actorIndex] = CourseGameplayActorPhase::Retired;
        }
    }
}

void CourseGameplayWaveRuntimeBridge::RemoveOwnedActors() {
    if (program_ == nullptr || runtime_ == nullptr) return;
    std::vector<uint32_t> retiredActorIds;
    for (const CourseEnemyActor& actor : runtime_->Enemies()) {
        if (program_->FindActor(actor.desc.sourcePlacementGuid) != nullptr) retiredActorIds.push_back(actor.actorId);
    }
    runtime_->RetireEnemies(retiredActorIds, false);
}

bool CourseGameplayWaveRuntimeBridge::RuntimeContains(
    std::string_view placementGuid) const {
    return std::any_of(runtime_->Enemies().begin(), runtime_->Enemies().end(),
        [&](const CourseEnemyActor& actor) {
            return actor.desc.sourcePlacementGuid == placementGuid;
        });
}

void CourseGameplayWaveRuntimeBridge::RefreshStats() {
    const uint32_t spawned = stats_.spawnedThisFrame;
    const uint32_t completed = stats_.completedThisFrame;
    stats_ = {};
    stats_.bound = IsBound();
    stats_.programFingerprint = program_ != nullptr ? program_->sourceFingerprint : 0;
    stats_.playerDistance = playerDistance_;
    stats_.spawnedThisFrame = spawned;
    stats_.completedThisFrame = completed;
    for (const CourseGameplayWavePhase phase : wavePhases_) {
        switch (phase) {
        case CourseGameplayWavePhase::Pending: ++stats_.pendingWaves; break;
        case CourseGameplayWavePhase::Prewarming:
        case CourseGameplayWavePhase::Blocked: ++stats_.prewarmingWaves; break;
        case CourseGameplayWavePhase::Active: ++stats_.activeWaves; break;
        case CourseGameplayWavePhase::Completed: ++stats_.completedWaves; break;
        case CourseGameplayWavePhase::Disabled: break;
        }
    }
    for (const CourseGameplayActorPhase phase : actorPhases_) {
        if (phase == CourseGameplayActorPhase::Active) ++stats_.activeActors;
        if (phase == CourseGameplayActorPhase::Defeated) ++stats_.defeatedActors;
    }
    stats_.message = "Gameplay Wave runtime synchronized from cooked ProgramAsset.";
}

void CourseGameplayWaveRuntimeBridge::PushEvent(
    CourseGameplayWaveEventType type,
    std::string subjectGuid,
    std::string message) {
    events_.push_back({type, std::move(subjectGuid), std::move(message)});
}

const char* ToString(CourseGameplayWavePhase phase) {
    switch (phase) {
    case CourseGameplayWavePhase::Disabled: return "Disabled";
    case CourseGameplayWavePhase::Pending: return "Pending";
    case CourseGameplayWavePhase::Prewarming: return "Prewarming";
    case CourseGameplayWavePhase::Blocked: return "Blocked";
    case CourseGameplayWavePhase::Active: return "Active";
    case CourseGameplayWavePhase::Completed: return "Completed";
    }
    return "Unknown";
}

const char* ToString(CourseGameplayActorPhase phase) {
    switch (phase) {
    case CourseGameplayActorPhase::Dormant: return "Dormant";
    case CourseGameplayActorPhase::Prewarmed: return "Prewarmed";
    case CourseGameplayActorPhase::Active: return "Active";
    case CourseGameplayActorPhase::Defeated: return "Defeated";
    case CourseGameplayActorPhase::Retired: return "Retired";
    }
    return "Unknown";
}

const char* ToString(CourseGameplayWaveEventType type) {
    switch (type) {
    case CourseGameplayWaveEventType::WavePrewarmed: return "WavePrewarmed";
    case CourseGameplayWaveEventType::WaveActivated: return "WaveActivated";
    case CourseGameplayWaveEventType::WaveCompleted: return "WaveCompleted";
    case CourseGameplayWaveEventType::ActorSpawned: return "ActorSpawned";
    case CourseGameplayWaveEventType::ActorDefeated: return "ActorDefeated";
    }
    return "Unknown";
}
