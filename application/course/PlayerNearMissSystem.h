#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "EnemyProjectileSystem.h"

struct PlayerNearMissRequest final {
    uint64_t projectileId = 0;
    uint32_t sourceActorId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    EnemyProjectileTrajectory trajectory = EnemyProjectileTrajectory::Direct;
    std::string sourceId;
    float closeness = 0.0f;
    float surfaceSeparation = 0.0f;
    float railDistance = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
};

struct PlayerNearMissResult final {
    uint64_t sequence = 0;
    PlayerNearMissRequest request{};
    bool accepted = false;
};

struct PlayerNearMissRuntimeState final {
    uint64_t nextSequence = 1;
    uint32_t acceptedNearMisses = 0;
    std::vector<uint64_t> processedProjectileIds;
    uint64_t revision = 0;
};

struct PlayerNearMissSettings final {
    uint32_t projectileHistoryCapacity = 256;
    size_t maximumResultsPerFrame = 16;

    bool Validate(std::string* errorMessage = nullptr) const;
};

class PlayerNearMissSystem final {
public:
    void Reset();
    void Update(float deltaTime);
    PlayerNearMissResult Submit(const PlayerNearMissRequest& request);
    bool RestoreState(
        const PlayerNearMissRuntimeState& state,
        std::string* errorMessage = nullptr);

    const PlayerNearMissRuntimeState& State() const noexcept { return state_; }
    const std::vector<PlayerNearMissResult>& ResultsThisFrame() const noexcept {
        return resultsThisFrame_;
    }
    bool Configure(const PlayerNearMissSettings& settings, std::string* errorMessage = nullptr);
    const PlayerNearMissSettings& Settings() const noexcept { return settings_; }

private:
    bool WasProcessed(uint64_t projectileId) const noexcept;
    void Remember(uint64_t projectileId);

    PlayerNearMissSettings settings_{};
    PlayerNearMissRuntimeState state_{};
    std::vector<PlayerNearMissResult> resultsThisFrame_;
};
