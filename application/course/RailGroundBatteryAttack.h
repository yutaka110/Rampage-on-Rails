#pragma once
#include <string_view>

namespace RailGroundBattery {
inline bool IsPredictive(std::string_view id) { return id=="short_turret_predictive"; }
inline bool IsBattery(std::string_view id) { return id=="short_turret" || IsPredictive(id); }
inline bool IsProjectile(std::string_view id) { return id=="enemy_turret_predictive"; }
inline bool IsFirstLiveBattery(std::string_view waveId) { return waveId=="short_turret_left"; }
inline constexpr std::string_view ShelfBatteryWave="shelf_battery";
inline constexpr float RidgeBatteryMarker=2060.0f;
inline constexpr std::string_view ArchBatteryWave="arch_inner_batteries";
inline constexpr float ArchWarningSeconds=1.85f;
inline bool IsUpperValleyBattery(std::string_view waveId,float spawnDistance) {
    return waveId=="route_turret_left" && spawnDistance==2840.0f;
}
inline bool IsRidgeBattery(std::string_view waveId,float spawnDistance) {
    return waveId=="ridge_ground_battery" && spawnDistance==RidgeBatteryMarker;
}
inline bool UsesMarkedPedestal(std::string_view waveId,float spawnDistance) {
    return waveId==ArchBatteryWave || IsRidgeBattery(waveId,spawnDistance) || IsUpperValleyBattery(waveId,spawnDistance);
}
inline float ActivationLead(std::string_view waveId,float spawnDistance,float lateralOffset) {
    if(waveId==ArchBatteryWave)return lateralOffset<0 ? 70.0f : 30.0f;
    if(IsFirstLiveBattery(waveId))return 25.0f;
    return IsRidgeBattery(waveId,spawnDistance) || IsUpperValleyBattery(waveId,spawnDistance) ? 70.0f : 40.0f;
}
// Start the first shot far enough out for a second full warning during a normal approach.
inline float FirstShotDistance(std::string_view waveId,float spawnDistance) {
    if(waveId==ShelfBatteryWave)return 128.0f;
    if(waveId==ArchBatteryWave)return 94.0f;
    if(IsRidgeBattery(waveId,spawnDistance))return 146.0f;
    return IsFirstLiveBattery(waveId) ? 114.0f : 108.0f;
}
inline constexpr float FirstLiveProjectileSpeed=40.0f;
// Leave enough forward space to see and intercept a side-mounted volley.
// Repeated attacks must budget their warning before reaching this boundary.
inline constexpr float MinimumFireDistance=40.0f;
inline float MinimumFireDistanceFor(std::string_view waveId,float spawnDistance,bool followUp) {
    // The ridge gun's close follow-up needs extra travel for the cart to brake
    // after seeing the release, even when it has just resumed accelerating.
    if(waveId==ArchBatteryWave)return followUp ? 85.0f : 49.0f;
    return followUp && UsesMarkedPedestal(waveId,spawnDistance) ? 60.0f : MinimumFireDistance;
}
}
