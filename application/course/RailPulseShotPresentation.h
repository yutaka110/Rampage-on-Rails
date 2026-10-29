#pragma once

#include "utils/math/Vector.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// The hitscan primary lights its whole authorized segment on the contact
// frame, then fades. A later flying head would imply a second, delayed hit.
struct RailPulseShot final {
    static constexpr float ContactRadius = .35f, FadeDuration = .055f;
    uint64_t shotId = 0;
    Vector3 start{}, end{}, direction{};
    float length = 0.0f, age = 0.0f;
    float radius = ContactRadius;
    float Duration() const { return FadeDuration; }
    Vector3 Head() const { return end; }
    Vector3 Tail() const { return start; }
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
    bool Enqueue(uint64_t id, Vector3 start, Vector3 end, float age = 0.0f,float radius=RailPulseShot::ContactRadius) {
        const auto finite = [](Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
        if (!id || !finite(start) || !finite(end) || !std::isfinite(age) || age < 0.0f ||
            !std::isfinite(radius) || radius<=0 ||
            std::any_of(shots_.begin(), shots_.end(), [id](const auto& shot) { return shot.shotId == id; })) return false;
        const Vector3 delta{end.x-start.x, end.y-start.y, end.z-start.z};
        const float length = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
        if (!std::isfinite(length) || length < 0.05f) return false;
        RailPulseShot shot{id,start,end,{delta.x/length,delta.y/length,delta.z/length},length,age,radius};
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
