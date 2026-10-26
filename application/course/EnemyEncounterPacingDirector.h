#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "EnemyAttackCoordinator.h"
#include "EnemyEncounterBeatDefinition.h"

class CourseSpawnRuntime;
class CourseGameplayWaveRuntimeBridge;
struct CourseAsset;
struct EnemyEncounterReadabilityFrame;

enum class EnemyEncounterPacingEventKind : uint8_t {
    BeatStarted,
    PhaseChanged,
    FormationExitRequested,
    BeatCompleted,
    ResolveStalled,
};

struct EnemyEncounterPacingEvent final {
    EnemyEncounterPacingEventKind kind =
        EnemyEncounterPacingEventKind::PhaseChanged;
    std::string beatGuid;
    std::string encounterId;
    EnemyEncounterBeatPhase previousPhase = EnemyEncounterBeatPhase::Dormant;
    EnemyEncounterBeatPhase phase = EnemyEncounterBeatPhase::Dormant;
    std::string message;
};

struct EnemyEncounterPacingInput final {
    const CourseAsset* course = nullptr;
    CourseSpawnRuntime* runtime = nullptr;
    const EnemyEncounterReadabilityFrame* readability = nullptr;
    float currentDistance = 0.0f;
    float deltaTime = 0.016f;
    bool gameplayActive = true;
    const CourseGameplayWaveRuntimeBridge* waveRuntime = nullptr;
};

struct EnemyEncounterPacingFrame final {
    bool active = false;
    bool attackWindowOpen = false;
    bool cameraCompositionRequested = false;
    bool resolveStalled = false;
    std::string activeBeatGuid;
    std::string encounterId;
    std::string waveGuid;
    std::string displayName;
    EnemyEncounterBeatPhase phase = EnemyEncounterBeatPhase::Dormant;
    float phaseElapsedSeconds = 0.0f;
    float totalElapsedSeconds = 0.0f;
    float readableRatio = 0.0f;
    uint32_t eligibleActors = 0;
    uint32_t readableActors = 0;
    uint32_t gatedActors = 0;
    uint32_t maximumConcurrentAttackers = 0;
    float maximumThreatBudget = 0.0f;
    EnemyEncounterBeatDefinition definition{};
    std::vector<EnemyEncounterPacingEvent> events;
    uint64_t revision = 0;
};

struct EnemyEncounterPacingCheckpoint final {
    std::string activeBeatGuid;
    EnemyEncounterBeatPhase phase = EnemyEncounterBeatPhase::Dormant;
    float phaseElapsedSeconds = 0.0f;
    float totalElapsedSeconds = 0.0f;
    std::vector<std::string> completedBeatGuids;
};

// Authoritative encounter rhythm state machine. It gates only attack
// commitment; actor simulation, Formation, Telegraph and Combat Truth retain
// their existing ownership.
class EnemyEncounterPacingDirector final {
public:
    void Reset(CourseSpawnRuntime* runtime = nullptr);
    const EnemyEncounterPacingFrame& Update(
        const EnemyEncounterPacingInput& input);
    EnemyEncounterPacingCheckpoint CaptureCheckpoint() const;
    bool RestoreCheckpoint(
        const EnemyEncounterPacingCheckpoint& checkpoint,
        const CourseAsset& course,
        std::string* errorMessage = nullptr);

    const EnemyEncounterPacingFrame& Frame() const noexcept { return frame_; }

private:
    void BeginBeat(
        const EnemyEncounterBeatDefinition& definition,
        CourseSpawnRuntime& runtime,
        EnemyEncounterPacingFrame& next);
    void EnterPhase(
        EnemyEncounterBeatPhase phase,
        EnemyEncounterPacingFrame& next,
        const char* reason);
    void RestoreCoordinator(CourseSpawnRuntime* runtime);

    EnemyEncounterPacingFrame frame_{};
    const EnemyEncounterBeatDefinition* activeDefinition_ = nullptr;
    std::unordered_set<std::string> completedBeatGuids_;
    EnemyAttackCoordinatorSettings coordinatorBaseline_{};
    bool hasCoordinatorBaseline_ = false;
    bool formationExitRequested_ = false;
    bool resolveStallReported_ = false;
    uint64_t revision_ = 0;
};

const char* ToString(EnemyEncounterPacingEventKind kind) noexcept;
