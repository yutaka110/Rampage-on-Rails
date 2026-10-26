#pragma once

#include "utils/math/MathUtils.h"
#include "RailManualBoost.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rail_boost_detail {
inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3 Mul(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vector3 Point(Vector3 p, const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
inline float Smooth(float x) { x=(std::clamp)(x,0.0f,1.0f); return x*x*(3-2*x); }
}

// Shared by the physical exhaust housings and the body-attached VFX.
struct RailBoostExhaust final {
    static constexpr float RootX=2.55f, RootY=0.02f, RootZ=0.70f;
    static Vector3 Root(float side) { return {side*RootX,RootY,RootZ}; }
};

struct RailBoostPresentationInput final {
    float deltaTime=0, distance=0, speed=0, strength=0, ignition=0;
    bool gameplayActive=false;
    Matrix4x4 bodyWorld=MakeIdentity4x4();
    Vector3 ground{}, forward{0,0,1}, right{1,0,0}, up{0,1,0};
};
struct RailBoostRibbon final {
    Vector3 head{}, tail{};
    float width=0, opacity=0;
    bool propulsion=false;
};
struct RailBoostDust final {
    Vector3 center{};
    float radius=0, opacity=0;
    uint32_t seed=0;
};
struct RailBoostPresentationFrame final {
    // Enough world streaks to cover their approach at the full 2x speed;
    // premature recycling used to remove them before reaching the near field.
    static constexpr size_t MaximumWindRibbons=96;
    std::array<RailBoostRibbon,MaximumWindRibbons+2> ribbons{};
    std::array<RailBoostDust,40> dust{};
    size_t ribbonCount=0, dustCount=0;
    float strength=0, animationUvY=0, ignition=0, ignitionShock=0;
    std::array<Vector4,2> lights{}; // world position + local radiance strength
    Vector3 exhaustRight{1,0,0}, exhaustUp{0,1,0};
};

// Bounded world-space exhaust, wheel dust and near-ground air streaks. Only
// the two exhaust roots follow the rendered body; emitted particles stay in
// the world. No collision queries, gameplay actors or per-enemy work.
class RailBoostPresentation final {
public:
    static constexpr float IgnitionShockSeconds=0.12f;
    void Reset() { *this=RailBoostPresentation{}; }
    bool Visible() const { return frame_.ribbonCount>0 || frame_.dustCount>0; }
    const RailBoostPresentationFrame& Frame() const { return frame_; }
    void Update(const RailBoostPresentationInput& in) {
        if (!std::isfinite(in.deltaTime) || in.deltaTime<=0) return;
        if (!in.gameplayActive || !std::isfinite(in.distance) || !std::isfinite(in.speed)) { Reset(); return; }
        const float dt=(std::min)(in.deltaTime,0.1f);
        float travel=initialized_ ? in.distance-previousDistance_ : 0;
        if (travel<0 || travel>24) { Reset(); travel=0; }
        const Vector3 from=initialized_ ? previousGround_ : in.ground;
        initialized_=true; previousDistance_=in.distance; previousGround_=in.ground;
        const float ignition=std::isfinite(in.ignition) ? (std::clamp)(in.ignition,0.0f,1.0f) : 0;
        const float target=(std::max)(ignition*0.65f,
            std::isfinite(in.strength) ? (std::clamp)(in.strength,0.0f,1.0f) : 0.0f);
        strength_+=(target-strength_)*(1-std::exp(-(target>strength_?18.0f:9.0f)*dt));
        clock_+=dt;
        for (auto& p:particles_) if (p.live) {
            p.age+=dt;
            if (p.age>=p.life || rail_boost_detail::Dot(
                rail_boost_detail::Add(p.origin,rail_boost_detail::Mul(in.ground,-1)),in.forward)<-15) p.live=false;
        }
        if (strength_>0.05f && travel>0 && in.speed>1) {
            const auto emitAlong=[&](float& remainder,float spacing,bool wind) {
                const float total=remainder+travel;
                const int count=(std::min)(12,int(total/spacing));
                const float first=spacing-remainder;
                for (int i=0;i<count;++i) {
                    const float f=(std::clamp)((first+i*spacing)/travel,0.0f,1.0f);
                    const auto ground=rail_boost_detail::Add(from,rail_boost_detail::Mul(
                        rail_boost_detail::Add(in.ground,rail_boost_detail::Mul(from,-1)),f));
                    Spawn(in,ground,wind,(1-f)*dt);
                    if (!wind) Spawn(in,ground,false,(1-f)*dt);
                }
                remainder=std::fmod(total,spacing);
            };
            emitAlong(dustDistance_,1.10f/(0.35f+0.65f*strength_),false);
            emitAlong(windDistance_,0.48f/(0.35f+0.65f*strength_),true);
        } else { dustDistance_=windDistance_=0; }
        frame_={}; frame_.strength=strength_; frame_.ignition=ignition;
        // Reuse the real ignition clock. Rendering at a higher rate, pausing,
        // or holding the key cannot retrigger the brief optical response.
        const float ignitionAge=RailManualBoost::IgnitionSeconds*(1.0f-std::sqrt(ignition));
        const float shock=(std::clamp)(1.0f-ignitionAge/IgnitionShockSeconds,0.0f,1.0f);
        frame_.ignitionShock=shock*shock;
        frame_.exhaustRight={in.bodyWorld.m[0][0],in.bodyWorld.m[0][1],in.bodyWorld.m[0][2]};
        frame_.exhaustUp={in.bodyWorld.m[1][0],in.bodyWorld.m[1][1],in.bodyWorld.m[1][2]};
        frame_.animationUvY=2*float(uint32_t(clock_*18.0f)%64);
        if (strength_>0.012f) for (float side:{-1.0f,1.0f}) {
            // The aperture and plume share the rendered suspension/banking
            // matrix. A short ignition overshoot settles into a thick jet.
            auto root=RailBoostExhaust::Root(side);
            root.z-=0.025f; // just outside the rear-facing metal lip
            const auto head=rail_boost_detail::Point(root,in.bodyWorld);
            const auto tail=rail_boost_detail::Point(
                {root.x+side*(0.45f+0.22f*strength_),root.y-0.12f,
                    root.z-(2.9f+2.4f*strength_+0.7f*ignition)},in.bodyWorld);
            frame_.ribbons[frame_.ribbonCount++]={head,tail,0.70f+0.40f*strength_+0.26f*ignition,
                (std::min)(1.0f,strength_+ignition*0.65f),true};
            // Small unshadowed lights stay outside the hull; their six-unit
            // cutoff bounds the bounce to the cart and passing rail/ground.
            const auto light=rail_boost_detail::Point({root.x+side*0.16f,root.y+0.10f,root.z-0.32f},in.bodyWorld);
            frame_.lights[side<0 ? 0:1]={light.x,light.y,light.z,0.95f*strength_+2.0f*ignition};
        }
        for (const auto& p:particles_) if (p.live && strength_>0.006f) {
            const float fade=rail_boost_detail::Smooth(p.age/0.06f)*
                rail_boost_detail::Smooth((p.life-p.age)/0.20f);
            const auto pos=rail_boost_detail::Add(p.origin,rail_boost_detail::Mul(p.velocity,p.age));
            const float opacity=fade*p.strength*std::sqrt(strength_);
            if (p.wind && frame_.ribbonCount<frame_.ribbons.size()) {
                frame_.ribbons[frame_.ribbonCount++]={pos,
                    rail_boost_detail::Add(pos,rail_boost_detail::Mul(p.direction,-p.size)),
                    0.022f+0.010f*float(p.seed%3),opacity*0.42f,false};
            } else if (!p.wind && frame_.dustCount<frame_.dust.size()) {
                frame_.dust[frame_.dustCount++]={pos,p.size+p.age*0.85f,opacity*0.14f,p.seed};
            }
        }
    }
private:
    struct Particle {
        Vector3 origin{},velocity{},direction{};
        float age=0,life=0,size=0,strength=0;
        uint32_t seed=0;
        bool live=false,wind=false;
    };
    float Random() { seed_=seed_*1664525u+1013904223u; return float(seed_>>8)*(1.0f/16777216.0f); }
    void Spawn(const RailBoostPresentationInput& in,Vector3 ground,bool wind,float age) {
        const size_t begin=wind?40:0,end=wind?particles_.size():40;
        size_t slot=begin;
        for (size_t i=begin;i<end;++i) {
            if (!particles_[i].live) {slot=i;break;}
            if (particles_[i].age/particles_[i].life>particles_[slot].age/particles_[slot].life) slot=i;
        }
        const float sign=(serial_++%2)?-1.0f:1.0f;
        const float ahead=wind?14+Random()*22:-2.0f;
        const float lateral=sign*(wind?3.8f+Random()*5.5f:1.75f+Random()*0.30f);
        const float height=wind?0.22f+Random()*1.35f:0.12f;
        Particle p;
        p.origin=rail_boost_detail::Add(ground,rail_boost_detail::Add(
            rail_boost_detail::Mul(in.forward,ahead),rail_boost_detail::Add(
            rail_boost_detail::Mul(in.right,lateral),rail_boost_detail::Mul(in.up,height))));
        p.velocity=wind?Vector3{}:rail_boost_detail::Add(rail_boost_detail::Mul(in.right,sign*(1.0f+Random())),
            rail_boost_detail::Add(rail_boost_detail::Mul(in.up,0.4f+Random()*0.4f),
            rail_boost_detail::Mul(in.forward,-in.speed*0.045f)));
        p.direction=in.forward;
        p.age=age; p.life=wind?(std::min)(2.6f,(ahead+14)/(std::max)(1.0f,in.speed)):0.55f+Random()*0.25f;
        p.size=wind?(std::clamp)(in.speed*0.095f,1.5f,10.0f):0.18f+Random()*0.15f;
        p.strength=strength_; p.seed=seed_%128; p.live=true; p.wind=wind;
        particles_[slot]=p;
    }
    std::array<Particle,40+RailBoostPresentationFrame::MaximumWindRibbons> particles_{};
    RailBoostPresentationFrame frame_{};
    Vector3 previousGround_{};
    float previousDistance_=0, strength_=0,clock_=0,dustDistance_=0,windDistance_=0;
    bool initialized_=false;
    uint32_t seed_=8041,serial_=0;
};
