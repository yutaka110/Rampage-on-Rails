#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class GameSessionPhase : uint8_t {
    Uninitialized,
    Ready,
    Intro,
    Playing,
    Paused,
    Victory,
    Defeat,
    Result,
};

enum class GameSessionOutcome : uint8_t {
    None,
    Victory,
    Defeat,
};

enum class GameSessionEndReason : uint8_t {
    None,
    CourseCompleted,
    PlayerDestroyed,
    ObjectiveFailed,
    TimeExpired,
    Aborted,
};

enum class GameSessionEventType : uint8_t {
    Initialized,
    Started,
    IntroCompleted,
    Paused,
    Resumed,
    CheckpointReached,
    PlayerDamaged,
    PlayerRecovered,
    ScoreChanged,
    ComboChanged,
    VictoryConfirmed,
    DefeatConfirmed,
    ResultEntered,
    RetryStarted,
    RunRestarted,
};

// Immutable tuning for one game-session ruleset. Authoring data may create a
// different definition per mode/course without leaking presentation or input
// concerns into the deterministic runtime state machine.
struct GameSessionDefinition final {
    std::string sessionId = "rail_shooter.default";
    float maximumPlayerHealth = 100.0f;
    float startingPlayerHealth = 100.0f;
    uint32_t startingRetries = 2;
    float retryHealthRatio = 1.0f;
    float introDurationSeconds = 0.0f;
    float resultDelaySeconds = 1.5f;
    float timeLimitSeconds = 0.0f;
    float courseEndTolerance = 0.5f;
    bool requireAllMandatoryWaves = true;
    bool requireAllMandatoryObjectives = true;
    bool allowPause = true;
    bool allowCheckpointRetry = true;
    bool restoreCheckpointScoreOnRetry = true;
    bool defeatTakesPriority = true;
    uint32_t eventHistoryCapacity = 128;
    bool timeLimitPerAttempt = false;

    static GameSessionDefinition RailShooterDefaults();
    bool Validate(std::string* errorMessage = nullptr) const;
};

// Serializable/checkpoint-friendly authoritative state. UI, audio, camera and
// scene transitions consume this state; they never decide victory or defeat.
struct GameSessionRuntimeState final {
    GameSessionPhase phase = GameSessionPhase::Uninitialized;
    GameSessionPhase phaseBeforePause = GameSessionPhase::Playing;
    GameSessionOutcome outcome = GameSessionOutcome::None;
    GameSessionEndReason endReason = GameSessionEndReason::None;
    uint64_t revision = 0;
    uint64_t frameIndex = 0;
    uint64_t runIndex = 0;
    uint32_t attemptIndex = 0;
    uint32_t retriesRemaining = 0;
    uint64_t score = 0;
    uint32_t combo = 0;
    uint32_t maximumCombo = 0;
    float playerHealth = 0.0f;
    float maximumPlayerHealth = 0.0f;
    float courseDistance = 0.0f;
    float courseLength = 0.0f;
    float checkpointDistance = 0.0f;
    uint64_t checkpointScore = 0;
    std::string checkpointId;
    float sessionElapsedSeconds = 0.0f;
    float gameplayElapsedSeconds = 0.0f;
    float attemptElapsedSeconds = 0.0f;
    float phaseElapsedSeconds = 0.0f;
    float pausedPhaseElapsedSeconds = 0.0f;
    uint32_t completedMandatoryWaves = 0;
    uint32_t totalMandatoryWaves = 0;
    uint32_t completedMandatoryObjectives = 0;
    uint32_t totalMandatoryObjectives = 0;
    bool courseEndReached = false;
    bool mandatoryWavesSatisfied = true;
    bool mandatoryObjectivesSatisfied = true;
    // CombatTruthGate owns this value. Course distance and Wave counters may
    // not resolve victory while an attack, projectile or defense window lives.
    bool combatClearConfirmed = true;
    bool objectiveFailed = false;
    bool gameplaySimulationEnabled = false;
    bool playerInputEnabled = false;
    bool canPause = false;
    bool canRetry = false;
    std::string statusMessage;
};

