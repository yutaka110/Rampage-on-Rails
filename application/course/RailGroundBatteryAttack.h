#pragma once
#include <string_view>

namespace RailGroundBattery {
inline bool IsPredictive(std::string_view id) { return id=="short_turret_predictive"; }
inline bool IsBattery(std::string_view id) { return id=="short_turret" || IsPredictive(id); }
inline bool IsProjectile(std::string_view id) { return id=="enemy_turret_predictive"; }
// Budget two actual travelling locks after the target enters weapon range,
// including the faster ordinary/E/reward trajectory around the final ridge.
// Allow the second lock to arrive before firing while preserving the shared
// 40-unit forward interception boundary below.
inline constexpr float FirstShotDistance=43.0f;
// Leave enough forward space to see and intercept a side-mounted volley.
// Repeated attacks must budget their warning before reaching this boundary.
inline constexpr float MinimumFireDistance=40.0f;
}
