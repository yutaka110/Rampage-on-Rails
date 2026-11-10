#pragma once

#include "utils/math/Vector.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Each hitscan shot has two visual bolts. Both paths end at the resolved
// contact (or range limit), regardless of subsequent vehicle or target motion.
struct RailPulseShot final {
    static constexpr float ContactRadius = .35f;
    static constexpr float BoltLength = 7.0f;
    static constexpr float BoltSpeed = 1500.0f;
    uint64_t shotId = 0;
    Vector3 start{}, end{}, muzzleOffset{};
    float length = 0.0f, age = 0.0f;
    float radius = ContactRadius;
    static float InitialDistance(float length) { return (std::min)(length*.25f,BoltLength*.30f); }
    static float TravelDuration(float length) {
        return (std::max)(.035f,(length-InitialDistance(length))/BoltSpeed);
    }
    float Duration() const { return TravelDuration(length); }
    float HeadDistance() const {
        return std::lerp(InitialDistance(length),length,(std::clamp)(age/Duration(),0.0f,1.0f));
    }
    Vector3 Head(float side = 0.0f) const { return Position(HeadDistance(),side); }
    Vector3 Tail(float side = 0.0f) const {
        return Position((std::max)(0.0f,HeadDistance()-BoltLength),side);
    }
    float MuzzleOpacity() const { return (std::clamp)(1.0f-age/0.035f,0.0f,1.0f); }

private:
    Vector3 Position(float distance, float side) const {
        const float progress=distance/length;
        return {
            std::lerp(start.x+muzzleOffset.x*side,end.x,progress),
            std::lerp(start.y+muzzleOffset.y*side,end.y,progress),
            std::lerp(start.z+muzzleOffset.z*side,end.z,progress),
        };
    }
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
    bool Enqueue(uint64_t id, Vector3 start, Vector3 end, float age = 0.0f,
        float radius = RailPulseShot::ContactRadius, Vector3 muzzleOffset = {.9f,0,0}) {
        const auto finite = [](Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
        if (!id || !finite(start) || !finite(end) || !finite(muzzleOffset) || !std::isfinite(age) || age < 0.0f ||
            !std::isfinite(radius) || radius<=0 ||
            std::any_of(shots_.begin(), shots_.end(), [id](const auto& shot) { return shot.shotId == id; })) return false;
        const Vector3 delta{end.x-start.x, end.y-start.y, end.z-start.z};
        const float length = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
        if (!std::isfinite(length) || length < 0.05f) return false;
        RailPulseShot shot{id,start,end,muzzleOffset,length,age,radius};
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
