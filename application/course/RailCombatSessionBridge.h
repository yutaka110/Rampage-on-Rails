#pragma once

#include <cstdint>
#include <functional>
#include <span>

class GameSessionSystem;
enum class EncounterPerformanceScoreKind : uint8_t;
struct EncounterPerformanceScoreResult;
struct GrazeScoreResult;
struct PlayerDamageResult;

struct RailCombatSessionInput final {
    bool trackScore = true;
    uint32_t chain = 0;
    uint32_t defenseSuccesses = 0;
    uint64_t defenseScore = 0;
    std::span<const GrazeScoreResult> grazes;
    std::span<const EncounterPerformanceScoreResult> rewards;
    std::span<const PlayerDamageResult> damage;
};

class RailCombatSessionBridge final {
public:
    using RewardCue = std::function<void(EncounterPerformanceScoreKind)>;

    static void Apply(GameSessionSystem& session,
        const RailCombatSessionInput& input, const RewardCue& playRewardCue);
};
