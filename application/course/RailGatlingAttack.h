#pragma once
#include <string_view>
#include "RailPulseShotPresentation.h"

// The carrier's rapid battery owns one admitted burst. These
// timings are shared by execution, the rotating barrel and the encounter rule.
namespace RailGatling {
inline constexpr int Rounds = 24;
// A pulse can disperse the adjacent round in the same stream. The small
// contact volume compensates for 20 incoming rounds/s versus 12 pulses/s.
inline constexpr float ClashRadius = 3.5f;
inline constexpr float PulseContactRadius = RailPulseShot::ContactRadius;
inline constexpr float ShotInterval = .05f;
inline constexpr float BurstSeconds = (Rounds - 1) * ShotInterval;
// A fixed point along the incoming stream, separated from the gun itself.
inline constexpr float GuardSampleSeconds = .60f;
// A newly emitted round can be intercepted individually. Once the stream
// clears the muzzle, a clash can spread to its adjacent round. This makes
// guarding the incoming stream more effective than camping the gun's face.
inline constexpr float ClashArmSeconds = .25f;
inline bool IsWeapon(std::string_view actorAssetId) { return actorAssetId == "short_mothership_rapid"; }
inline bool IsProjectile(std::string_view definitionId) { return definitionId == "enemy_mothership_rapid"; }
}
