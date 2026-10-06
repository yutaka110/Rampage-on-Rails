#include "CoursePreviewSimulationSystem.h"

#include "CourseEnemyAuthoringModel.h"
#include "CourseWaveAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace editor {
namespace {

constexpr std::size_t kInvalidIndex = (std::numeric_limits<std::size_t>::max)();
constexpr std::size_t kMaximumEventHistory = 128;

bool IsWaveDone(CoursePreviewWavePhase phase) {
    return phase == CoursePreviewWavePhase::Completed ||
        phase == CoursePreviewWavePhase::Disabled;
}

uint32_t WaveColor(CoursePreviewWavePhase phase) {
    switch (phase) {
    case CoursePreviewWavePhase::Prewarming: return 0xff55c8ffu;
    case CoursePreviewWavePhase::Blocked: return 0xff6c70ffu;
    case CoursePreviewWavePhase::Active: return 0xff4f5fffu;
    case CoursePreviewWavePhase::Completed: return 0xff67d987u;
    case CoursePreviewWavePhase::Disabled: return 0xff707070u;
    case CoursePreviewWavePhase::Pending: return 0xff9a9a9au;
    }
    return 0xffffffffu;
}

uint32_t EnemyColor(CoursePreviewEnemyPhase phase) {
    switch (phase) {
    case CoursePreviewEnemyPhase::Prewarmed: return 0xff54c8ffu;
    case CoursePreviewEnemyPhase::Active: return 0xff4f5fffu;
    case CoursePreviewEnemyPhase::Defeated: return 0xff67d987u;
    case CoursePreviewEnemyPhase::Retired: return 0xff8c9f8fu;
    case CoursePreviewEnemyPhase::Disabled: return 0xff606060u;
    case CoursePreviewEnemyPhase::Dormant: return 0xff909090u;
    }
    return 0xffffffffu;
}

} // namespace

bool CoursePreviewSimulationSettings::Validate(std::string* errorMessage) const {
    const bool valid = std::isfinite(fixedStepSeconds) && fixedStepSeconds >= 1.0f / 240.0f && fixedStepSeconds <= 0.25f &&
        std::isfinite(travelSpeed) && travelSpeed >= 0.0f && travelSpeed <= 100000.0f &&
        std::isfinite(playbackRate) && playbackRate >= 0.05f && playbackRate <= 8.0f &&
        std::isfinite(automaticEnemyDefeatSeconds) && automaticEnemyDefeatSeconds >= 0.0f && automaticEnemyDefeatSeconds <= 100000.0f &&
        maximumSubsteps >= 1 && maximumSubsteps <= 64;
    if (errorMessage != nullptr) *errorMessage = valid ? "" : "Preview settings require finite values, step 1/240..0.25s, rate 0.05..8 and 1..64 substeps.";
    return valid;
}

bool CoursePreviewSimulationSystem::Configure(const CoursePreviewSimulationSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    // Preserve playback/snapshot/actors; only bound the unconsumed time budget.
    accumulator_ = (std::min)(accumulator_, settings_.fixedStepSeconds * settings_.maximumSubsteps);
    return true;
}

bool CoursePreviewSimulationSystem::BeginPreview(
    const CourseAsset& source,
    float startDistance,
    std::string* errorMessage) {
    if (!std::isfinite(startDistance)) {
        if (errorMessage != nullptr) *errorMessage = "Preview start distance must be finite.";
        return false;
    }
    CourseWaveAuthoringModel waveModel(source);
    CourseEnemyAuthoringModel enemyModel(source);
    if (!source.IsValid() || !waveModel.IsValid() || !enemyModel.IsValid()) {
        frame_ = {};
        frame_.playback = CoursePreviewPlaybackState::Error;
        frame_.message = !waveModel.IsValid()
            ? waveModel.ValidationError()
            : !enemyModel.IsValid()
            ? enemyModel.ValidationError()
            : "Course preview requires at least two valid Rail control points.";
        if (errorMessage != nullptr) *errorMessage = frame_.message;
        return false;
    }

    snapshot_ = source;
    snapshot_.ApplyToRailPath(railPath_);
    if (railPath_.Length() <= 0.0f) {
        frame_ = {};
        frame_.playback = CoursePreviewPlaybackState::Error;
        frame_.message = "Course preview RailPath has zero length.";
        if (errorMessage != nullptr) *errorMessage = frame_.message;
        return false;
    }

    hasSnapshot_ = true;
    requestedStartDistance_ = (std::clamp)(startDistance, 0.0f, railPath_.Length());
    InitializeRuntimeState();
    if (!RebuildAtDistance(requestedStartDistance_, errorMessage)) return false;
    frame_.playback = CoursePreviewPlaybackState::Playing;
    frame_.message = "Course preview playing from an immutable authoring snapshot.";
    PushEvent(
        CoursePreviewEventType::PreviewStarted,
        snapshot_.name,
        "Preview started at " + std::to_string(frame_.distance) + "m.");
    RefreshFrame();
    return true;
}

