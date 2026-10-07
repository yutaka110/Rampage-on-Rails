#include "TerrainVolumeField.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(const Vector3& v) {
    return std::sqrt(Dot(v, v));
}

Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
    const float len = Length(v);
    if (len <= 0.00001f) {
        return fallback;
    }
    return Scale(v, 1.0f / len);
}

float Hash31(float x, float y, float z, uint32_t seed) {
    float p = x * 127.1f + y * 311.7f + z * 74.7f + static_cast<float>(seed) * 0.013f;
    return std::sin(p) * 43758.5453123f - std::floor(std::sin(p) * 43758.5453123f);
}

float ValueNoise(float x, float y, float z, uint32_t seed) {
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float iz = std::floor(z);
    float fx = x - ix;
    float fy = y - iy;
    float fz = z - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    fz = fz * fz * (3.0f - 2.0f * fz);

    auto h = [&](float dx, float dy, float dz) {
        return Hash31(ix + dx, iy + dy, iz + dz, seed);
    };
    const float x00 = std::lerp(h(0, 0, 0), h(1, 0, 0), fx);
    const float x10 = std::lerp(h(0, 1, 0), h(1, 1, 0), fx);
    const float x01 = std::lerp(h(0, 0, 1), h(1, 0, 1), fx);
    const float x11 = std::lerp(h(0, 1, 1), h(1, 1, 1), fx);
    const float y0 = std::lerp(x00, x10, fy);
    const float y1 = std::lerp(x01, x11, fy);
    return std::lerp(y0, y1, fz);
}

