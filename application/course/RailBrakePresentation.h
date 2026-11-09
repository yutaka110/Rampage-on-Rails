#pragma once

#include "RailVehicleWheelContactPresentationBridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rail_friction_detail {
inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 Scale(Vector3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}

struct RailBrakeSpark final {
    Vector3 position{}, velocity{};
    Vector3 railOrigin{}, railNormal{}, railRight{};
    float age = 1.0f;
    float lifetime = .25f;
    float trailAge = 0.0f;
    float width = .04f;
    float energy = 1.0f;
    float trailLimit = .9f;
    bool canBounce = false;
    bool bounced = false;

    static constexpr Vector3 Gravity{0.0f, -29.0f, 0.0f};

    bool Visible() const { return age < lifetime; }
    Vector3 TrailTail() const {
        using namespace rail_friction_detail;
        const float speed = std::sqrt(Dot(velocity, velocity));
        const float t = (std::min)({trailAge, .064f, trailLimit / (std::max)(speed, 1.0f)});
        return Add(Subtract(position, Scale(velocity, t)), Scale(Gravity, .5f * t * t));
    }
};

struct RailFrictionFlash final {
    Vector3 position{}, normal{};
    float age = 1.0f;
    float lifetime = .06f;
    float energy = 0.0f;
    float radius = .20f;

    float Intensity() const {
        const float remaining = (std::max)(0.0f, 1.0f - age / lifetime);
        return energy * remaining * remaining;
    }
};

// Both inputs use the same world-space sparks. The landing burst waits for
// the actual wheel supports and fires once per landing.
class RailBrakePresentation final {
public:
    static constexpr float PassDuckSeconds=.14f;
    static float PassFrictionGain(float remaining) {
        const float recovery=1-(std::clamp)(remaining/.10f,0.0f,1.0f);
        return .32f+.68f*recovery*recovery*(3-2*recovery);
    }
    struct LoadFrame {
        float pitchDegrees=0, compression=0, scrape=0;
        bool engaged=false, released=false;
    };
    void Reset() { *this = {}; }
    const auto& Sparks() const { return sparks_; }
    const auto& Flashes() const { return flashes_; }
    const LoadFrame& Load() const { return loadFrame_; }

    void UpdateLoad(float dt, float speed, bool held, bool supported, bool enabled) {
        loadFrame_.engaged=loadFrame_.released=false;
        if(!std::isfinite(dt) || dt<=0)return;
        if(!enabled || !supported) {
            loadFrame_={};
            load_=releaseFrom_=0;
            releaseAge_=.36f;
            loaded_=false;
            return;
        }
        dt=(std::min)(dt,.10f);
        const float rolling=(std::clamp)((speed-1.0f)/25.0f,0.0f,1.0f);
        const bool braking=held && rolling>0;
        loadFrame_.engaged=braking && !loaded_;
        loadFrame_.released=!braking && loaded_ && rolling>.06f;
        if(braking) {
            load_+=(rolling-load_)*(1.0f-std::exp(-dt*16.0f));
        } else {
            if(loaded_){releaseFrom_=load_;releaseAge_=0;}
            releaseAge_=(std::min)(.36f,releaseAge_+dt);
            const float phase=releaseAge_/.36f;
            const float damping=(1-phase)*(1-phase);
            load_=releaseFrom_*damping*std::cos(phase*4.65f);
        }
        loaded_=braking;
        loadFrame_.pitchDegrees=3.8f*load_;
        loadFrame_.compression=.11f*load_;
        loadFrame_.scrape=braking ? rolling*(.28f+.72f*(std::max)(0.0f,load_)) : 0.0f;
    }

    bool Visible() const {
        return std::any_of(sparks_.begin(), sparks_.end(), [](const auto& p) { return p.Visible(); }) ||
            std::any_of(flashes_.begin(), flashes_.end(), [](const auto& f) { return f.Intensity() > 0; });
    }

    std::array<Vector4, 2> Lights() const {
        using namespace rail_friction_detail;
        std::array<Vector4, 2> lights{};
        for (size_t i = 0; i < lights.size(); ++i) {
            const auto& flash = flashes_[i];
            const auto source = Add(flash.position, Scale(flash.normal, .26f));
            lights[i] = {source.x, source.y, source.z, flash.Intensity()};
        }
        return lights;
    }