void CoursePreviewSimulationSystem::Play() {
    if (!hasSnapshot_ || frame_.playback == CoursePreviewPlaybackState::Error) return;
    if (frame_.playback == CoursePreviewPlaybackState::Completed) {
        Restart(true, nullptr);
        return;
    }
    frame_.playback = CoursePreviewPlaybackState::Playing;
    frame_.message = "Course preview playing.";
}

void CoursePreviewSimulationSystem::Pause() {
    if (!IsActive()) return;
    frame_.playback = CoursePreviewPlaybackState::Paused;
    frame_.message = "Course preview paused.";
}

void CoursePreviewSimulationSystem::Stop() {
    if (hasSnapshot_ && frame_.playback != CoursePreviewPlaybackState::Stopped) {
        PushEvent(
            CoursePreviewEventType::PreviewStopped,
            snapshot_.name,
            "Preview stopped.");
    }
    frame_.playback = CoursePreviewPlaybackState::Stopped;
    frame_.message = "Course preview stopped. Authoring is unlocked.";
    accumulator_ = 0.0f;
    ++frame_.simulationRevision;
}

bool CoursePreviewSimulationSystem::Restart(
    bool playImmediately,
    std::string* errorMessage) {
    if (!hasSnapshot_) {
        if (errorMessage != nullptr) *errorMessage = "Course preview snapshot is unavailable.";
        return false;
    }
    InitializeRuntimeState();
    if (!RebuildAtDistance(requestedStartDistance_, errorMessage)) return false;
    frame_.playback = playImmediately
        ? CoursePreviewPlaybackState::Playing
        : CoursePreviewPlaybackState::Paused;
    frame_.message = playImmediately
        ? "Course preview restarted."
        : "Course preview reset and paused.";
    PushEvent(
        CoursePreviewEventType::PreviewStarted,
        snapshot_.name,
        "Preview restarted.");
    RefreshFrame();
    return true;
}

bool CoursePreviewSimulationSystem::Seek(
    float distance,
    std::string* errorMessage) {
    if (!hasSnapshot_) {
        if (errorMessage != nullptr) *errorMessage = "Course preview snapshot is unavailable.";
        return false;
    }
    if (!std::isfinite(distance)) {
        if (errorMessage != nullptr) *errorMessage = "Preview seek distance must be finite.";
        return false;
    }
    const CoursePreviewPlaybackState previous = frame_.playback;
    if (!RebuildAtDistance(distance, errorMessage)) return false;
    frame_.playback = previous == CoursePreviewPlaybackState::Playing
        ? CoursePreviewPlaybackState::Playing
        : CoursePreviewPlaybackState::Paused;
    frame_.message = "Course preview reconstructed deterministically at the requested distance.";
    RefreshFrame();
    return true;
}

bool CoursePreviewSimulationSystem::Step(std::string* errorMessage) {
    if (!hasSnapshot_) {
        if (errorMessage != nullptr) *errorMessage = "Course preview snapshot is unavailable.";
        return false;
    }
    if (frame_.playback == CoursePreviewPlaybackState::Completed) {
        if (!Restart(false, errorMessage)) return false;
    }
    frame_.playback = CoursePreviewPlaybackState::Paused;
    AdvanceFixed((std::max)(0.0001f, settings_.fixedStepSeconds), true);
    if (frame_.playback != CoursePreviewPlaybackState::Completed) {
        frame_.playback = CoursePreviewPlaybackState::Paused;
    }
    frame_.message = "Course preview advanced by one fixed simulation step.";
    RefreshFrame();
    return true;
}

