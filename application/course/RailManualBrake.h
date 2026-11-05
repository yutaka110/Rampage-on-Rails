#pragma once

#include <algorithm>
#include <cmath>

// Value state is copied by travel forecasts and retry checkpoints with the speed director.
class RailManualBrake final {
public:
    static constexpr float MinimumMultiplier = .60f;
    static constexpr float AttackSeconds = .30f;
    static constexpr float ReleaseSeconds = .45f;

    void Reset() { *this = {}; }
    void SetInput(bool held, bool enabled) { held_ = held && enabled; }
    void Update(float dt) {
        if(!std::isfinite(dt) || dt<=0) return;
        dt=(std::min)(dt,.25f);
        blend_=held_ ? (std::min)(1.0f,blend_+dt/AttackSeconds)
                     : (std::max)(0.0f,blend_-dt/ReleaseSeconds);
    }
    float Blend() const { return blend_*blend_*(3.0f-2.0f*blend_); }
    bool Held() const { return held_; }

private:
    float blend_ = 0.0f;
    bool held_ = false;
};
