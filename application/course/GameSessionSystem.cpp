#include "GameSessionSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

bool FiniteNonNegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

float ClampDistance(float distance, float courseLength) {
    if (!std::isfinite(distance)) return 0.0f;
    if (courseLength <= 0.0f) return (std::max)(0.0f, distance);
    return (std::clamp)(distance, 0.0f, courseLength);
}

void SetError(std::string* errorMessage, std::string message) {
    if (errorMessage != nullptr) *errorMessage = std::move(message);
}

} // namespace

GameSessionDefinition GameSessionDefinition::RailShooterDefaults() {
    return {};
}

bool GameSessionDefinition::Validate(std::string* errorMessage) const {
    if (sessionId.empty()) {
        SetError(errorMessage, "GameSessionDefinition.sessionId must not be empty.");
        return false;
    }
    if (!std::isfinite(maximumPlayerHealth) || maximumPlayerHealth <= 0.0f) {
        SetError(errorMessage, "maximumPlayerHealth must be finite and greater than zero.");
        return false;
    }
    if (!FiniteNonNegative(startingPlayerHealth) ||
        startingPlayerHealth > maximumPlayerHealth) {
        SetError(errorMessage, "startingPlayerHealth must be within [0, maximumPlayerHealth].");
        return false;
    }
    if (!std::isfinite(retryHealthRatio) || retryHealthRatio <= 0.0f ||
        retryHealthRatio > 1.0f) {
        SetError(errorMessage, "retryHealthRatio must be within (0, 1].");
        return false;
    }
    if (!FiniteNonNegative(introDurationSeconds) ||
        !FiniteNonNegative(resultDelaySeconds) ||
        !FiniteNonNegative(timeLimitSeconds) ||
        !FiniteNonNegative(courseEndTolerance)) {
        SetError(errorMessage, "session durations and courseEndTolerance must be finite and non-negative.");
        return false;
    }
    if (startingRetries > 99) {
        SetError(errorMessage, "startingRetries exceeds the supported commercial-session limit of 99.");
        return false;
    }
    if (eventHistoryCapacity < 16 || eventHistoryCapacity > 4096) {
        SetError(errorMessage, "eventHistoryCapacity must be within [16, 4096].");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool GameSessionSystem::Initialize(
    const GameSessionDefinition& definition,
    float courseLength,
    std::string* errorMessage) {
    BeginOperation();
    if (!definition.Validate(errorMessage)) return false;
    if (!FiniteNonNegative(courseLength)) {
        SetError(errorMessage, "courseLength must be finite and non-negative.");
        return false;
    }

    definition_ = definition;
    state_ = {};
    state_.phase = GameSessionPhase::Ready;
    state_.phaseBeforePause = GameSessionPhase::Playing;
    state_.maximumPlayerHealth = definition_.maximumPlayerHealth;
    state_.playerHealth = definition_.startingPlayerHealth;
    state_.courseLength = courseLength;
    state_.retriesRemaining = definition_.startingRetries;
    state_.revision = 1;
    state_.runIndex = 1;
    state_.statusMessage = "Session ready.";
    eventHistory_.clear();
    nextEventSequence_ = 1;
    RefreshDerivedState();
    PushEvent(
        GameSessionEventType::Initialized,
        GameSessionPhase::Uninitialized,
        GameSessionPhase::Ready,
        GameSessionEndReason::None,
        courseLength,
        definition_.sessionId,
        "Game session initialized.");
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool GameSessionSystem::Start(float startDistance, std::string* errorMessage) {
    BeginOperation();
    if (!IsInitialized()) {
        SetError(errorMessage, "Cannot start an uninitialized game session.");
        return false;
    }
    if (state_.phase != GameSessionPhase::Ready) {
        SetError(errorMessage, "Start is valid only while the session is Ready.");
        return false;
    }

    ResetRunState(startDistance, true);
    const GameSessionPhase next = definition_.introDurationSeconds > 0.0f
        ? GameSessionPhase::Intro
        : GameSessionPhase::Playing;
    Transition(
        next,
        GameSessionEndReason::None,
        GameSessionEventType::Started,
        next == GameSessionPhase::Intro ? "Session intro started." : "Gameplay started.");
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool GameSessionSystem::RestartRun(float startDistance, std::string* errorMessage) {
    BeginOperation();
    if (!IsInitialized()) {
        SetError(errorMessage, "Cannot restart an uninitialized game session.");
        return false;
    }

    ++state_.runIndex;
    ResetRunState(startDistance, true);
    const GameSessionPhase next = definition_.introDurationSeconds > 0.0f
        ? GameSessionPhase::Intro
        : GameSessionPhase::Playing;
    Transition(
        next,
        GameSessionEndReason::None,
        GameSessionEventType::RunRestarted,
        "Game session run restarted.");
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool GameSessionSystem::Retry(std::string* errorMessage) {
    BeginOperation();
    const bool defeatResult =
        state_.outcome == GameSessionOutcome::Defeat &&
        (state_.phase == GameSessionPhase::Defeat || state_.phase == GameSessionPhase::Result);
    if (!defeatResult) {
        SetError(errorMessage, "Retry is valid only after defeat.");
        return false;
    }
    if (!definition_.allowCheckpointRetry || (!definition_.unlimitedCheckpointRetries && state_.retriesRemaining == 0)) {
        SetError(errorMessage, "No retry is available for this session.");
        return false;
    }

    const GameSessionPhase before = state_.phase;
    if(!definition_.unlimitedCheckpointRetries) --state_.retriesRemaining;
    ++state_.attemptIndex;
    state_.phase = definition_.introDurationSeconds > 0.0f
        ? GameSessionPhase::Intro
        : GameSessionPhase::Playing;
    state_.phaseBeforePause = GameSessionPhase::Playing;
    state_.outcome = GameSessionOutcome::None;
    state_.endReason = GameSessionEndReason::None;
    state_.playerHealth = definition_.maximumPlayerHealth * definition_.retryHealthRatio;
    state_.courseDistance = state_.checkpointDistance;
    if (definition_.restoreCheckpointScoreOnRetry) state_.score = state_.checkpointScore;
    state_.combo = 0;
    state_.attemptElapsedSeconds = 0.0f;
    state_.phaseElapsedSeconds = 0.0f;
    state_.objectiveFailed = false;
    state_.statusMessage = "Retry started from checkpoint.";
    ++state_.revision;
    RefreshDerivedState();
    PushEvent(
        GameSessionEventType::RetryStarted,
        before,
        state_.phase,
        GameSessionEndReason::None,
        state_.checkpointDistance,
        state_.checkpointId,
        state_.statusMessage);
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool GameSessionSystem::Pause() {
    BeginOperation();
    if (!definition_.allowPause ||
        (state_.phase != GameSessionPhase::Playing && state_.phase != GameSessionPhase::Intro)) {
        return false;
    }
    state_.phaseBeforePause = state_.phase;
    state_.pausedPhaseElapsedSeconds = state_.phaseElapsedSeconds;
    Transition(
        GameSessionPhase::Paused,
        GameSessionEndReason::None,
        GameSessionEventType::Paused,
        "Session paused.");
    return true;
}

bool GameSessionSystem::Resume() {
    BeginOperation();
    if (state_.phase != GameSessionPhase::Paused) return false;
    const GameSessionPhase resumed = state_.phaseBeforePause == GameSessionPhase::Intro
        ? GameSessionPhase::Intro
        : GameSessionPhase::Playing;
    Transition(
        resumed,
        GameSessionEndReason::None,
        GameSessionEventType::Resumed,
        "Session resumed.");
    state_.phaseElapsedSeconds = state_.pausedPhaseElapsedSeconds;
    state_.pausedPhaseElapsedSeconds = 0.0f;
    return true;
}

void GameSessionSystem::Update(const GameSessionFrameInput& input) {
    BeginOperation();
    if (!IsInitialized()) return;

    ++state_.frameIndex;
    const float dt = std::isfinite(input.deltaTime)
        ? (std::max)(0.0f, input.deltaTime)
        : 0.0f;
    if (state_.phase == GameSessionPhase::Paused ||
        state_.phase == GameSessionPhase::Ready ||
        state_.phase == GameSessionPhase::Result) {
        ++state_.revision;
        RefreshDerivedState();
        return;
    }

    state_.sessionElapsedSeconds += dt;
    state_.phaseElapsedSeconds += dt;
    if (state_.phase == GameSessionPhase::Intro ||
        state_.phase == GameSessionPhase::Playing) {
        state_.courseDistance = ClampDistance(input.courseDistance, state_.courseLength);
        state_.completedMandatoryWaves = input.completedMandatoryWaves;
        state_.totalMandatoryWaves = input.totalMandatoryWaves;
        state_.completedMandatoryObjectives = input.completedMandatoryObjectives;
        state_.totalMandatoryObjectives = input.totalMandatoryObjectives;
        state_.combatClearConfirmed = input.combatClearConfirmed;
        state_.objectiveFailed = state_.objectiveFailed || input.objectiveFailed;
        state_.mandatoryWavesSatisfied = !definition_.requireAllMandatoryWaves ||
            state_.totalMandatoryWaves == 0 ||
            state_.completedMandatoryWaves >= state_.totalMandatoryWaves;
        state_.mandatoryObjectivesSatisfied = !definition_.requireAllMandatoryObjectives ||
            state_.totalMandatoryObjectives == 0 ||
            state_.completedMandatoryObjectives >= state_.totalMandatoryObjectives;
        state_.courseEndReached = state_.courseLength > 0.0f &&
            state_.courseDistance >=
                (std::max)(0.0f, state_.courseLength - definition_.courseEndTolerance);
    }

    if (state_.phase == GameSessionPhase::Intro) {
        if (state_.phaseElapsedSeconds >= definition_.introDurationSeconds) {
            Transition(
                GameSessionPhase::Playing,
                GameSessionEndReason::None,
                GameSessionEventType::IntroCompleted,
                "Session intro completed.");
        } else {
            ++state_.revision;
            RefreshDerivedState();
        }
        return;
    }

    if (state_.phase == GameSessionPhase::Playing) {
        state_.gameplayElapsedSeconds += dt;
        state_.attemptElapsedSeconds += dt;
        const GameSessionEndReason defeat = DefeatReason(input);
        const bool victory = VictoryConditionsSatisfied();
        if (definition_.defeatTakesPriority) {
            if (defeat != GameSessionEndReason::None) {
                ConfirmOutcome(GameSessionOutcome::Defeat, defeat);
                return;
            }
            if (victory) {
                ConfirmOutcome(GameSessionOutcome::Victory, GameSessionEndReason::CourseCompleted);
                return;
            }
        } else {
            if (victory) {
                ConfirmOutcome(GameSessionOutcome::Victory, GameSessionEndReason::CourseCompleted);
                return;
            }
            if (defeat != GameSessionEndReason::None) {
                ConfirmOutcome(GameSessionOutcome::Defeat, defeat);
                return;
            }
        }
        ++state_.revision;
        RefreshDerivedState();
        return;
    }

    if ((state_.phase == GameSessionPhase::Victory ||
         state_.phase == GameSessionPhase::Defeat) &&
        state_.phaseElapsedSeconds >= definition_.resultDelaySeconds) {
        Transition(
            GameSessionPhase::Result,
            state_.endReason,
            GameSessionEventType::ResultEntered,
            state_.outcome == GameSessionOutcome::Victory
                ? "Victory result ready."
                : "Defeat result ready.");
        return;
    }

    ++state_.revision;
    RefreshDerivedState();
}

float GameSessionSystem::ApplyPlayerDamage(float amount, std::string_view sourceId) {
    BeginOperation();
    if (state_.phase != GameSessionPhase::Playing ||
        !std::isfinite(amount) || amount <= 0.0f || state_.playerHealth <= 0.0f) {
        return 0.0f;
    }
    const float applied = (std::min)(state_.playerHealth, amount);
    state_.playerHealth -= applied;
    state_.statusMessage = "Player damaged.";
    ++state_.revision;
    RefreshDerivedState();
    PushEvent(
        GameSessionEventType::PlayerDamaged,
        state_.phase,
        state_.phase,
        GameSessionEndReason::None,
        applied,
        std::string(sourceId),
        state_.statusMessage);
    return applied;
}

float GameSessionSystem::RestorePlayerHealth(float amount, std::string_view sourceId) {
    BeginOperation();
    if ((state_.phase != GameSessionPhase::Playing &&
         state_.phase != GameSessionPhase::Intro) ||
        !std::isfinite(amount) || amount <= 0.0f) {
        return 0.0f;
    }
    const float restored =
        (std::min)(state_.maximumPlayerHealth - state_.playerHealth, amount);
    if (restored <= 0.0f) return 0.0f;
    state_.playerHealth += restored;
    state_.statusMessage = "Player health restored.";
    ++state_.revision;
    RefreshDerivedState();
    PushEvent(
        GameSessionEventType::PlayerRecovered,
        state_.phase,
        state_.phase,
        GameSessionEndReason::None,
        restored,
        std::string(sourceId),
        state_.statusMessage);
    return restored;
}

void GameSessionSystem::AddScore(uint64_t amount, std::string_view sourceId) {
    BeginOperation();
    if (amount == 0 || state_.phase != GameSessionPhase::Playing) return;
    const uint64_t available = (std::numeric_limits<uint64_t>::max)() - state_.score;
    const uint64_t applied = (std::min)(amount, available);
    state_.score += applied;
    state_.statusMessage = "Score updated.";
    ++state_.revision;
    PushEvent(
        GameSessionEventType::ScoreChanged,
        state_.phase,
        state_.phase,
        GameSessionEndReason::None,
        static_cast<float>((std::min<uint64_t>)(applied, 16777216ULL)),
        std::string(sourceId),
        state_.statusMessage);
}

void GameSessionSystem::SetCombo(uint32_t combo, std::string_view sourceId) {
    BeginOperation();
    if (state_.phase != GameSessionPhase::Playing || state_.combo == combo) return;
    state_.combo = combo;
    state_.maximumCombo = (std::max)(state_.maximumCombo, combo);
    state_.statusMessage = "Combo updated.";
    ++state_.revision;
    PushEvent(
        GameSessionEventType::ComboChanged,
        state_.phase,
        state_.phase,
        GameSessionEndReason::None,
        static_cast<float>(combo),
        std::string(sourceId),
        state_.statusMessage);
}

bool GameSessionSystem::RegisterCheckpoint(float courseDistance, std::string checkpointId) {
    BeginOperation();
    if (!IsInitialized() || !std::isfinite(courseDistance)) return false;
    const float clamped = ClampDistance(courseDistance, state_.courseLength);
    if (clamped + 0.0001f < state_.checkpointDistance) return false;
    if (std::abs(clamped - state_.checkpointDistance) <= 0.0001f &&
        checkpointId == state_.checkpointId) {
        return false;
    }
    state_.checkpointDistance = clamped;
    state_.checkpointScore = state_.score;
    state_.checkpointId = std::move(checkpointId);
    state_.statusMessage = "Checkpoint reached.";
    ++state_.revision;
    PushEvent(
        GameSessionEventType::CheckpointReached,
        state_.phase,
        state_.phase,
        GameSessionEndReason::None,
        clamped,
        state_.checkpointId,
        state_.statusMessage);
    return true;
}

bool GameSessionSystem::MarkObjectiveFailed(std::string_view objectiveId) {
    BeginOperation();
    if (state_.phase != GameSessionPhase::Playing || state_.objectiveFailed) return false;
    state_.objectiveFailed = true;
    state_.statusMessage = objectiveId.empty()
        ? "Mandatory objective failed."
        : "Mandatory objective failed: " + std::string(objectiveId);
    ++state_.revision;
    RefreshDerivedState();
    return true;
}

bool GameSessionSystem::Abort(std::string_view reason) {
    BeginOperation();
    if (state_.phase != GameSessionPhase::Playing &&
        state_.phase != GameSessionPhase::Intro &&
        state_.phase != GameSessionPhase::Paused) {
        return false;
    }
    ConfirmOutcome(GameSessionOutcome::Defeat, GameSessionEndReason::Aborted);
    if (!reason.empty()) state_.statusMessage += " " + std::string(reason);
    return true;
}

void GameSessionSystem::ResetRunState(float startDistance, bool preserveRunIndex) {
    const uint64_t revision = state_.revision;
    const uint64_t runIndex = preserveRunIndex ? state_.runIndex : 1;
    const float courseLength = state_.courseLength;
    state_ = {};
    state_.phase = GameSessionPhase::Ready;
    state_.phaseBeforePause = GameSessionPhase::Playing;
    state_.revision = revision;
    state_.runIndex = runIndex;
    state_.attemptIndex = 1;
    state_.retriesRemaining = definition_.startingRetries;
    state_.maximumPlayerHealth = definition_.maximumPlayerHealth;
    state_.playerHealth = definition_.startingPlayerHealth;
    state_.courseLength = courseLength;
    state_.courseDistance = ClampDistance(startDistance, courseLength);
    state_.checkpointDistance = state_.courseDistance;
    state_.checkpointScore = 0;
    state_.statusMessage = "Run state reset.";
    RefreshDerivedState();
}

void GameSessionSystem::BeginOperation() {
    eventsThisFrame_.clear();
}

void GameSessionSystem::RefreshDerivedState() {
    state_.gameplaySimulationEnabled = state_.phase == GameSessionPhase::Playing;
    state_.playerInputEnabled = state_.phase == GameSessionPhase::Playing;
    state_.canPause = definition_.allowPause &&
        (state_.phase == GameSessionPhase::Playing || state_.phase == GameSessionPhase::Intro);
    state_.canRetry = definition_.allowCheckpointRetry &&
        (definition_.unlimitedCheckpointRetries || state_.retriesRemaining > 0) &&
        state_.outcome == GameSessionOutcome::Defeat &&
        (state_.phase == GameSessionPhase::Defeat || state_.phase == GameSessionPhase::Result);
}

void GameSessionSystem::Transition(
    GameSessionPhase next,
    GameSessionEndReason reason,
    GameSessionEventType eventType,
    std::string message) {
    const GameSessionPhase before = state_.phase;
    state_.phase = next;
    state_.phaseElapsedSeconds = 0.0f;
    if (reason != GameSessionEndReason::None) state_.endReason = reason;
    state_.statusMessage = std::move(message);
    ++state_.revision;
    RefreshDerivedState();
    PushEvent(
        eventType,
        before,
        next,
        state_.endReason,
        0.0f,
        definition_.sessionId,
        state_.statusMessage);
}

void GameSessionSystem::ConfirmOutcome(
    GameSessionOutcome outcome,
    GameSessionEndReason reason) {
    state_.outcome = outcome;
    state_.endReason = reason;
    Transition(
        outcome == GameSessionOutcome::Victory
            ? GameSessionPhase::Victory
            : GameSessionPhase::Defeat,
        reason,
        outcome == GameSessionOutcome::Victory
            ? GameSessionEventType::VictoryConfirmed
            : GameSessionEventType::DefeatConfirmed,
        outcome == GameSessionOutcome::Victory
            ? "Victory confirmed by authoritative session rules."
            : "Defeat confirmed by authoritative session rules.");
}

bool GameSessionSystem::VictoryConditionsSatisfied() const {
    return state_.courseEndReached &&
        state_.mandatoryWavesSatisfied &&
        state_.mandatoryObjectivesSatisfied &&
        state_.combatClearConfirmed &&
        !state_.objectiveFailed;
}

GameSessionEndReason GameSessionSystem::DefeatReason(
    const GameSessionFrameInput& input) const {
    if (input.abortRequested) return GameSessionEndReason::Aborted;
    if (state_.playerHealth <= 0.0f) return GameSessionEndReason::PlayerDestroyed;
    if (state_.objectiveFailed) return GameSessionEndReason::ObjectiveFailed;
    if (definition_.timeLimitSeconds > 0.0f &&
        (definition_.timeLimitPerAttempt ? state_.attemptElapsedSeconds : state_.gameplayElapsedSeconds)
            >= definition_.timeLimitSeconds) {
        return GameSessionEndReason::TimeExpired;
    }
    return GameSessionEndReason::None;
}

void GameSessionSystem::PushEvent(
    GameSessionEventType type,
    GameSessionPhase before,
    GameSessionPhase after,
    GameSessionEndReason reason,
    float value,
    std::string subjectId,
    std::string message) {
    GameSessionEvent event{};
    event.sequence = nextEventSequence_++;
    event.type = type;
    event.phaseBefore = before;
    event.phaseAfter = after;
    event.reason = reason;
    event.courseDistance = state_.courseDistance;
    event.value = value;
    event.subjectId = std::move(subjectId);
    event.message = std::move(message);
    eventsThisFrame_.push_back(event);
    eventHistory_.push_back(std::move(event));
    const size_t capacity = definition_.eventHistoryCapacity;
    if (eventHistory_.size() > capacity) {
        eventHistory_.erase(
            eventHistory_.begin(),
            eventHistory_.begin() + static_cast<std::ptrdiff_t>(eventHistory_.size() - capacity));
    }
}

const char* ToString(GameSessionPhase phase) {
    switch (phase) {
    case GameSessionPhase::Uninitialized: return "Uninitialized";
    case GameSessionPhase::Ready: return "Ready";
    case GameSessionPhase::Intro: return "Intro";
    case GameSessionPhase::Playing: return "Playing";
    case GameSessionPhase::Paused: return "Paused";
    case GameSessionPhase::Victory: return "Victory";
    case GameSessionPhase::Defeat: return "Defeat";
    case GameSessionPhase::Result: return "Result";
    }
    return "Unknown";
}

const char* ToString(GameSessionOutcome outcome) {
    switch (outcome) {
    case GameSessionOutcome::None: return "None";
    case GameSessionOutcome::Victory: return "Victory";
    case GameSessionOutcome::Defeat: return "Defeat";
    }
    return "Unknown";
}

const char* ToString(GameSessionEndReason reason) {
    switch (reason) {
    case GameSessionEndReason::None: return "None";
    case GameSessionEndReason::CourseCompleted: return "CourseCompleted";
    case GameSessionEndReason::PlayerDestroyed: return "PlayerDestroyed";
    case GameSessionEndReason::ObjectiveFailed: return "ObjectiveFailed";
    case GameSessionEndReason::TimeExpired: return "TimeExpired";
    case GameSessionEndReason::Aborted: return "Aborted";
    }
    return "Unknown";
}

const char* ToString(GameSessionEventType type) {
    switch (type) {
    case GameSessionEventType::Initialized: return "Initialized";
    case GameSessionEventType::Started: return "Started";
    case GameSessionEventType::IntroCompleted: return "IntroCompleted";
    case GameSessionEventType::Paused: return "Paused";
    case GameSessionEventType::Resumed: return "Resumed";
    case GameSessionEventType::CheckpointReached: return "CheckpointReached";
    case GameSessionEventType::PlayerDamaged: return "PlayerDamaged";
    case GameSessionEventType::PlayerRecovered: return "PlayerRecovered";
    case GameSessionEventType::ScoreChanged: return "ScoreChanged";
    case GameSessionEventType::ComboChanged: return "ComboChanged";
    case GameSessionEventType::VictoryConfirmed: return "VictoryConfirmed";
    case GameSessionEventType::DefeatConfirmed: return "DefeatConfirmed";
    case GameSessionEventType::ResultEntered: return "ResultEntered";
    case GameSessionEventType::RetryStarted: return "RetryStarted";
    case GameSessionEventType::RunRestarted: return "RunRestarted";
    }
    return "Unknown";
}