    void Update(float dt, const RailVehicleRuntimeState& vehicle,
        const RailVehicleWheelContactPresentationFrame& wheels, float railHeadWidth,
        bool brakeHeld, bool landing, bool enabled) {
        if (!std::isfinite(dt) || dt <= 0) return;
        if (!enabled) { Reset(); return; }

        dt = (std::min)(dt, .1f);
        for (auto& flash : flashes_) flash.age = (std::min)(flash.lifetime, flash.age + dt);
        for (auto& spark : sparks_) if (spark.Visible()) Advance(spark, dt, railHeadWidth * .5f);

        if (!landing) landingEmitted_ = false;
        const auto supported = [](const auto& wheel) { return wheel.visible && wheel.supported; };
        if (landing && !landingEmitted_ && wheels.valid &&
            std::any_of(wheels.wheels.begin(), wheels.wheels.end(), supported)) {
            landingEmitted_ = true;
            const float energy = std::lerp(.7f, 1.0f, (std::clamp)(vehicle.speed / 70.0f, 0.0f, 1.0f));
            for (size_t i = 0; i < wheels.wheels.size(); ++i) {
                if (supported(wheels.wheels[i]))
                    Emit(vehicle, wheels.wheels[i], i, energy, i < 2 ? 13 : 7, true, true);
            }
        }

        if (!brakeHeld || vehicle.speed <= 1.0f || !wheels.valid) {
            brakeWasHeld_ = false;
            accumulator_ = 0.0f;
            return;
        }
        const float deceleration = (std::clamp)(-vehicle.acceleration / 36.0f, 0.0f, 1.0f);
        const float rollingSpeed = (std::clamp)(vehicle.speed / 25.0f, 0.0f, 1.0f);
        const float energy = (.55f + .34f * deceleration) * rollingSpeed;
        if (!brakeWasHeld_) {
            for (size_t i = 2; i < wheels.wheels.size(); ++i) {
                if (supported(wheels.wheels[i])) Emit(vehicle, wheels.wheels[i], i, .94f * rollingSpeed, 5, true, false);
            }
            brakeWasHeld_ = true;
        }
        accumulator_ = (std::min)(5.0f, accumulator_ + dt * (35.0f + 40.0f * deceleration) * rollingSpeed);
        while (accumulator_ >= 1.0f) {
            accumulator_ -= 1.0f;
            const uint32_t burst = ++burstSerial_;
            const size_t wheelIndex = burst % 5 == 0 ? (burst & 1u) : 2 + (burst & 1u);
            if (supported(wheels.wheels[wheelIndex]))
                Emit(vehicle, wheels.wheels[wheelIndex], wheelIndex, energy, 2, burst % 7 == 0, false);
        }
    }

private:
    static float Noise(uint32_t seed) {
        seed ^= seed >> 16;
        seed *= 0x7feb352du;
        seed ^= seed >> 15;
        return float(seed & 0xffffu) / 65535.0f;
    }

    static void Advance(RailBrakeSpark& p, float dt, float railHalfWidth) {
        using namespace rail_friction_detail;
        p.age += dt;
        if (!p.Visible()) return;

        const auto before = p.position;
        const auto next = Add(Add(before, Scale(p.velocity, dt)), Scale(RailBrakeSpark::Gravity, .5f * dt * dt));
        const float oldHeight = Dot(Subtract(before, p.railOrigin), p.railNormal);
        const float newHeight = Dot(Subtract(next, p.railOrigin), p.railNormal);
        if (oldHeight > 0 && newHeight <= 0) {
            const float fraction = oldHeight / (oldHeight - newHeight);
            const auto impact = Add(before, Scale(Subtract(next, before), fraction));
            if (std::abs(Dot(Subtract(impact, p.railOrigin), p.railRight)) <= railHalfWidth) {
                if (!p.canBounce || p.bounced || p.lifetime - p.age < .065f) {
                    p.age = p.lifetime;
                    return;
                }
                const auto impactVelocity = Add(p.velocity, Scale(RailBrakeSpark::Gravity, dt * fraction));
                const float normalSpeed = Dot(impactVelocity, p.railNormal);
                const auto tangent = Subtract(impactVelocity, Scale(p.railNormal, normalSpeed));
                p.velocity = Add(Scale(tangent, .54f), Scale(p.railNormal, (std::max)(.8f, -normalSpeed * .32f)));
                const float remaining = dt * (1 - fraction);
                p.position = Add(Add(impact, Scale(p.railNormal, .018f)),
                    Add(Scale(p.velocity, remaining), Scale(RailBrakeSpark::Gravity, .5f * remaining * remaining)));
                p.velocity = Add(p.velocity, Scale(RailBrakeSpark::Gravity, remaining));
                p.trailAge = remaining;
                p.energy *= .55f;
                p.bounced = true;
                return;
            }
        }
        p.position = next;
        p.velocity = Add(p.velocity, Scale(RailBrakeSpark::Gravity, dt));
        p.trailAge += dt;
    }

