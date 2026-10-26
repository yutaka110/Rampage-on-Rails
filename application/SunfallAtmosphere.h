#pragma once

#include <algorithm>
#include <cmath>
#include "utils/math/MathUtils.h"

// Original game implementation inspired by Sunfall's light/dust composition.
// The terrain and its PBR materials remain the authoritative scene geometry.
struct SunfallAtmosphereSettings {
    bool enabled = true;
    float strength = 0.85f;
    float density = 0.0028f;
    float maxDistance = 200.0f;
    float heightFalloff = 32.0f;
    float windSpeed = 0.9f;
    float skyFill = 1.15f;
};

inline float SunfallOutdoorBlend(float distance,float start,float length,float strength) {
    if(!std::isfinite(distance) || !std::isfinite(start) || !std::isfinite(length) ||
        !std::isfinite(strength)) return 0.0f;
    const float t=(std::clamp)((distance-start)/(std::max)(length,1.0f),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t)*(std::clamp)(strength,0.0f,1.0f);
}

struct SunfallFrameConstants {
    Matrix4x4 inverseViewProjection{};
    Matrix4x4 lightViewProjection{};
    Vector4 cameraAndTime{};
    Vector4 sunDirectionAndStrength{};
    Vector4 sunColorAndDensity{};
    Vector4 rangeHeightAndGround{};
    Vector4 windAndShadow{};
};
static_assert(sizeof(SunfallFrameConstants) == 52 * sizeof(float));

class SunfallExposure final {
public:
    float Update(float deltaTime, float sunIntensity, bool active) {
        if(!active) { exposure_=1.0f; return exposure_; }
        if(!std::isfinite(sunIntensity)) sunIntensity=2.0f;
        const float daylight=(std::clamp)((sunIntensity-1.6f)/1.4f,0.0f,1.0f);
        // Keep daylight at neutral exposure. Highlight compression and the
        // display transfer handle bright skies without darkening every shadow.
        const float target=1.08f-0.08f*daylight*daylight*(3.0f-2.0f*daylight);
        const float dt=std::isfinite(deltaTime) ? (std::clamp)(deltaTime,0.0f,0.05f) : 0.0f;
        exposure_+=(target-exposure_)*(1.0f-std::exp(-dt/0.70f));
        return exposure_;
    }
private:
    float exposure_=1.0f;
};