void CoursePreviewSimulationSystem::Tick(float deltaTime) {
    if (!IsPlaying() || !hasSnapshot_ || !std::isfinite(deltaTime) || deltaTime < 0.0f) return;
    const float fixedStep = (std::clamp)(settings_.fixedStepSeconds, 1.0f / 240.0f, 0.25f);
    const float playbackRate = (std::clamp)(settings_.playbackRate, 0.05f, 8.0f);
    accumulator_ += (std::clamp)(deltaTime, 0.0f, 0.25f) * playbackRate;
    const uint32_t maximumSubsteps = (std::clamp)(settings_.maximumSubsteps, 1u, 64u);
    uint32_t steps = 0;
    while (accumulator_ + 0.000001f >= fixedStep && steps < maximumSubsteps && IsPlaying()) {
        AdvanceFixed(fixedStep, true);
        accumulator_ -= fixedStep;
        ++steps;
    }
    if (steps == maximumSubsteps && accumulator_ >= fixedStep) {
        accumulator_ = std::fmod(accumulator_, fixedStep);
    }
    RefreshFrame();
}

bool CoursePreviewSimulationSystem::MarkEnemyDefeated(
    std::string_view placementGuid,
    bool defeated) {
    const std::size_t index = FindEnemyIndex(placementGuid);
    if (index == kInvalidIndex || enemies_[index].phase == CoursePreviewEnemyPhase::Disabled) {
        return false;
    }
    enemies_[index].phase = defeated
        ? CoursePreviewEnemyPhase::Defeated
        : CoursePreviewEnemyPhase::Active;
    if (defeated) {
        PushEvent(
            CoursePreviewEventType::EnemyDefeated,
            enemies_[index].placementGuid,
            enemies_[index].actorAssetId + " defeated.");
    }
    UpdateActiveState(0.0f, true);
    UpdatePrewarmAndActivation(true);
    RefreshFrame();
    return true;
}

uint32_t CoursePreviewSimulationSystem::DefeatActiveEnemies() {
    uint32_t defeated = 0;
    for (CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.phase != CoursePreviewEnemyPhase::Active) continue;
        enemy.phase = CoursePreviewEnemyPhase::Defeated;
        ++defeated;
        PushEvent(
            CoursePreviewEventType::EnemyDefeated,
            enemy.placementGuid,
            enemy.actorAssetId + " defeated by preview control.");
    }
    UpdateActiveState(0.0f, true);
    UpdatePrewarmAndActivation(true);
    RefreshFrame();
    return defeated;
}

void CoursePreviewSimulationSystem::SignalEvent(std::string eventId) {
    if (eventId.empty()) return;
    signaledEvents_.insert(eventId);
    PushEvent(
        CoursePreviewEventType::TriggerEvent,
        eventId,
        "Preview signal received: " + eventId);
    UpdateActiveState(0.0f, true);
    UpdatePrewarmAndActivation(true);
    RefreshFrame();
}

void CoursePreviewSimulationSystem::InitializeRuntimeState() {
    waves_.clear();
    enemies_.clear();
    events_.clear();
    signaledEvents_.clear();
    forcedWaveActivations_.clear();
    accumulator_ = 0.0f;

    std::vector<const CourseWaveDefinition*> sorted;
    sorted.reserve(snapshot_.waveDefinitions.size());
    for (const CourseWaveDefinition& wave : snapshot_.waveDefinitions) sorted.push_back(&wave);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return a->triggerRailDistance < b->triggerRailDistance;
    });
    for (const CourseWaveDefinition* wave : sorted) {
        CoursePreviewWaveState state{};
        state.waveGuid = wave->editorGuid;
        state.displayName = wave->displayName;
        state.triggerDistance = wave->triggerRailDistance;
        state.phase = wave->enabled
            ? CoursePreviewWavePhase::Pending
            : CoursePreviewWavePhase::Disabled;
        state.memberCount = static_cast<uint32_t>(std::count_if(
            snapshot_.enemyPlacements.begin(), snapshot_.enemyPlacements.end(),
            [&](const CourseEnemyPlacement& placement) {
                return placement.waveGroupGuid == wave->editorGuid && placement.enabled;
            }));
        waves_.push_back(std::move(state));
    }
    for (const CourseEnemyPlacement& placement : snapshot_.enemyPlacements) {
        CoursePreviewEnemyState state{};
        state.placementGuid = placement.editorGuid;
        state.actorAssetId = placement.actorAssetId;
        state.waveGuid = placement.waveGroupGuid;
        state.phase = placement.enabled
            ? CoursePreviewEnemyPhase::Dormant
            : CoursePreviewEnemyPhase::Disabled;
        enemies_.push_back(std::move(state));
    }
    frame_ = {};
    frame_.playback = CoursePreviewPlaybackState::Paused;
    frame_.railLength = railPath_.Length();
    frame_.simulationRevision = nextRevision_++;
}