    void Emit(const RailVehicleRuntimeState& vehicle, const RailVehicleWheelContactVisual& wheel,
        size_t index, float energy, int count, bool flash, bool landing) {
        using namespace rail_friction_detail;
        const float side = (index & 1u) ? 1.0f : -1.0f;
        const auto contact = Add(wheel.railContact, Scale(vehicle.up, .075f));
        const auto source = Add(contact, Scale(vehicle.right, side * .15f));
        if (flash) {
            auto& f = flashes_[index & 1u];
            const float flashEnergy = energy * (landing ? (index < 2 ? 2.35f : 1.45f) : .95f);
            if (flashEnergy >= f.Intensity()) {
                f = {Add(source, Scale(vehicle.up, .10f)), vehicle.up, 0.0f,
                    landing ? .12f : .075f, flashEnergy, landing ? .52f : .28f};
            }
        }
        for (int i = 0; i < count; ++i) {
            const uint32_t seed = ++serial_;
            auto& p = sparks_[(seed - 1) % sparks_.size()];
            p = {};
            p.canBounce = seed % 8 == 0;
            p.railOrigin = contact;
            p.railNormal = vehicle.up;
            p.railRight = vehicle.right;
            const float scatter = Noise(seed * 3 + 71);
            const float drift = p.canBounce ? side * (.04f + .10f * scatter) :
                side * std::lerp(1.6f, landing ? 8.4f : 4.2f, scatter);
            const float along = vehicle.speed * std::lerp(.17f, .37f, Noise(seed * 7 + 13)) - 2.4f;
            const float rise = p.canBounce ? .9f : std::lerp(.9f, landing ? 5.4f : 2.8f, Noise(seed * 11 + 31));
            p.position = Add(p.canBounce ? contact : source,
                Add(Scale(vehicle.up, std::lerp(.09f, .16f, scatter)), Scale(vehicle.forward, .22f * (scatter - .5f))));
            p.velocity = Add(Scale(vehicle.forward, along), Add(Scale(vehicle.right, drift), Scale(vehicle.up, rise)));
            p.age = 0.0f;
            p.lifetime = std::lerp(.17f, landing ? .42f : .31f, Noise(seed * 19 + 17));
            if (p.canBounce) p.lifetime = (std::max)(p.lifetime, .29f);
            p.width = landing ? std::lerp(.032f, .098f, Noise(seed * 29 + 23)) :
                std::lerp(.043f, .082f, Noise(seed * 29 + 23));
            p.energy = (.68f + .64f * Noise(seed * 23 + 8)) * energy;
            p.trailLimit = landing ? std::lerp(.40f, 2.10f, Noise(seed * 31 + 5)) :
                std::lerp(.52f, 1.28f, Noise(seed * 31 + 5));
        }
    }

    std::array<RailBrakeSpark, 112> sparks_{};
    std::array<RailFrictionFlash, 2> flashes_{};
    float accumulator_ = 0.0f;
    uint32_t serial_ = 0;
    uint32_t burstSerial_ = 0;
    bool brakeWasHeld_ = false;
    bool landingEmitted_ = false;
    LoadFrame loadFrame_{};
    float load_=0,releaseFrom_=0,releaseAge_=.36f;
    bool loaded_=false;
};
