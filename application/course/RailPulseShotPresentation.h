#pragma once

#include "utils/math/Vector.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Fast world-space tracers for the responsive hitscan primary weapon. This
// clock only presents authorized shots; it never performs another damage hit.
struct RailPulseShot final {
    static constexpr float Speed = 720.0f, TrailLength = 2.8f, FadeDuration = 0.035f;
    uint64_t shotId = 0;
    Vector3 start{}, end{}, direction{};
    float length = 0.0f, age = 0.0f;
    float Duration() const { return length / Speed + FadeDuration; }
    Vector3 Head() const {
        const float distance = (std::min)(length, 0.65f + age * Speed);
        return {start.x+direction.x*distance, start.y+direction.y*distance, start.z+direction.z*distance};
    }
    Vector3 Tail() const {
        const float distance = (std::max)(0.0f, (std::min)(length, 0.65f + age * Speed) - TrailLength);
        return {start.x+direction.x*distance, start.y+direction.y*distance, start.z+direction.z*distance};
    }
    float Opacity() const { return (std::clamp)((Duration()-age)/FadeDuration,0.0f,1.0f); }
    float MuzzleOpacity() const { return (std::clamp)(1.0f-age/0.035f,0.0f,1.0f); }
};

class RailPulseShotPresentation final {
public:
    static constexpr size_t Capacity = 32;
    void Reset() { shots_.clear(); }
    void Update(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        for (auto& shot : shots_) shot.age += dt;
        std::erase_if(shots_, [](const auto& shot) { return shot.age >= shot.Duration(); });
    }
    bool Enqueue(uint64_t id, Vector3 start, Vector3 end, float age = 0.0f) {
        const auto finite = [](Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
        if (!id || !finite(start) || !finite(end) || !std::isfinite(age) || age < 0.0f ||
            std::any_of(shots_.begin(), shots_.end(), [id](const auto& shot) { return shot.shotId == id; })) return false;
        const Vector3 delta{end.x-start.x, end.y-start.y, end.z-start.z};
        const float length = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
        if (!std::isfinite(length) || length < 0.05f) return false;
        RailPulseShot shot{id,start,end,{delta.x/length,delta.y/length,delta.z/length},length,age};
        if (age >= shot.Duration()) return false;
        if (shots_.size() >= Capacity) shots_.erase(shots_.begin());
        shots_.push_back(shot);
        return true;
    }
    bool Visible() const { return !shots_.empty(); }
    std::span<const RailPulseShot> Shots() const { return shots_; }
private:
    std::vector<RailPulseShot> shots_;
};