bool CoursePreviewSimulationSystem::RebuildAtDistance(
    float distance,
    std::string* errorMessage) {
    if (!hasSnapshot_ || railPath_.Length() <= 0.0f) {
        if (errorMessage != nullptr) *errorMessage = "Course preview snapshot is invalid.";
        return false;
    }
    InitializeRuntimeState();
    const float target = (std::clamp)(distance, 0.0f, railPath_.Length());
    UpdatePrewarmAndActivation(false);
    constexpr uint32_t kMaximumRebuildSteps = 120000;
    uint32_t steps = 0;
    while (frame_.distance + 0.0001f < target && steps < kMaximumRebuildSteps) {
        const float speed = (std::max)(0.01f, ResolveTravelSpeed());
        const float remainingSeconds = (target - frame_.distance) / speed;
        const float step = (std::min)(
            (std::max)(0.0001f, settings_.fixedStepSeconds), remainingSeconds);
        AdvanceFixed(step, false);
        ++steps;
    }
    if (frame_.distance + 0.001f < target) {
        frame_.distance = target;
        UpdatePrewarmAndActivation(false);
    }
    events_.clear();
    frame_.playback = CoursePreviewPlaybackState::Paused;
    RefreshFrame();
    return true;
}

void CoursePreviewSimulationSystem::AdvanceFixed(float deltaTime, bool emitEvents) {
    if (!hasSnapshot_ || frame_.playback == CoursePreviewPlaybackState::Completed) return;
    const float dt = (std::max)(0.0f, deltaTime);
    UpdateActiveState(dt, emitEvents);
    frame_.elapsedSeconds += dt;
    frame_.currentSpeed = ResolveTravelSpeed();
    frame_.distance = (std::min)(
        railPath_.Length(), frame_.distance + frame_.currentSpeed * dt);
    UpdatePrewarmAndActivation(emitEvents);
    UpdateActiveState(0.0f, emitEvents);

    if (frame_.distance + 0.0001f >= railPath_.Length()) {
        if (settings_.loop) {
            InitializeRuntimeState();
            frame_.playback = CoursePreviewPlaybackState::Playing;
            UpdatePrewarmAndActivation(emitEvents);
        } else {
            frame_.playback = CoursePreviewPlaybackState::Completed;
            frame_.message = "Course preview reached the end of the RailPath.";
            if (emitEvents) {
                PushEvent(
                    CoursePreviewEventType::PreviewCompleted,
                    snapshot_.name,
                    frame_.message);
            }
        }
    }
    ++frame_.simulationRevision;
}

void CoursePreviewSimulationSystem::UpdatePrewarmAndActivation(bool emitEvents) {
    for (std::size_t index = 0; index < waves_.size(); ++index) {
        CoursePreviewWaveState& state = waves_[index];
        if (state.phase == CoursePreviewWavePhase::Disabled ||
            state.phase == CoursePreviewWavePhase::Active ||
            state.phase == CoursePreviewWavePhase::Completed) {
            continue;
        }
        const CourseWaveDefinition* definition = nullptr;
        for (const CourseWaveDefinition& wave : snapshot_.waveDefinitions) {
            if (wave.editorGuid == state.waveGuid) {
                definition = &wave;
                break;
            }
        }
        if (definition == nullptr) continue;
        const bool forced = forcedWaveActivations_.contains(state.waveGuid);
        if (state.phase == CoursePreviewWavePhase::Pending &&
            (forced || frame_.distance >=
                (std::max)(0.0f, definition->triggerRailDistance - definition->prewarmDistance))) {
            PrewarmWave(index, emitEvents);
        }
        if ((forced || frame_.distance >= definition->triggerRailDistance) &&
            state.phase != CoursePreviewWavePhase::Active) {
            if (CanActivateWave(index)) {
                ActivateWave(index, emitEvents);
                forcedWaveActivations_.erase(state.waveGuid);
            } else {
                state.phase = CoursePreviewWavePhase::Blocked;
            }
        }
    }
}

