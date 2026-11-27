#pragma once
#include <algorithm>
#include <cmath>

namespace RailCavernLightning {
inline constexpr char Actor[] = "cavern_lightning";
inline constexpr char Projectile[] = "enemy_cavern_lightning";
inline constexpr char Visual[] = "visual_cavern_lightning";
inline constexpr float CoreUv = 162.0f;
inline constexpr float WakeUv = 164.0f;
inline constexpr float ArcUv = 166.0f;
inline constexpr float ImpactUv = 168.0f;
inline constexpr float PlasmaUv = 170.0f;
inline constexpr float VortexUv = 172.0f;
inline constexpr float FlightRimUv = 174.0f;
inline constexpr float FlightRibbonUv = 176.0f;
inline constexpr float FlightCrescentUv = 178.0f;
inline constexpr float FlightBodyUv = 3500.0f;
inline constexpr float VortexSeconds = .72f;
inline constexpr float ImpactSeconds = .70f;

// Matches the wedge sampled by RailLightningHeadEdge for the dark body and rim.
inline float FlightOutlineRadius(float angle, float phase) {
    const float x = std::cos(angle);
    const float y = std::sin(angle);
    const float planes = (std::max)({1.20f*x - 2.03f*y, 1.20f*x + 1.48f*y,
        -1.39f*x - .98f*y, -1.32f*x + 1.21f*y, -1.48f*x});
    return 1.0f / planes + .027f * std::sin(angle * 3.0f + phase) +
        .015f * std::sin(angle * 9.0f - phase * 1.3f);
}

inline float VortexProgress(float urgency, float attackLead, float warningLead) {
    const float lead = (std::max)({.05f, warningLead, attackLead});
    const float vortexLead = (std::min)(attackLead, VortexSeconds);
    const float hold = attackLead - vortexLead;
    const float vortexWindow = (std::max)({.05f, warningLead, vortexLead});
    const float remaining = (1.0f - urgency) * lead - hold;
    return (std::clamp)(1.0f - remaining / vortexWindow, 0.0f, 1.0f);
}

inline float FlightHeat(float time, float seed) {
    return .78f + .15f * std::sin(time * 23.0f + seed * .64f) +
        .07f * std::sin(time * 46.0f + seed * 1.31f);
}
}
