#pragma once
#include "utils/math/Vector.h"

// The shipped hostile orb and its warning share this red-orange energy.
// Keep UI ink saturated; only the small hottest center approaches yellow.
namespace RailEnemyEnergy {
inline constexpr Vector4 Body{1.0f,.20f,.025f,1.0f};
inline constexpr Vector4 Halo{1.5f,.22f,.018f,1.0f};
inline constexpr Vector4 Trail{1.7f,.35f,.035f,1.0f};
inline constexpr Vector4 Hot{2.2f,.90f,.22f,1.0f};
inline constexpr Vector4 Surface{1.8f,.38f,.035f,1.0f};
inline constexpr Vector4 Ink{1.0f,.30f,.035f,1.0f};
inline constexpr Vector4 HotInk{1.0f,.66f,.20f,1.0f};
inline Vector4 Alpha(Vector4 color,float alpha) {color.w=alpha;return color;}
}