void CoursePreviewSimulationSystem::UpdateActiveState(
    float deltaTime,
    bool emitEvents) {
    for (CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.phase != CoursePreviewEnemyPhase::Active) continue;
        enemy.activeSeconds += deltaTime;
        if (settings_.automaticallyDefeatEnemies &&
            enemy.activeSeconds >=
                (std::max)(0.0f, settings_.automaticEnemyDefeatSeconds)) {
            enemy.phase = CoursePreviewEnemyPhase::Defeated;
            if (emitEvents) {
                PushEvent(
                    CoursePreviewEventType::EnemyDefeated,
                    enemy.placementGuid,
                    enemy.actorAssetId + " automatically resolved.");
            }
        }
    }

    std::vector<std::size_t> completed;
    for (std::size_t index = 0; index < waves_.size(); ++index) {
        CoursePreviewWaveState& state = waves_[index];
        if (state.phase != CoursePreviewWavePhase::Active) continue;
        state.activeSeconds += deltaTime;
        const CourseWaveDefinition* definition = nullptr;
        for (const CourseWaveDefinition& wave : snapshot_.waveDefinitions) {
            if (wave.editorGuid == state.waveGuid) {
                definition = &wave;
                break;
            }
        }
        if (definition == nullptr) continue;
        state.defeatedMemberCount = static_cast<uint32_t>(std::count_if(
            enemies_.begin(), enemies_.end(), [&](const CoursePreviewEnemyState& enemy) {
                return enemy.waveGuid == state.waveGuid &&
                    (enemy.phase == CoursePreviewEnemyPhase::Defeated ||
                     enemy.phase == CoursePreviewEnemyPhase::Retired);
            }));

        bool isComplete = false;
        switch (definition->completionCondition) {
        case CourseWaveCompletionCondition::AllEnemiesDefeated:
            isComplete = state.defeatedMemberCount >= state.memberCount;
            break;
        case CourseWaveCompletionCondition::Timeout:
            isComplete = state.activeSeconds >= (std::max)(0.0f, definition->timeoutSeconds);
            break;
        case CourseWaveCompletionCondition::ReachRailDistance: {
            float completionDistance = definition->triggerRailDistance +
                (std::max)(definition->prewarmDistance, ResolveTravelSpeed());
            const std::size_t nextIndex = FindWaveIndex(definition->nextWaveGuid);
            if (nextIndex != kInvalidIndex &&
                waves_[nextIndex].triggerDistance > definition->triggerRailDistance) {
                completionDistance = waves_[nextIndex].triggerDistance;
            }
            isComplete = frame_.distance >= completionDistance;
            break;
        }
        case CourseWaveCompletionCondition::ScriptedEvent:
            isComplete = !definition->triggerEventId.empty() &&
                signaledEvents_.contains(definition->triggerEventId);
            break;
        }
        if (isComplete) completed.push_back(index);
    }
    for (std::size_t index : completed) CompleteWave(index, emitEvents);
}

bool CoursePreviewSimulationSystem::CanActivateWave(std::size_t index) const {
    if (index >= waves_.size()) return false;
    const CourseWaveDefinition* definition = nullptr;
    for (const CourseWaveDefinition& wave : snapshot_.waveDefinitions) {
        if (wave.editorGuid == waves_[index].waveGuid) {
            definition = &wave;
            break;
        }
    }
    if (definition == nullptr) return false;
    if (definition->executionPolicy == CourseWaveExecutionPolicy::Parallel) return true;
    if (definition->executionPolicy == CourseWaveExecutionPolicy::Sequential) {
        for (std::size_t previous = index; previous > 0; --previous) {
            const CoursePreviewWaveState& candidate = waves_[previous - 1];
            if (candidate.phase == CoursePreviewWavePhase::Disabled) continue;
            return IsWaveDone(candidate.phase);
        }
        return true;
    }
    return std::none_of(waves_.begin(), waves_.end(), [&](const auto& candidate) {
        return candidate.waveGuid != waves_[index].waveGuid &&
            candidate.phase == CoursePreviewWavePhase::Active;
    });
}

