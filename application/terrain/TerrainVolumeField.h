#pragma once

#include "RailPath.h"
#include "TerrainGenerationSettings.h"
#include "TerrainEditLayer.h"
#include "utils/math/Vector.h"

struct TerrainVolumeLocalSample {
    float sdf = 0.0f;
    float noise = 0.0f;
    float archMask = 0.0f;
    float carveMask = 0.0f;
    float openingMask = 0.0f;
    float openCanyonBlend = 0.0f;
    float radiusScale = 1.0f;
};

class TerrainVolumeField {
public:
    TerrainVolumeField(
        const RailPath& railPath,
        const TerrainGenerationSettings& settings,
        const TerrainEditLayer* edits = nullptr,
        const TerrainEditLayer* preview = nullptr);

    TerrainVolumeLocalSample SampleLocal(
        float distance,
        float lateral,
        float vertical) const;
    Vector3 SurfacePoint(
        float distance,
        float angle,
        Vector3* outNormal = nullptr) const;
    // Exact zero contour of SampleLocal, including the edited-radius clamp.
    // The render surface offsets edits along a different radial direction;
    // collision must preserve the existing authoritative SDF definition.
    Vector3 CollisionSurfacePoint(float distance, float angle,
        const RailPathSample& pathSample) const;
    float OpeningMask(float distance, float angle) const;
    float OpenCanyonBlend(float distance) const;
    float PaintVariation(float distance, float angle) const;
    Vector3 EstimateNormal(
        float distance,
        float lateral,
        float vertical) const;

private:
    float Noise3(float distance, float lateral, float vertical, float scale) const;
    float ArchMask(float distance, float angle) const;
    float SubtractiveCarveMask(float distance, float angle) const;
    float RadiusScale(float distance, float angle) const;

    const RailPath& railPath_;
    const TerrainGenerationSettings& settings_;
    const TerrainEditLayer* edits_ = nullptr;
    const TerrainEditLayer* preview_ = nullptr;
};
