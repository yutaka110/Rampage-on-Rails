#pragma once

#include <algorithm>
#include <cmath>

// Copied with the speed director so warnings forecast the same unlimited
// held boost, ramp and release as actual travel. Future input changes are never assumed.
class RailManualBoost final {
public:
    static constexpr float SpeedGain = 1.0f;
    static constexpr float MaximumMultiplier = 1.0f + SpeedGain;
    static constexpr float MinimumBoostSpeed = 40.0f;
    static constexpr float AttackSeconds = 0.22f;
    static constexpr float AccelerationMultiplier = 1.9f;
    static constexpr float MaximumJerk = 240.0f;
    static constexpr float IgnitionSeconds = 0.30f;
    static constexpr float ReleaseSeconds = 0.70f;

    void Reset() { *this = RailManualBoost{}; }
    void SetInput(bool held, bool enabled) { held_ = held; enabled_ = enabled; }
    void Update(float deltaTime) {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
        startedThisFrame_ = false;
        float remaining = (std::min)(deltaTime, 0.25f);
        while (remaining > 0.000001f) {
            const float dt = (std::min)(remaining, 1.0f / 120.0f);
            const bool active = enabled_ && held_;
            if (active && !wasActive_) {
                startedThisFrame_ = true;
                ignitionAge_ = 0.0f;
            }
            wasActive_ = active;
            ignitionAge_ = (std::min)(IgnitionSeconds, ignitionAge_ + dt);
            blend_ = active ? (std::min)(1.0f, blend_ + dt / AttackSeconds)
                            : (std::max)(0.0f, blend_ - dt / ReleaseSeconds);
            remaining -= dt;
        }
    }
    float SpeedMultiplier() const { return 1.0f + SpeedGain * Blend(); }
    static float ClampMultiplier(float value) {
        return std::isfinite(value) ? (std::clamp)(value, 1.0f, MaximumMultiplier) : 1.0f;
    }
    float Blend() const { return blend_ * blend_ * (3.0f - 2.0f * blend_); }
    static float RequestedSpeed(float normalSpeed, float multiplier) {
        if (!std::isfinite(normalSpeed) || normalSpeed <= 0.0f) return 0.0f;
        const float boost = ClampMultiplier(multiplier);
        const float blend = (boost - 1.0f) / SpeedGain;
        // Lift low-speed sections progressively; stops and curve limits
        // remain owned by the shared motion envelope and movement.
        return (std::max)(normalSpeed * boost,
            normalSpeed + (MinimumBoostSpeed - normalSpeed) * blend);
    }
    bool StartedThisFrame() const { return startedThisFrame_; }
    float IgnitionPulse() const {
        if (!enabled_ || !held_) return 0.0f;
        const float t = (std::clamp)(ignitionAge_ / IgnitionSeconds, 0.0f, 1.0f);
        return (1.0f - t) * (1.0f - t);
    }
    // Curves, collisions and speed caps must also suppress the sensation of
    // acceleration: effects follow the speed actually achieved, not the key.
    float PresentationStrength(float speed, float normalSpeed) const {
        if (!std::isfinite(speed) || !std::isfinite(normalSpeed) || normalSpeed < 1.0f) return 0.0f;
        const float extraSpeed = RequestedSpeed(normalSpeed, MaximumMultiplier) - normalSpeed;
        return (std::min)(Blend(), (std::clamp)(
            (speed - normalSpeed) / (std::max)(1.0f, extraSpeed), 0.0f, 1.0f));
    }

private:
    float blend_ = 0.0f;
    float ignitionAge_ = IgnitionSeconds;
    bool wasActive_ = false;
    bool startedThisFrame_ = false;
    bool held_ = false;
    bool enabled_ = false;
};
