#pragma once

#include "utils/math/Vector.h"

// Shared visual pose; does not own a spawn, collision body or combat state.
struct TwinShieldDronePose final {
    bool visible = false;
    Vector3 position{};
    Vector3 rotation{};
    Vector3 scale{1,1,1};
    float alpha = 1.0f;
    float charge = 0.0f;
    float flash = 0.0f;
    float death = 0.0f;
};