void CoursePreviewSimulationSystem::PrewarmWave(
    std::size_t index,
    bool emitEvents) {
    if (index >= waves_.size()) return;
    CoursePreviewWaveState& wave = waves_[index];
    if (wave.phase != CoursePreviewWavePhase::Pending) return;
    wave.phase = CoursePreviewWavePhase::Prewarming;
    for (CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.waveGuid == wave.waveGuid && enemy.phase == CoursePreviewEnemyPhase::Dormant) {
            enemy.phase = CoursePreviewEnemyPhase::Prewarmed;
        }
    }
    if (emitEvents) {
        PushEvent(
            CoursePreviewEventType::WavePrewarmed,
            wave.waveGuid,
            wave.displayName + " entered prewarm range.");
    }
}

void CoursePreviewSimulationSystem::ActivateWave(
    std::size_t index,
    bool emitEvents) {
    if (index >= waves_.size()) return;
    CoursePreviewWaveState& wave = waves_[index];
    if (wave.phase == CoursePreviewWavePhase::Active ||
        wave.phase == CoursePreviewWavePhase::Completed ||
        wave.phase == CoursePreviewWavePhase::Disabled) {
        return;
    }
    wave.phase = CoursePreviewWavePhase::Active;
    wave.activeSeconds = 0.0f;
    for (CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.waveGuid != wave.waveGuid ||
            enemy.phase == CoursePreviewEnemyPhase::Disabled ||
            enemy.phase == CoursePreviewEnemyPhase::Defeated) {
            continue;
        }
        enemy.phase = CoursePreviewEnemyPhase::Active;
        enemy.activeSeconds = 0.0f;
        if (emitEvents) {
            PushEvent(
                CoursePreviewEventType::EnemyActivated,
                enemy.placementGuid,
                enemy.actorAssetId + " activated.");
        }
    }
    if (emitEvents) {
        PushEvent(
            CoursePreviewEventType::WaveActivated,
            wave.waveGuid,
            wave.displayName + " activated.");
    }
}

void CoursePreviewSimulationSystem::CompleteWave(
    std::size_t index,
    bool emitEvents) {
    if (index >= waves_.size() ||
        waves_[index].phase != CoursePreviewWavePhase::Active) {
        return;
    }
    CoursePreviewWaveState& state = waves_[index];
    state.phase = CoursePreviewWavePhase::Completed;
    const CourseWaveDefinition* definition = nullptr;
    for (const CourseWaveDefinition& wave : snapshot_.waveDefinitions) {
        if (wave.editorGuid == state.waveGuid) {
            definition = &wave;
            break;
        }
    }
    for (CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.waveGuid == state.waveGuid && enemy.phase == CoursePreviewEnemyPhase::Active) {
            enemy.phase = CoursePreviewEnemyPhase::Retired;
        }
    }
    if (definition != nullptr && !definition->nextWaveGuid.empty()) {
        forcedWaveActivations_.insert(definition->nextWaveGuid);
    }
    if (emitEvents) {
        PushEvent(
            CoursePreviewEventType::WaveCompleted,
            state.waveGuid,
            state.displayName + " completed.");
    }
}

void CoursePreviewSimulationSystem::RefreshFrame() {
    if (!hasSnapshot_) return;
    frame_.railLength = railPath_.Length();
    frame_.distance = (std::clamp)(frame_.distance, 0.0f, frame_.railLength);
    frame_.normalizedProgress = frame_.railLength > 0.0f
        ? frame_.distance / frame_.railLength : 0.0f;
    frame_.railSample = railPath_.Evaluate(frame_.distance);
    frame_.currentSpeed = ResolveTravelSpeed();
    frame_.activeWaves = static_cast<uint32_t>(std::count_if(
        waves_.begin(), waves_.end(), [](const auto& state) {
            return state.phase == CoursePreviewWavePhase::Active;
        }));
    frame_.prewarmingWaves = static_cast<uint32_t>(std::count_if(
        waves_.begin(), waves_.end(), [](const auto& state) {
            return state.phase == CoursePreviewWavePhase::Prewarming ||
                state.phase == CoursePreviewWavePhase::Blocked;
        }));
    frame_.completedWaves = static_cast<uint32_t>(std::count_if(
        waves_.begin(), waves_.end(), [](const auto& state) {
            return state.phase == CoursePreviewWavePhase::Completed;
        }));
    frame_.activeEnemies = static_cast<uint32_t>(std::count_if(
        enemies_.begin(), enemies_.end(), [](const auto& state) {
            return state.phase == CoursePreviewEnemyPhase::Active;
        }));
}

