#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "EnemyFormationDefinition.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;

enum class EnemyEntranceExitPhase : uint8_t {
    Pending,
    Entering,
    Active,
    Exiting,
    Exited,
};

struct EnemyEntranceExitRuntimeState final {
    EnemyEntranceExitPhase phase = EnemyEntranceExitPhase::Pending;
    float phaseElapsedSeconds = 0.0f;
    float delayRemainingSeconds = 0.0f;
    float appliedForwardOffset = 0.0f;
    float appliedLateralOffset = 0.0f;
    float appliedVerticalOffset = 0.0f;
    float presentationAlpha = 0.0f;
    float presentationScale = 0.72f;
    uint64_t revision = 0;
    bool initialized = false;
    bool attackSuppressed = true;
    bool targetable = false;
    bool exitRequested = false;
    bool exitComplete = false;
};

enum class EnemyEntranceExitEventKind : uint8_t {
    EntranceStarted,
    EntranceCompleted,
    ExitStarted,
    ExitCompleted,
};

struct EnemyEntranceExitEvent final {
    EnemyEntranceExitEventKind kind =
        EnemyEntranceExitEventKind::EntranceStarted;
    uint32_t actorId = 0;
    std::string formationId;
    uint64_t sequence = 0;
};

struct EnemyEntranceExitFrame final {
    std::vector<EnemyEntranceExitEvent> events;
    uint32_t enteringActors = 0;
    uint32_t activeActors = 0;
    uint32_t exitingActors = 0;
    uint32_t exitedActors = 0;
    uint64_t revision = 0;
};

// Stages rail-local arrivals and departures. The director gates targeting and
// fire while actors are visually outside their authored combat slots.
class EnemyEntranceExitDirector final {
public:
    void Reset();
    void BeginFrame(CourseSpawnRuntime& runtime);
    void Update(CourseSpawnRuntime& runtime, float deltaTime);
    bool RequestActorExit(uint32_t actorId);
    bool RequestFormationExit(std::string formationId);

    const EnemyEntranceExitFrame& Frame() const noexcept { return frame_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void BeginFrameActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime);
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        float deltaTime);

    void QueueEvent(
        EnemyEntranceExitEventKind kind,
        uint32_t actorId,
        const std::string& formationId);

    std::unordered_set<uint32_t> actorExitRequests_;
    std::unordered_set<std::string> formationExitRequests_;
    EnemyEntranceExitFrame frame_{};
    uint64_t eventSequence_ = 1;
    uint64_t revision_ = 0;
};

const char* ToString(EnemyEntranceExitPhase phase) noexcept;
const char* ToString(EnemyEntranceExitEventKind kind) noexcept;
