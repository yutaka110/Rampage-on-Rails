#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "utils/math/Vector.h"

namespace RailGroundBatteryCharge {
inline constexpr float ReleaseHeat = .68f;
inline constexpr float DissolveSeconds = .15f;
inline constexpr float LaunchBurstSeconds = .12f;
inline constexpr float HeavyLaunchBurstSeconds = .14f;

inline float Smooth(float value, float from, float to) {
    const float t = (std::clamp)((value - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

inline float Growth(float charge) { return Smooth(charge, 0, .80f); }
inline float Heat(float secondsToFire) { return ReleaseHeat * (1 - Smooth(secondsToFire, 0, .16f)); }
inline float FlightHeat(float normalizedAge) {
    return ReleaseHeat + (1 - ReleaseHeat) * Smooth(normalizedAge, 0, .014f);
}

inline float Orbit(float time, uint32_t actorId, uint64_t intent, float heat) {
    // The same clock and attack identity drive the loaded shell and the live round.
    const float orbit = time * 46.0f + float((uint64_t(actorId) * 13 + intent * 7) % 64) +
        5.0f * (std::min)(heat / ReleaseHeat, 1.0f);
    return 2.0f * float(static_cast<unsigned>(std::fmod(orbit, 64.0f)));
}

inline float BodyPhase(float phase, float heat) {
    return phase + 128.0f * float(static_cast<unsigned>(heat * 63.0f + .5f));
}
}