void CoursePreviewSimulationSystem::PushEvent(
    CoursePreviewEventType type,
    std::string subjectGuid,
    std::string message) {
    if (events_.size() >= kMaximumEventHistory) events_.erase(events_.begin());
    events_.push_back(CoursePreviewSimulationEvent{
        type,
        frame_.distance,
        frame_.elapsedSeconds,
        std::move(subjectGuid),
        std::move(message)});
}

float CoursePreviewSimulationSystem::ResolveTravelSpeed() const {
    if (settings_.travelSpeed > 0.0f) return settings_.travelSpeed;
    if (!hasSnapshot_ || railPath_.Length() <= 0.0f) return 0.0f;
    return (std::max)(0.01f, railPath_.Evaluate(frame_.distance).speed);
}

std::size_t CoursePreviewSimulationSystem::FindWaveIndex(std::string_view guid) const {
    if (guid.empty()) return kInvalidIndex;
    const auto found = std::find_if(waves_.begin(), waves_.end(), [&](const auto& state) {
        return state.waveGuid == guid;
    });
    return found == waves_.end()
        ? kInvalidIndex
        : static_cast<std::size_t>(std::distance(waves_.begin(), found));
}

std::size_t CoursePreviewSimulationSystem::FindEnemyIndex(std::string_view guid) const {
    const auto found = std::find_if(enemies_.begin(), enemies_.end(), [&](const auto& state) {
        return state.placementGuid == guid;
    });
    return found == enemies_.end()
        ? kInvalidIndex
        : static_cast<std::size_t>(std::distance(enemies_.begin(), found));
}

void CoursePreviewSimulationSystem::Build(
    const EditorViewportOverlayFrameContext& context,
    EditorViewportOverlayCommandSink& sink) const {
    viewportStats_ = {};
    if (!IsActive() || !hasSnapshot_ || !settings_.showViewportOverlay ||
        context.coordinates == nullptr) {
        return;
    }
    const EditorViewportProjectedPoint playhead =
        context.coordinates->ProjectWorld(frame_.railSample.position);
    if (playhead.valid && playhead.inDepth && playhead.onscreen) {
        EditorViewportOverlayItemOptions options{};
        options.selected = true;
        options.background = true;
        options.priority = 950;
        sink.CircleFilled(playhead.render.x, playhead.render.y, 8.0f, 0xff55f0ffu, options);
        sink.Circle(playhead.render.x, playhead.render.y, 13.0f, 0xff55f0ffu, 2.5f, options);
        sink.Label(
            playhead.render.x + 16.0f,
            playhead.render.y - 16.0f,
            std::string("PREVIEW ") + ToString(frame_.playback) + "  " +
                std::to_string(static_cast<int>(frame_.distance)) + "m",
            0xff55f0ffu,
            options);
        ++viewportStats_.labels;
    } else {
        ++viewportStats_.rejectedBehindCamera;
    }

    for (const CoursePreviewWaveState& wave : waves_) {
        if (wave.phase == CoursePreviewWavePhase::Pending ||
            wave.phase == CoursePreviewWavePhase::Disabled) {
            continue;
        }
        const RailPathSample sample = railPath_.Evaluate(wave.triggerDistance);
        const EditorViewportProjectedPoint projected =
            context.coordinates->ProjectWorld(sample.position);
        if (!projected.valid || !projected.inDepth || !projected.onscreen) {
            ++viewportStats_.rejectedBehindCamera;
            continue;
        }
        EditorViewportOverlayItemOptions options{};
        options.background = true;
        options.priority = wave.phase == CoursePreviewWavePhase::Active ? 900 : 700;
        const uint32_t color = WaveColor(wave.phase);
        sink.RectFilled(
            projected.render.x - 7.0f, projected.render.y - 7.0f,
            projected.render.x + 7.0f, projected.render.y + 7.0f,
            color, options);
        sink.Label(
            projected.render.x + 11.0f,
            projected.render.y - 11.0f,
            wave.displayName + " [" + ToString(wave.phase) + "]",
            color,
            options);
        ++viewportStats_.waveMarkers;
        ++viewportStats_.labels;
    }

    const CourseEnemyAuthoringModel enemyModel(snapshot_);
    if (!enemyModel.IsValid()) return;
    for (const CoursePreviewEnemyState& enemy : enemies_) {
        if (enemy.phase == CoursePreviewEnemyPhase::Dormant ||
            enemy.phase == CoursePreviewEnemyPhase::Disabled) {
            continue;
        }
        const CourseEnemyPlacementResolution resolved =
            enemyModel.Resolve(enemy.placementGuid);
        if (!resolved.valid) continue;
        const EditorViewportProjectedPoint projected =
            context.coordinates->ProjectWorld(resolved.worldPosition);
        if (!projected.valid || !projected.inDepth || !projected.onscreen) {
            ++viewportStats_.rejectedBehindCamera;
            continue;
        }
        EditorViewportOverlayItemOptions options{};
        options.background = true;
        options.priority = enemy.phase == CoursePreviewEnemyPhase::Active ? 920 : 650;
        const uint32_t color = EnemyColor(enemy.phase);
        sink.CircleFilled(projected.render.x, projected.render.y, 6.0f, color, options);
        sink.Circle(projected.render.x, projected.render.y, 9.0f, color, 2.0f, options);
        sink.Label(
            projected.render.x + 10.0f,
            projected.render.y + 7.0f,
            enemy.actorAssetId + " [" + ToString(enemy.phase) + "]",
            color,
            options);
        ++viewportStats_.enemyMarkers;
        ++viewportStats_.labels;
    }
}

