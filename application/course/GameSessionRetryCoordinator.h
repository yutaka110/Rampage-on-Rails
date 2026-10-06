#pragma once

#include <cstdint>
#include <string>

#include "CourseAsset.h"
#include "CourseCollisionSystem.h"
#include "CourseGameplayWaveRuntimeBridge.h"
#include "CourseSpawnRuntime.h"
#include "EncounterPerformanceScoreSystem.h"
#include "EnemyEncounterPacingDirector.h"
#include "GameSessionSystem.h"
#include "GrazeScoreSystem.h"
#include "RailDodgeSystem.h"
#include "RailPlayerMovementSystem.h"
#include "RailVehicleMovementSystem.h"
#include "RailVehicleMountedEvasionSystem.h"
#include "RailVehicleDamageCoordinator.h"
#include "SectionCheckpointSystem.h"

struct GameSessionRetryCoordinatorBinding final {
    GameSessionSystem* session = nullptr;
    CourseRuntime* courseRuntime = nullptr;
    CourseGameplayWaveRuntimeBridge* waveRuntime = nullptr;
    CourseSpawnRuntime* spawnRuntime = nullptr;
    CourseCollisionSystem* collisionSystem = nullptr;
    SectionCheckpointSystem* sectionCheckpoints = nullptr;
    const CourseAsset* course = nullptr;
    RailPlayerMovementSystem* playerMovement = nullptr;
    RailDodgeSystem* playerDodge = nullptr;
    RailVehicleMovementSystem* railVehicle = nullptr;
    const RailPath* railPath = nullptr;
    GrazeScoreSystem* grazeScore = nullptr;
    RailVehicleMountedEvasionSystem* mountedEvasion = nullptr;
    RailVehicleDamageCoordinator* vehicleDamageCoordinator = nullptr;
    EnemyEncounterPacingDirector* encounterPacing = nullptr;
    EncounterPerformanceScoreSystem* encounterScore = nullptr;
};

struct GameSessionRetryCheckpoint final {
    bool valid = false;
    uint64_t runIndex = 0;
    float courseDistance = 0.0f;
    std::string checkpointId;
    CourseGameplayWaveCheckpoint wave;
    CourseSpawnRuntimeCheckpoint spawn;
    bool hasWaveCheckpoint = false;
    uint64_t revision = 0;
    RailPlayerMovementRuntimeState playerMovement;
    RailDodgeRuntimeState playerDodge;
    bool hasPlayerRuntime = false;
    RailVehicleRuntimeState railVehicle;
    bool hasRailVehicleRuntime = false;
    GrazeScoreRuntimeState grazeScore;
    PlayerNearMissRuntimeState nearMiss;
    bool hasGrazeScoreRuntime = false;
    RailVehicleMountedEvasionRuntimeState mountedEvasion;
    bool hasMountedEvasionRuntime = false;
    RailVehicleDamageRuntimeState vehicleDamage;
    bool hasVehicleDamageRuntime = false;
    EnemyEncounterPacingCheckpoint encounterPacing;
    bool hasEncounterPacingRuntime = false;
    EncounterPerformanceScoreRuntimeState encounterScore;
    bool hasEncounterScoreRuntime = false;
};

enum class GameSessionRetryStatus : uint8_t {
    None,
    Succeeded,
    NotBound,
    NoCheckpoint,
    SessionRejected,
    StaleRun,
    WaveMismatch,
    PlayerRuntimeMismatch,
    VehicleRuntimeMismatch,
    GrazeRuntimeMismatch,
    MountedEvasionRuntimeMismatch,
    VehicleDamageRuntimeMismatch,
    EncounterPacingRuntimeMismatch,
    EncounterScoreRuntimeMismatch,
    SpawnRuntimeMismatch,
};

struct GameSessionRetryResult final {
    GameSessionRetryStatus status = GameSessionRetryStatus::None;
    bool succeeded = false;
    float restoredDistance = 0.0f;
    uint32_t restoredEnemies = 0;
    uint32_t restoredObstacles = 0;
    uint32_t restoredWaves = 0;
    std::string message;
};

// Coordinates the destructive retry boundary. Validation happens before the
// session consumes a retry, then CourseRuntime, SpawnRuntime, WaveRuntime,
// collision and section state are restored as one foreground transaction.
class GameSessionRetryCoordinator final {
public:
    bool Bind(
        GameSessionRetryCoordinatorBinding binding,
        std::string* errorMessage = nullptr);
    void Unbind();
    bool CaptureCheckpoint(
        float courseDistance,
        std::string checkpointId,
        std::string* errorMessage = nullptr);
    GameSessionRetryResult Retry(std::string* errorMessage = nullptr);

    bool IsBound() const noexcept;
    bool CanRetry() const noexcept;
    const GameSessionRetryCheckpoint& Checkpoint() const noexcept {
        return checkpoint_;
    }
    const GameSessionRetryResult& LastResult() const noexcept { return lastResult_; }

private:
    bool ValidateWaveCheckpoint(std::string* errorMessage) const;

    GameSessionRetryCoordinatorBinding binding_{};
    GameSessionRetryCheckpoint checkpoint_{};
    GameSessionRetryResult lastResult_{};
    uint64_t checkpointRevision_ = 0;
};

const char* ToString(GameSessionRetryStatus status);
