#pragma once

#include "utils/math/MathUtils.h"
#include <algorithm>
#include <cstddef>

static_assert(offsetof(Material, padding2) + sizeof(float) == 112);
static_assert(sizeof(Material) >= 172); // Rail origin / distance and distance gradient.

// Preserve the original outdoor material. Mode 15 blends a separate cave
// surface into it using the same world-space opening probes as the terrain.
inline Material WithCaveSurfaceLighting(Material material, Vector3 albedo,
    float roughness, float metalness, float contactVisibility = 1.0f) {
    material.specularMode = 15;
    material.padding2[1] = albedo.x;
    material.padding2[2] = albedo.y;
    material.padding2[3] = albedo.z;
    material.padding2[4] = std::clamp(roughness, 0.35f, 0.90f);
    material.padding2[5] = std::clamp(metalness, 0.0f, 1.0f);
    material.padding2[6] = std::clamp(contactVisibility, 0.0f, 1.0f);
    for (int index = 7; index < 16; ++index) material.padding2[index] = 0.0f;
    return material;
}

// Bind the baked course coordinate to this static part. Projecting onto the
// camera tangent makes curved track change brightness as the camera advances.
inline Material WithContinuousRailLighting(Material material, const Matrix4x4& world,
    float startDistance, float endDistance) {
    material.padding2[7] = 1.0f;
    material.padding2[9] = world.m[3][0];
    material.padding2[10] = world.m[3][1];
    material.padding2[11] = world.m[3][2];
    material.padding2[12] = (startDistance + endDistance) * 0.5f;
    const float lengthSquared = world.m[2][0] * world.m[2][0] +
        world.m[2][1] * world.m[2][1] + world.m[2][2] * world.m[2][2];
    const float scale = lengthSquared > 0.000001f
        ? (endDistance - startDistance) / lengthSquared : 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        material.padding2[13 + axis] = world.m[2][axis] * scale;
    return material;
}
