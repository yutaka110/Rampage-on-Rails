#pragma once
#include <string_view>

namespace RailHeavy {
inline bool IsWeapon(std::string_view id){return id=="short_mothership_heavy";}
inline bool IsProjectile(std::string_view id){return id=="enemy_mothership_heavy";}
inline constexpr int InterceptLocks=2;
inline constexpr float LockStackSeconds=.22f;
}