const char* ToString(CoursePreviewPlaybackState state) {
    switch (state) {
    case CoursePreviewPlaybackState::Stopped: return "Stopped";
    case CoursePreviewPlaybackState::Paused: return "Paused";
    case CoursePreviewPlaybackState::Playing: return "Playing";
    case CoursePreviewPlaybackState::Completed: return "Completed";
    case CoursePreviewPlaybackState::Error: return "Error";
    }
    return "Unknown";
}

const char* ToString(CoursePreviewWavePhase phase) {
    switch (phase) {
    case CoursePreviewWavePhase::Disabled: return "Disabled";
    case CoursePreviewWavePhase::Pending: return "Pending";
    case CoursePreviewWavePhase::Prewarming: return "Prewarming";
    case CoursePreviewWavePhase::Blocked: return "Blocked";
    case CoursePreviewWavePhase::Active: return "Active";
    case CoursePreviewWavePhase::Completed: return "Completed";
    }
    return "Unknown";
}

const char* ToString(CoursePreviewEnemyPhase phase) {
    switch (phase) {
    case CoursePreviewEnemyPhase::Disabled: return "Disabled";
    case CoursePreviewEnemyPhase::Dormant: return "Dormant";
    case CoursePreviewEnemyPhase::Prewarmed: return "Prewarmed";
    case CoursePreviewEnemyPhase::Active: return "Active";
    case CoursePreviewEnemyPhase::Defeated: return "Defeated";
    case CoursePreviewEnemyPhase::Retired: return "Retired";
    }
    return "Unknown";
}

const char* ToString(CoursePreviewEventType type) {
    switch (type) {
    case CoursePreviewEventType::PreviewStarted: return "Preview Started";
    case CoursePreviewEventType::PreviewStopped: return "Preview Stopped";
    case CoursePreviewEventType::PreviewCompleted: return "Preview Completed";
    case CoursePreviewEventType::WavePrewarmed: return "Wave Prewarmed";
    case CoursePreviewEventType::WaveActivated: return "Wave Activated";
    case CoursePreviewEventType::WaveCompleted: return "Wave Completed";
    case CoursePreviewEventType::EnemyActivated: return "Enemy Activated";
    case CoursePreviewEventType::EnemyDefeated: return "Enemy Defeated";
    case CoursePreviewEventType::TriggerEvent: return "Trigger Event";
    }
    return "Unknown";
}

} // namespace editor
