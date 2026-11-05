#pragma once

#include "RailVehicleWheelContactPresentationBridge.h"
#include <algorithm>
#include <array>
#include <cmath>

struct RailBrakeSpark final {
    Vector3 position{}, previousPosition{}, velocity{};
    float age = 1.0f;
    float lifetime = .25f;
};

// Emission follows achieved braking. Released sparks retain world-space inertia.
class RailBrakePresentation final {
public:
    void Reset() { *this = {}; }
    const std::array<RailBrakeSpark,64>& Sparks() const { return sparks_; }
    bool Visible() const {
        return std::any_of(sparks_.begin(),sparks_.end(),[](const auto& p){return p.age<p.lifetime;});
    }
    void Update(float dt,const RailVehicleRuntimeState& vehicle,
        const RailVehicleWheelContactPresentationFrame& wheels,float brakeBlend,bool enabled) {
        if(!std::isfinite(dt) || dt<=0) return;
        if(!enabled) { Reset(); return; }
        dt=(std::min)(dt,.1f);
        for(auto& p:sparks_) if(p.age<p.lifetime) {
            p.previousPosition=p.position;
            p.age+=dt;
            p.velocity.y-=9.81f*dt;
            p.position.x+=p.velocity.x*dt;
            p.position.y+=p.velocity.y*dt;
            p.position.z+=p.velocity.z*dt;
        }
        const float strength=(std::clamp)(-vehicle.acceleration/36.0f,0.0f,1.0f)*
            (std::clamp)(vehicle.speed/25.0f,0.0f,1.0f);
        if(brakeBlend<=.01f || strength<.08f || !wheels.valid) { accumulator_=0; return; }
        accumulator_=(std::min)(4.0f,accumulator_+dt*(20.0f+strength*30.0f));
        const unsigned bursts=(std::min)(4u,unsigned(accumulator_));
        accumulator_-=float(bursts);
        for(unsigned burst=0;burst<bursts;++burst) for(size_t i=0;i<wheels.wheels.size();++i) {
            const auto& wheel=wheels.wheels[i];
            if(!wheel.visible || !wheel.supported) continue;
            auto& p=sparks_[serial_++%sparks_.size()];
            const float side=(i%2)==0?-1.0f:1.0f;
            const float spread=4.5f+.8f*float(serial_%5);
            const float forwardSpeed=vehicle.speed*.35f;
            p.position=wheel.axleCenter;
            p.position.x+=vehicle.up.x*.08f+vehicle.right.x*side*.25f;
            p.position.y+=vehicle.up.y*.08f+vehicle.right.y*side*.25f;
            p.position.z+=vehicle.up.z*.08f+vehicle.right.z*side*.25f;
            p.previousPosition=p.position;
            p.velocity={vehicle.forward.x*forwardSpeed+vehicle.right.x*side*spread+vehicle.up.x*2.2f,
                vehicle.forward.y*forwardSpeed+vehicle.right.y*side*spread+vehicle.up.y*2.2f,
                vehicle.forward.z*forwardSpeed+vehicle.right.z*side*spread+vehicle.up.z*2.2f};
            p.age=0;
            p.lifetime=.18f+.025f*float(serial_%5);
        }
    }
private:
    std::array<RailBrakeSpark,64> sparks_{};
    float accumulator_ = 0;
    unsigned serial_ = 0;
};