float SmoothStep(float edge0, float edge1, float value) {
    const float t = (std::clamp)((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

TerrainVolumeField::TerrainVolumeField(
    const RailPath& railPath,
    const TerrainGenerationSettings& settings,
    const TerrainEditLayer* edits,
    const TerrainEditLayer* preview)
    : railPath_(railPath),
      settings_(settings),
      edits_(edits),
      preview_(preview) {
}

float TerrainVolumeField::Noise3(float distance, float lateral, float vertical, float scale) const {
    const float safeScale = (std::max)(scale, 0.001f);
    const float x = lateral * safeScale;
    const float y = vertical * safeScale;
    const float z = distance * safeScale;
    const float n0 = ValueNoise(x, y, z, settings_.seed);
    const float n1 = ValueNoise(x * 2.07f + 17.0f, y * 2.07f, z * 2.07f, settings_.seed + 101u);
    const float n2 = ValueNoise(x * 4.11f, y * 4.11f + 29.0f, z * 4.11f, settings_.seed + 211u);
    return (n0 * 0.58f + n1 * 0.30f + n2 * 0.12f) * 2.0f - 1.0f;
}

float TerrainVolumeField::ArchMask(float distance, float angle) const {
    if (settings_.archDensity <= 0.0f || settings_.volumeArchScale <= 0.0f) {
        return 0.0f;
    }

    const float cellLength = (std::max)(settings_.chunkLength * 1.65f, 40.0f);
    const float cell = std::floor(distance / cellLength);
    const float local = distance / cellLength - cell;
    const float gate = ValueNoise(cell, 8.0f, 3.0f, settings_.seed + 503u);
    if (gate > settings_.archDensity) {
        return 0.0f;
    }

    const float center = 0.28f + ValueNoise(cell, 2.0f, 4.0f, settings_.seed + 607u) * 0.44f;
    const float distanceMask = 1.0f - (std::min)(std::abs(local - center) / 0.24f, 1.0f);
    const float side = ValueNoise(cell, 5.0f, 9.0f, settings_.seed + 701u) < 0.5f ? -1.0f : 1.0f;
    const float targetAngle = side < 0.0f ? kPi : 0.0f;
    float angleDelta = std::abs(angle - targetAngle);
    angleDelta = (std::min)(angleDelta, kTau - angleDelta);
    const float angleMask = 1.0f - (std::min)(angleDelta / 0.72f, 1.0f);
    return distanceMask * distanceMask * angleMask * angleMask * settings_.volumeArchScale;
}

float TerrainVolumeField::SubtractiveCarveMask(float distance, float angle) const {
    if (settings_.sdfCarveDensity <= 0.0f || settings_.sdfCarveStrength <= 0.0f) {
        return 0.0f;
    }

    const float scale = (std::max)(settings_.sdfCarveScale, 0.25f);
    const float cellLength = (std::max)(settings_.chunkLength * (0.42f / scale), 18.0f);
    const float cell = std::floor(distance / cellLength);
    const float local = distance / cellLength - cell;
    float mask = 0.0f;

    for (int32_t octave = 0; octave < 2; ++octave) {
        const float octaveCell = cell + static_cast<float>(octave);
        const float gate = ValueNoise(octaveCell, 13.0f, 29.0f, settings_.seed + 1703u + static_cast<uint32_t>(octave) * 97u);
        if (gate > settings_.sdfCarveDensity) {
            continue;
        }

        const float center = 0.18f + ValueNoise(octaveCell, 19.0f, 31.0f, settings_.seed + 1811u) * 0.64f;
        const float halfLength = 0.12f + ValueNoise(octaveCell, 23.0f, 37.0f, settings_.seed + 1901u) * 0.16f;
        const float sideGate = ValueNoise(octaveCell, 41.0f, 43.0f, settings_.seed + 1999u);
        const float targetAngle =
            sideGate < 0.34f ? kPi * 0.5f :
            (sideGate < 0.67f ? 0.0f : kPi);
        const float angleWidth =
            targetAngle == kPi * 0.5f
                ? (0.34f + ValueNoise(octaveCell, 47.0f, 53.0f, settings_.seed + 2081u) * 0.32f)
                : (0.22f + ValueNoise(octaveCell, 59.0f, 61.0f, settings_.seed + 2131u) * 0.26f);

        const float along = 1.0f - (std::min)(std::abs(local - center) / halfLength, 1.0f);
        float angleDelta = std::abs(angle - targetAngle);
        angleDelta = (std::min)(angleDelta, kTau - angleDelta);
        const float angular = 1.0f - (std::min)(angleDelta / angleWidth, 1.0f);
        const float chipNoise = 0.72f + Noise3(
            distance + static_cast<float>(octave) * 31.0f,
            std::cos(angle) * settings_.canyonHalfWidth,
            std::sin(angle) * settings_.wallHeight,
            0.16f) * 0.28f;
        const float cellMask = SmoothStep(0.0f, 1.0f, along) *
            SmoothStep(0.0f, 1.0f, angular) *
            (std::max)(0.45f, chipNoise);
        mask = (std::max)(mask, cellMask);
    }

    const float ceilingBias = SmoothStep(0.08f, 0.82f, std::sin(angle));
    const float wallBias = SmoothStep(0.36f, 0.96f, std::abs(std::cos(angle))) * 0.72f;
    const float placementBias = (std::max)(ceilingBias, wallBias);
    return (std::clamp)(mask * placementBias * settings_.sdfCarveStrength, 0.0f, 1.2f);
}

float TerrainVolumeField::OpenCanyonBlend(float distance) const {
    const float strength = (std::clamp)(settings_.openCanyonStrength, 0.0f, 1.0f);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    const float transitionLength = (std::max)(settings_.openCanyonTransitionLength, 1.0f);
    const float blend = SmoothStep(
        settings_.openCanyonStartDistance,
        settings_.openCanyonStartDistance + transitionLength,
        distance);
    return blend * strength;
}

float TerrainVolumeField::OpeningMask(float distance, float angle) const {
    const float strength = (std::clamp)(settings_.openingSilhouetteStrength, 0.0f, 1.5f);
    const float openCanyonBlend = OpenCanyonBlend(distance);
    if (strength <= 0.0f && openCanyonBlend <= 0.0f) {
        return 0.0f;
    }

    const float scale = (std::max)(settings_.openingSilhouetteScale, 0.35f);
    const float cellLength = (std::max)(settings_.chunkLength * (1.95f / scale), 92.0f);
    const float cell = std::floor(distance / cellLength);
    const float local = distance / cellLength - cell;
    float mask = 0.0f;

    for (int32_t octave = 0; octave < 2; ++octave) {
        const float octaveCell = cell + static_cast<float>(octave) * 0.37f;
        const float gate = ValueNoise(
            octaveCell,
            71.0f,
            11.0f,
            settings_.seed + 2609u + static_cast<uint32_t>(octave) * 109u);
        const float gateThreshold = 0.34f + strength * 0.38f;
        if (gate > gateThreshold) {
            continue;
        }

        const float center = 0.30f + ValueNoise(octaveCell, 79.0f, 17.0f, settings_.seed + 2711u) * 0.42f;
        const float halfLength =
            0.18f + ValueNoise(octaveCell, 83.0f, 23.0f, settings_.seed + 2801u) * (0.16f + strength * 0.05f);
        const float sideGate = ValueNoise(octaveCell, 89.0f, 31.0f, settings_.seed + 2903u);
        const float targetAngle =
            sideGate < 0.42f ? kPi * 0.50f :
            (sideGate < 0.70f ? kPi * 0.22f : kPi * 0.78f);
        const float angleWidth =
            0.38f + ValueNoise(octaveCell, 97.0f, 37.0f, settings_.seed + 3001u) * (0.32f + strength * 0.12f);

        const float along = 1.0f - (std::min)(std::abs(local - center) / halfLength, 1.0f);
        float angleDelta = std::abs(angle - targetAngle);
        angleDelta = (std::min)(angleDelta, kTau - angleDelta);
        const float angular = 1.0f - (std::min)(angleDelta / angleWidth, 1.0f);
        const float ceilingBias = SmoothStep(0.10f, 0.76f, std::sin(angle));
        const float upperWallBias = SmoothStep(0.18f, 0.92f, std::abs(std::cos(angle))) *
            SmoothStep(-0.18f, 0.52f, std::sin(angle)) * 0.72f;
        const float placementBias = (std::max)(ceilingBias, upperWallBias);
        const float raggedEdge =
            0.76f + Noise3(
                distance + static_cast<float>(octave) * 43.0f,
                std::cos(angle) * settings_.canyonHalfWidth,
                std::sin(angle) * settings_.wallHeight,
                0.075f) * 0.24f;
        const float cellMask =
            SmoothStep(0.0f, 1.0f, along) *
            SmoothStep(0.0f, 1.0f, angular) *
            placementBias *
            (std::max)(0.35f, raggedEdge);
        mask = (std::max)(mask, cellMask);
    }

    mask *= strength;

    if (openCanyonBlend > 0.0f) {
        const float skyAngle = std::sin(angle);
        const float sideAngle = std::abs(std::cos(angle));
        const float topOpen = SmoothStep(0.02f, 0.48f, skyAngle);
        const float crownOpen = SmoothStep(0.34f, 0.86f, skyAngle);
        const float wallKeep = 1.0f -
            SmoothStep(0.70f, 0.98f, sideAngle) *
            SmoothStep(-0.02f, 0.58f, skyAngle) *
            0.58f;
        const float raggedSkyline =
            0.80f + Noise3(
                distance * 0.72f + std::cos(angle) * 29.0f,
                std::cos(angle) * settings_.canyonHalfWidth,
                std::sin(angle) * settings_.wallHeight,
                0.052f) * 0.20f;
        const float overheadOpening =
            (topOpen * 0.72f + crownOpen * 0.42f) *
            wallKeep *
            (std::max)(0.42f, raggedSkyline) *
            (1.05f + openCanyonBlend * 0.22f);
        mask = (std::max)(mask, overheadOpening * openCanyonBlend);
    }

    return (std::clamp)(mask, 0.0f, 1.35f);
}

float TerrainVolumeField::RadiusScale(float distance, float angle) const {
    const float lateral = std::cos(angle) * settings_.canyonHalfWidth;
    const float verticalBase = std::sin(angle) >= 0.0f ? settings_.wallHeight : settings_.corridorRadius * 0.92f;
    const float vertical = std::sin(angle) * verticalBase;
    const float erosionStrength = (std::clamp)(settings_.motherRockErosionStrength, 0.0f, 1.5f);
    const float largeErosionStrength = (std::clamp)(settings_.largeScaleErosionStrength, 0.0f, 1.5f);
    const float breakupDensity = (std::clamp)(settings_.surfaceBreakupDensity, 0.0f, 1.5f);
    const float n = Noise3(distance, lateral, vertical, 0.025f);
    const float ceilingMask = SmoothStep(0.22f, 0.86f, std::sin(angle));
    const float wallMask = SmoothStep(0.18f, 0.96f, std::abs(std::cos(angle)));
    const float motherMask = (std::max)(wallMask, ceilingMask * 0.82f);
    const float ribNoise = Noise3(distance + std::sin(angle) * 19.0f, lateral * 0.45f, vertical, 0.055f);
    const float fractureNoise = Noise3(distance + 37.0f, lateral, vertical * 1.7f, 0.105f);
    const float verticalGrooveNoise = Noise3(
        distance * 0.34f + std::cos(angle) * 27.0f,
        lateral * 0.08f,
        vertical * 0.34f,
        0.19f);
    const float longCutNoise = Noise3(
        distance + std::sin(angle) * 43.0f,
        lateral * 0.62f,
        vertical * 0.16f,
        0.038f);
    const float macroPocketNoise = Noise3(
        distance + std::cos(angle) * 87.0f,
        lateral * 0.26f + vertical * 0.11f,
        vertical * 0.19f - distance * 0.05f,
        0.018f);
    const float diagonalCutNoise = Noise3(
        distance * 0.72f + vertical * 0.34f,
        lateral * 0.20f + distance * 0.08f,
        vertical * 0.24f + std::cos(angle) * 31.0f,
        0.032f);
    const float ledgeCoord =
        vertical * 0.115f +
        distance * 0.018f +
        Noise3(distance, lateral * 0.25f, vertical, 0.045f) * 0.42f;
    const float ledgeBand = std::abs((ledgeCoord - std::floor(ledgeCoord)) - 0.5f);
    const float verticalCrack =
        wallMask * erosionStrength *
        SmoothStep(0.42f, 0.92f, std::abs(verticalGrooveNoise)) *
        (verticalGrooveNoise < 0.0f ? -0.18f : 0.12f);
    const float erodedLedge =
        (wallMask * 0.72f + ceilingMask * 0.52f) * erosionStrength *
        SmoothStep(0.50f, 0.96f, std::abs(longCutNoise)) *
        (longCutNoise < 0.0f ? -0.13f : 0.08f);
    const float ceilingBreak =
        ceilingMask * settings_.volumeRoughness *
        (ribNoise * 0.34f + fractureNoise * 0.18f);
    const float sideLedge =
        wallMask * settings_.volumeRoughness *
        (std::max)(0.0f, fractureNoise) * 0.28f;
    const float macroPocket =
        motherMask * largeErosionStrength *
        SmoothStep(0.54f, 0.96f, macroPocketNoise) *
        (0.22f + SmoothStep(0.20f, 0.88f, std::abs(diagonalCutNoise)) * 0.18f);
    const float diagonalShear =
        motherMask * largeErosionStrength *
        SmoothStep(0.60f, 0.96f, std::abs(diagonalCutNoise)) *
        (diagonalCutNoise < 0.0f ? -0.20f : 0.26f);
    const float brokenTerrace =
        wallMask * largeErosionStrength * (0.74f + breakupDensity * 0.36f) *
        SmoothStep(0.12f + breakupDensity * 0.015f, 0.0f, ledgeBand) *
        SmoothStep(0.34f - breakupDensity * 0.09f, 0.92f, std::abs(fractureNoise)) *
        (fractureNoise < 0.0f ? -0.14f - breakupDensity * 0.035f : 0.20f + breakupDensity * 0.045f);
    const float chippedStrata =
        motherMask * largeErosionStrength * breakupDensity *
        SmoothStep(0.44f, 0.92f, std::abs(fractureNoise + longCutNoise * 0.55f)) *
        Noise3(distance * 1.8f + vertical * 0.37f, lateral * 1.45f, vertical * 0.82f, 0.145f) *
        0.18f;
    const float roughness = settings_.volumeRoughness * n;
    const float subtractiveCarve = SubtractiveCarveMask(distance, angle);
    const float openingMask = OpeningMask(distance, angle);
    const float openCanyonBlend = OpenCanyonBlend(distance);
    const float openingLip =
        openingMask *
        (0.92f + settings_.volumeRoughness * 0.42f + settings_.largeScaleErosionStrength * 0.28f);
    const float openCanyonCliff =
        openCanyonBlend *
        wallMask *
        (0.28f + largeErosionStrength * 0.24f) *
        (0.72f + SmoothStep(0.20f, 0.88f, std::abs(verticalGrooveNoise)) * 0.36f);
    const float openCanyonCeilingRelease =
        openCanyonBlend *
        ceilingMask *
        (0.08f + settings_.volumeRoughness * 0.08f);
    const float chippedEdge =
        subtractiveCarve *
        Noise3(distance + 91.0f, lateral * 1.9f, vertical * 1.25f, 0.23f) *
        0.12f;
    return (std::max)(
        0.52f,
        1.0f + roughness + ceilingBreak + sideLedge +
            macroPocket + diagonalShear + brokenTerrace +
            chippedStrata + verticalCrack + erodedLedge +
            ArchMask(distance, angle) * (1.0f - openCanyonBlend * 0.70f) +
            subtractiveCarve + openingLip + chippedEdge +
            openCanyonCliff - openCanyonCeilingRelease);
}

TerrainVolumeLocalSample TerrainVolumeField::SampleLocal(
    float distance,
    float lateral,
    float vertical) const {
    const float openCanyonBlend = OpenCanyonBlend(distance);
    const float lateralRadius =
        (std::max)(settings_.canyonHalfWidth, settings_.corridorRadius + 4.0f) *
        std::lerp(1.0f, 1.38f, openCanyonBlend);
    const float verticalRadius =
        vertical >= 0.0f
            ? (std::max)(settings_.wallHeight, settings_.corridorRadius + 4.0f) *
                std::lerp(1.0f, 1.58f, openCanyonBlend)
            : (std::max)(settings_.corridorRadius * 0.92f, 4.0f) *
                std::lerp(1.0f, 1.08f, openCanyonBlend);
    const float angle = std::atan2(vertical / verticalRadius, lateral / lateralRadius);
    const float radiusScale = RadiusScale(distance, angle);
    float radialOffset = 0.0f;
    if (edits_ != nullptr) radialOffset += edits_->Evaluate(distance, angle).radialOffset;
    if (preview_ != nullptr) radialOffset += preview_->Evaluate(distance, angle).radialOffset;
    const float meanRadius = std::sqrt(
        lateralRadius * lateralRadius + verticalRadius * verticalRadius) * 0.70710678f;
    const float editedRadiusScale = (std::max)(0.20f, radiusScale + radialOffset / meanRadius);
    const float nx = lateral / (lateralRadius * editedRadiusScale);
    const float ny = vertical / (verticalRadius * editedRadiusScale);
    TerrainVolumeLocalSample sample{};
    sample.noise = Noise3(distance, lateral, vertical, 0.025f);
    sample.archMask = ArchMask(distance, angle);
    sample.carveMask = SubtractiveCarveMask(distance, angle);
    sample.openingMask = OpeningMask(distance, angle);
    sample.openCanyonBlend = openCanyonBlend;
    sample.radiusScale = radiusScale;
    sample.sdf = std::sqrt(nx * nx + ny * ny) - 1.0f;
    return sample;
}

Vector3 TerrainVolumeField::SurfacePoint(
    float distance,
    float angle,
    Vector3* outNormal) const {
    const RailPathSample pathSample = railPath_.Evaluate(distance);
    const float openCanyonBlend = OpenCanyonBlend(distance);
    const float lateralRadius =
        (std::max)(settings_.canyonHalfWidth, settings_.corridorRadius + 4.0f) *
        std::lerp(1.0f, 1.38f, openCanyonBlend);
    const float verticalBase = std::sin(angle) >= 0.0f
        ? (std::max)(settings_.wallHeight, settings_.corridorRadius + 4.0f) *
            std::lerp(1.0f, 1.58f, openCanyonBlend)
        : (std::max)(settings_.corridorRadius * 0.92f, 4.0f) *
            std::lerp(1.0f, 1.08f, openCanyonBlend);
    const float radiusScale = RadiusScale(distance, angle);
    float radialOffset = 0.0f;
    if (edits_ != nullptr) radialOffset += edits_->Evaluate(distance, angle).radialOffset;
    if (preview_ != nullptr) radialOffset += preview_->Evaluate(distance, angle).radialOffset;
    const float lateral = std::cos(angle) * lateralRadius * radiusScale;
    const float vertical = std::sin(angle) * verticalBase * radiusScale;
    if (outNormal != nullptr) {
        *outNormal = EstimateNormal(distance, lateral, vertical);
    }
    const Vector3 radialDirection = NormalizeOr(
        Add(Scale(pathSample.right, std::cos(angle)),
            Scale(pathSample.up, std::sin(angle))),
        pathSample.up);
    return Add(
        pathSample.position,
        Add(
            Add(Scale(pathSample.right, lateral), Scale(pathSample.up, vertical)),
            Scale(radialDirection, radialOffset)));
}

Vector3 TerrainVolumeField::CollisionSurfacePoint(
    float distance, float angle, const RailPathSample& pathSample) const {
    const float blend = OpenCanyonBlend(distance);
    const float lateralRadius = (std::max)(settings_.canyonHalfWidth,
        settings_.corridorRadius + 4.0f) * std::lerp(1.0f, 1.38f, blend);
    const float verticalRadius = std::sin(angle) >= 0.0f
        ? (std::max)(settings_.wallHeight, settings_.corridorRadius + 4.0f) * std::lerp(1.0f, 1.58f, blend)
        : (std::max)(settings_.corridorRadius * 0.92f, 4.0f) * std::lerp(1.0f, 1.08f, blend);
    float offset = 0.0f;
    if (edits_) offset += edits_->Evaluate(distance, angle).radialOffset;
    if (preview_) offset += preview_->Evaluate(distance, angle).radialOffset;
    const float meanRadius = std::sqrt(lateralRadius*lateralRadius + verticalRadius*verticalRadius) * 0.70710678f;
    const float scale = (std::max)(0.20f, RadiusScale(distance, angle) + offset/meanRadius);
    return Add(pathSample.position, Add(
        Scale(pathSample.right, std::cos(angle)*lateralRadius*scale),
        Scale(pathSample.up, std::sin(angle)*verticalRadius*scale)));
}

float TerrainVolumeField::PaintVariation(float distance, float angle) const {
    TerrainEditEvaluation evaluation{};
    if (edits_ != nullptr) {
        const TerrainEditEvaluation authored = edits_->Evaluate(distance, angle);
        for (uint32_t index = 0; index < 4u; ++index) {
            evaluation.paintWeights[index] += authored.paintWeights[index];
        }
    }
    if (preview_ != nullptr) {
        const TerrainEditEvaluation transient = preview_->Evaluate(distance, angle);
        for (uint32_t index = 0; index < 4u; ++index) {
            evaluation.paintWeights[index] += transient.paintWeights[index];
        }
    }
    return evaluation.MaterialVariation();
}

Vector3 TerrainVolumeField::EstimateNormal(float distance, float lateral, float vertical) const {
    constexpr float e = 0.75f;
    const float sx = SampleLocal(distance, lateral + e, vertical).sdf -
        SampleLocal(distance, lateral - e, vertical).sdf;
    const float sy = SampleLocal(distance, lateral, vertical + e).sdf -
        SampleLocal(distance, lateral, vertical - e).sdf;
    const RailPathSample sample = railPath_.Evaluate(distance);
    return NormalizeOr(
        Add(Scale(sample.right, sx), Scale(sample.up, sy)),
        sample.up);
}
