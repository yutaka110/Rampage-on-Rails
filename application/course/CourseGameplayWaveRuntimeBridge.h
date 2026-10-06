#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "CourseRuntimeProgramAsset.h"
#include "CourseSpawnRuntime.h"

enum class CourseGameplayWavePhase : uint8_t {
    Disabled,
    Pending,
    Prewarming,
    Blocked,
    Active,
    Completed,
};

enum class CourseGameplayActorPhase : uint8_t {
    Dormant,
    Prewarmed,
    Active,
    Defeated,
    Retired,
};

enum class CourseGameplayWaveEventType : uint8_t {
    WavePrewarmed,
    WaveActivated,
    WaveCompleted,
    ActorSpawned,
    ActorDefeated,
};

struct CourseGameplayWaveEvent final {
    CourseGameplayWaveEventType type = CourseGameplayWaveEventType::WaveActivated;
    std::string subjectGuid;
    std::string message;
};

struct CourseGameplayWaveRuntimeSettings final {
    bool retireActorsWhenWaveCompletes = true;
    bool detectMissingActiveActorsAsDefeated = true;
    uint32_t maximumStateTransitionsPerFrame = 256;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseGameplayWaveFrameInput final {
    float deltaTime = 0.0f;
    float playerDistance = 0.0f;
    std::vector<std::string> signaledEventIds;
};

struct CourseGameplayWaveRuntimeStats final {
    bool bound = false;
    uint64_t programFingerprint = 0;
    float playerDistance = 0.0f;
    uint32_t pendingWaves = 0;
    uint32_t prewarmingWaves = 0;
    uint32_t activeWaves = 0;
    uint32_t completedWaves = 0;
    uint32_t activeActors = 0;
    uint32_t defeatedActors = 0;
    uint32_t spawnedThisFrame = 0;
    uint32_t completedThisFrame = 0;
    std::string message;
};

struct CourseGameplayWaveCheckpoint final {
    uint64_t programFingerprint = 0;
    float playerDistance = 0.0f;
    std::vector<CourseGameplayWavePhase> wavePhases;
    std::vector<float> waveActiveSeconds;
    std::vector<std::string> defeatedPlacementGuids;
    std::vector<std::string> signaledEventIds;
};

// Owns only Wave state. Actors are materialized into the supplied gameplay
// CourseSpawnRuntime, so collision, damage, targeting, telegraphs and rendering
// keep using the existing authoritative Actor ID path.
class CourseGameplayWaveRuntimeBridge final {
public:
    bool Bind(
        const CourseRuntimeProgramAsset* program,
        CourseSpawnRuntime* spawnRuntime,
        float startDistance = 0.0f,
        std::string* errorMessage = nullptr);
    void Unbind();
    void Reset(float startDistance = 0.0f);
    void Update(const CourseGameplayWaveFrameInput& input);

    bool NotifyEnemyDefeated(std::string_view placementGuid);
    void SignalEvent(std::string eventId);
    CourseGameplayWaveCheckpoint CaptureCheckpoint() const;
    bool RestoreCheckpoint(
        const CourseGameplayWaveCheckpoint& checkpoint,
        std::string* errorMessage = nullptr);

    bool IsBound() const noexcept { return program_ != nullptr && runtime_ != nullptr; }
    const CourseRuntimeProgramAsset* Program() const noexcept { return program_; }
    const std::vector<CourseGameplayWavePhase>& WavePhases() const noexcept {
        return wavePhases_;
    }
    const std::vector<CourseGameplayActorPhase>& ActorPhases() const noexcept {
        return actorPhases_;
    }
    const std::vector<CourseGameplayWaveEvent>& Events() const noexcept { return events_; }
    const CourseGameplayWaveRuntimeStats& Stats() const noexcept { return stats_; }
    bool Configure(const CourseGameplayWaveRuntimeSettings& settings, std::string* errorMessage = nullptr);
    const CourseGameplayWaveRuntimeSettings& Settings() const noexcept { return settings_; }

private:
    void InitializeState(float startDistance);
    void ReconcileDefeatedActors();
    void UpdateActiveWaves(float deltaTime);
    void UpdatePrewarmAndActivation();
    bool CanActivateWave(std::size_t index) const;
    void PrewarmWave(std::size_t index);
    void ActivateWave(std::size_t index);
    void CompleteWave(std::size_t index);
    void SpawnActor(uint32_t actorIndex);
    void RemoveActorsForWave(std::size_t waveIndex);
    void RemoveOwnedActors();
    bool RuntimeContains(std::string_view placementGuid) const;
    void RefreshStats();
    void PushEvent(
        CourseGameplayWaveEventType type,
        std::string subjectGuid,
        std::string message);

    const CourseRuntimeProgramAsset* program_ = nullptr;
    CourseSpawnRuntime* runtime_ = nullptr;
    CourseGameplayWaveRuntimeSettings settings_{};
    CourseGameplayWaveRuntimeStats stats_{};
    std::vector<CourseGameplayWavePhase> wavePhases_;
    std::vector<CourseGameplayActorPhase> actorPhases_;
    std::vector<float> waveActiveSeconds_;
    std::unordered_set<std::string> signaledEventIds_;
    std::unordered_set<std::size_t> forcedWaveIndices_;
    std::vector<CourseGameplayWaveEvent> events_;
    float playerDistance_ = 0.0f;
};

const char* ToString(CourseGameplayWavePhase phase);
const char* ToString(CourseGameplayActorPhase phase);
const char* ToString(CourseGameplayWaveEventType type);
