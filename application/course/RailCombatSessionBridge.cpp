#include "RailCombatSessionBridge.h"

#include "EncounterPerformanceScoreSystem.h"
#include "GameSessionSystem.h"

#include <string_view>

namespace {
std::string_view RewardSource(const EncounterPerformanceScoreResult& reward) {
    switch (reward.kind) {
    case EncounterPerformanceScoreKind::LockSweep:
    case EncounterPerformanceScoreKind::BatteryBreak:
    case EncounterPerformanceScoreKind::ProjectileIntercept:
        return ToString(reward.kind);
    case EncounterPerformanceScoreKind::CleanClear:
        return reward.fullSweep ? "encounter_clean_clear_full_sweep" : "encounter_clean_clear";
    case EncounterPerformanceScoreKind::EncounterClear:
        return reward.fullSweep ? "encounter_clear_full_sweep" : "encounter_clear";
    case EncounterPerformanceScoreKind::EnemyDefeat:
    default:
        return "encounter_enemy_defeat";
    }
}
}

void RailCombatSessionBridge::Apply(GameSessionSystem& session,
    const RailCombatSessionInput& input, const RewardCue& playRewardCue) {
    if (input.trackScore) {
        uint64_t grazeScore = 0;
        for (const auto& graze : input.grazes) {
            if (graze.accepted) grazeScore += graze.scoreAwarded;
        }
        session.SetCombo(input.chain, !input.rewards.empty() ? "encounter_performance_chain" :
            (input.defenseSuccesses > 0 ? "enemy_attack_defense_chain" : "graze_chain"));
        if (grazeScore > 0) session.AddScore(grazeScore, "near_miss_graze");
        if (input.defenseScore > 0) session.AddScore(input.defenseScore, "enemy_attack_defense");
    }

    for (const auto& reward : input.rewards) {
        if (!reward.accepted || reward.scoreAwarded == 0) continue;
        const auto kind = reward.kind;
        if (playRewardCue && (kind == EncounterPerformanceScoreKind::LockSweep ||
            kind == EncounterPerformanceScoreKind::BatteryBreak ||
            kind == EncounterPerformanceScoreKind::ProjectileIntercept)) {
            playRewardCue(kind);
        }
        if (input.trackScore) session.AddScore(reward.scoreAwarded, RewardSource(reward));
    }

    for (const auto& damage : input.damage) {
        if (!damage.accepted || damage.appliedDamage <= 0.0f) continue;
        const std::string_view source = damage.request.sourceId.empty()
            ? std::string_view(ToString(damage.request.kind))
            : std::string_view(damage.request.sourceId);
        (void)session.ApplyPlayerDamage(damage.appliedDamage, source);
    }
}
