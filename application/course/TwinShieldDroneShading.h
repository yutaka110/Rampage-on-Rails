#pragma once

#include "utils/math/MathUtils.h"
#include <algorithm>
#include <cstddef>

static_assert(offsetof(Material, padding2) + sizeof(float) == 112);
static_assert(sizeof(Material::padding2) == 16 * sizeof(float));

// Mode 10 reuses the mode-15 material slots without changing the GPU ABI.
// Store world-to-part columns as three float4s; contact shading then follows
// the actual shield transform, including banking and detached wreck pieces.
inline void SetTwinShieldDroneContactShading(Material& material,
        const Matrix4x4& worldInverseTranspose, float side,
        float leftAttachment, float rightAttachment) {
    for (int axis = 0; axis < 3; ++axis)
        for (int component = 0; component < 4; ++component)
            material.padding2[1 + axis * 4 + component] = worldInverseTranspose.m[axis][component];
    material.padding2[13] = side; // Hull: 0; shield: -1 / +1.
    material.padding2[14] = (std::clamp)(leftAttachment, 0.0f, 1.0f);
    material.padding2[15] = (std::clamp)(rightAttachment, 0.0f, 1.0f);
}
