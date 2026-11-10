#pragma once
#include <string_view>

namespace RailGroundBattery {
inline bool IsPredictive(std::string_view id) { return id=="short_turret_predictive"; }
inline bool IsBattery(std::string_view id) { return id=="short_turret" || IsPredictive(id); }
inline bool IsProjectile(std::string_view id) { return id=="enemy_turret_predictive"; }
inline bool IsFirstLiveBattery(std::string_view waveId) { return waveId=="short_turret_left"; }
// Start the first shot far enough out for a second full warning during a normal approach.
inline float FirstShotDistance(std::string_view waveId) { return IsFirstLiveBattery(waveId) ? 114.0f : 108.0f; }
inline constexpr float FirstLiveProjectileSpeed=40.0f;
// Leave enough forward space to see and intercept a side-mounted volley.
// Repeated attacks must budget their warning before reaching this boundary.
inline constexpr float MinimumFireDistance=40.0f;
}
