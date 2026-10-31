#pragma once
#include <string_view>

// Both player weapon paths share the same carrier armour rule. Eligibility is
// captured at firing, so earned lock shots retain it during their flight.
namespace RailBatteryDamage {
inline constexpr float ArmoredMultiplier = .25f;
inline constexpr float CounterMultiplier = 2.0f;
inline bool IsBattery(std::string_view asset) {
    return asset=="short_mothership_rapid" || asset=="short_mothership_heavy";
}
inline bool IsCounterWeapon(std::string_view asset,bool lockOn) {
    return lockOn ? asset=="short_mothership_heavy" : asset=="short_mothership_rapid";
}
}
