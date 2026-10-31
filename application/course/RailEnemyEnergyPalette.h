#pragma once
#include "utils/math/Vector.h"
#include <string_view>

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

enum class BatteryKind : unsigned char { None, Gatling, Heavy };
struct Palette {Vector4 body,halo,trail,hot,surface,ink,hotInk;};
inline constexpr Palette Ordinary{Body,Halo,Trail,Hot,Surface,Ink,HotInk};
inline constexpr Palette Gatling{
    {1.0f,.34f,.025f,1},{1.65f,.42f,.035f,1},{1.8f,.48f,.04f,1},
    {3.2f,2.65f,1.25f,1},{2.0f,.65f,.08f,1},{1,.40f,.035f,1},{1,.78f,.32f,1}};
inline constexpr Palette Heavy{
    {.84f,.025f,.56f,1},{1.35f,.045f,.92f,1},{1.7f,.08f,1.1f,1},
    {3.1f,2.55f,3.0f,1},{1.65f,.08f,1.2f,1},{.95f,.07f,.67f,1},{1,.56f,.88f,1}};
inline BatteryKind KindForProjectile(std::string_view id) {
    return id=="enemy_mothership_rapid"?BatteryKind::Gatling:id=="enemy_mothership_heavy"?BatteryKind::Heavy:BatteryKind::None;
}
inline const Palette& For(BatteryKind kind) {return kind==BatteryKind::Gatling?Gatling:kind==BatteryKind::Heavy?Heavy:Ordinary;}
inline const Palette& ForActor(std::string_view id) {
    return id=="short_mothership_rapid"?Gatling:id=="short_mothership_heavy"?Heavy:Ordinary;
}
}
