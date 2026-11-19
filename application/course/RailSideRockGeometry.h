#pragma once
#include "utils/math/Vector.h"
#include <array>
namespace RailSideRockGeometry {
inline constexpr std::array<Vector3,8> Pivots{{
    Vector3{0.4440774f,0.3828690f,0.4217106f},
    Vector3{0.3328384f,0.4765530f,-0.2979620f},
    Vector3{0.6129038f,-0.4833418f,0.3434681f},
    Vector3{0.4099059f,-0.4160818f,-0.4941653f},
    Vector3{-0.2999388f,0.1684919f,0.5925508f},
    Vector3{-0.4934783f,0.2803056f,-0.2183032f},
    Vector3{-0.2737691f,-0.5919153f,0.5651493f},
    Vector3{-0.4154098f,-0.4764105f,-0.4234116f},
}};
}