struct GameSessionFrameInput final {
    float deltaTime = 0.0f;
    float courseDistance = 0.0f;
    uint32_t completedMandatoryWaves = 0;
    uint32_t totalMandatoryWaves = 0;
    uint32_t completedMandatoryObjectives = 0;
    uint32_t totalMandatoryObjectives = 0;
    bool objectiveFailed = false;
    bool abortRequested = false;
    // Appended to retain the established aggregate-initializer field order.
    bool combatClearConfirmed = true;
};

struct GameSessionEvent final {
    uint64_t sequence = 0;
    GameSessionEventType type = GameSessionEventType::Initialized;
    GameSessionPhase phaseBefore = GameSessionPhase::Uninitialized;
    GameSessionPhase phaseAfter = GameSessionPhase::Uninitialized;
    GameSessionEndReason reason = GameSessionEndReason::None;
    float courseDistance = 0.0f;
    float value = 0.0f;
    std::string subjectId;
    std::string message;
};

class GameSessionSystem final {
public:
    bool Initialize(
        const GameSessionDefinition& definition,
        float courseLength,
        std::string* errorMessage = nullptr);
    bool Start(float startDistance = 0.0f, std::string* errorMessage = nullptr);
    bool RestartRun(float startDistance = 0.0f, std::string* errorMessage = nullptr);
    bool Retry(std::string* errorMessage = nullptr);
    bool Pause();
    bool Resume();
    void Update(const GameSessionFrameInput& input);

    float ApplyPlayerDamage(float amount, std::string_view sourceId = {});
    float RestorePlayerHealth(float amount, std::string_view sourceId = {});
    void AddScore(uint64_t amount, std::string_view sourceId = {});
    void SetCombo(uint32_t combo, std::string_view sourceId = {});
    bool RegisterCheckpoint(float courseDistance, std::string checkpointId = {});
    bool MarkObjectiveFailed(std::string_view objectiveId = {});
    bool Abort(std::string_view reason = {});

    bool IsInitialized() const noexcept {
        return state_.phase != GameSessionPhase::Uninitialized;
    }
    bool AllowsGameplaySimulation() const noexcept {
        return state_.gameplaySimulationEnabled;
    }
    const GameSessionDefinition& Definition() const noexcept { return definition_; }
    const GameSessionRuntimeState& State() const noexcept { return state_; }
    const std::vector<GameSessionEvent>& EventsThisFrame() const noexcept {
        return eventsThisFrame_;
    }
    const std::vector<GameSessionEvent>& EventHistory() const noexcept {
        return eventHistory_;
    }

private:
    void ResetRunState(float startDistance, bool preserveRunIndex);
    void BeginOperation();
    void RefreshDerivedState();
    void Transition(
        GameSessionPhase next,
        GameSessionEndReason reason,
        GameSessionEventType eventType,
        std::string message);
    void ConfirmOutcome(GameSessionOutcome outcome, GameSessionEndReason reason);
    bool VictoryConditionsSatisfied() const;
    GameSessionEndReason DefeatReason(const GameSessionFrameInput& input) const;
    void PushEvent(
        GameSessionEventType type,
        GameSessionPhase before,
        GameSessionPhase after,
        GameSessionEndReason reason,
        float value,
        std::string subjectId,
        std::string message);

    GameSessionDefinition definition_{};
    GameSessionRuntimeState state_{};
    std::vector<GameSessionEvent> eventsThisFrame_;
    std::vector<GameSessionEvent> eventHistory_;
    uint64_t nextEventSequence_ = 1;
};

const char* ToString(GameSessionPhase phase);
const char* ToString(GameSessionOutcome outcome);
const char* ToString(GameSessionEndReason reason);
const char* ToString(GameSessionEventType type);
