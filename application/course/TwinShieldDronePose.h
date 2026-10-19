#pragma once

#include "utils/math/MathUtils.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

// The rigid model's nose is -Z. Cinematic flight follows the rail/world
// heading; only live combat uses the camera-facing aiming convention.
inline Vector3 ResolveTwinShieldDroneFacing(Vector3 railTangent, Vector3 position,
        Vector3 camera, Vector3 localRotation, Vector3 animationRotation,
        bool flightControlled) {
    if(flightControlled) {
        const auto forward=Normalize(railTangent);
        const auto right=Normalize(Vector3{forward.z,0,-forward.x});
        const Vector3 up{forward.y*right.z-forward.z*right.y,
            forward.z*right.x-forward.x*right.z,forward.x*right.y-forward.y*right.x};
        const float pitch=localRotation.x+animationRotation.x,yaw=localRotation.y+animationRotation.y;
        const Vector3 nose{-std::cos(pitch)*std::sin(yaw),std::sin(pitch),-std::cos(pitch)*std::cos(yaw)};
        const Vector3 direction{right.x*nose.x+up.x*nose.y+forward.x*nose.z,
            right.y*nose.x+up.y*nose.y+forward.y*nose.z,right.z*nose.x+up.z*nose.y+forward.z*nose.z};
        float worldYaw=std::atan2(-direction.x,-direction.z);
        if(worldYaw<0.0f) worldYaw+=6.28318530718f;
        return {std::asin((std::clamp)(direction.y,-1.0f,1.0f)),worldYaw,
            localRotation.z+animationRotation.z};
    }
    Vector3 base{-std::asin((std::clamp)(railTangent.y,-1.0f,1.0f)),
        std::atan2(railTangent.x,railTangent.z),0.0f};
    if(!flightControlled) {
        const Vector3 delta{camera.x-position.x,camera.y-position.y,camera.z-position.z};
        const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        if(length>0.001f) base={std::asin((std::clamp)(delta.y/length,-1.0f,1.0f)),
            std::atan2(-delta.x,-delta.z),0.0f};
    }
    return {base.x+localRotation.x+animationRotation.x,
        base.y+localRotation.y+animationRotation.y,base.z+localRotation.z+animationRotation.z};
}

// Shared visual pose; does not own a spawn, collision body or combat state.
struct TwinShieldDronePose final {
    bool visible = false;
    Vector3 position{};
    Vector3 rotation{};
    Vector3 scale{1,1,1};
    float alpha = 1.0f;
    float charge = 0.0f;
    float flash = 0.0f;
    float death = 0.0f;
    float flightBank = 0.0f;
    float destructionAge = -1.0f; // Negative means an intact aircraft.
    uint32_t destructionSeed = 0;
    Vector3 destructionDrift{};
    float destructionImpactAge = -1.0f; // Cached contact; negative keeps the airborne fallback.
    Vector3 destructionImpulse{}; // World-space velocity from the lethal contact.
    Vector3 destructionHitDirection{};
    float destructionHitSide = 0.0f; // Local -X/+X; zero uses the seeded centre-hit fallback.
    Vector3 flightVelocity{};
    bool hasFlightVelocity = false;
};

inline Matrix4x4 BuildTwinShieldDroneWorldMatrix(const TwinShieldDronePose& pose) {
    // Bank about the aircraft's own longitudinal axis before heading into
    // the turn. A final world-Z Euler rotation would swing the nose sideways.
    return Multiply(MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,pose.flightBank},Vector3{}),
        MakeAffineMatrix(pose.scale,pose.rotation,pose.position));
}

inline float TwinShieldDestructionSmooth(float age,float start,float end) {
    const float u=(std::clamp)((age-start)/(end-start),0.0f,1.0f);
    return u*u*(3-2*u);
}

inline float TwinShieldDestructionSide(const TwinShieldDronePose& pose) {
    return pose.destructionHitSide!=0 ? (pose.destructionHitSide>0?1.0f:-1.0f)
        : ((pose.destructionSeed&1u)?1.0f:-1.0f);
}

// Called only for the accepted lethal hit. Centre hits alternate deterministically
// while off-centre hits release the struck shield first.
inline void ConfigureTwinShieldDroneDestructionHit(TwinShieldDronePose& pose,
        Vector3 contact,Vector3 incoming,uint32_t seed) {
    pose.destructionSeed=seed;
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    const auto right=Normalize(Vector3{world.m[0][0],world.m[0][1],world.m[0][2]});
    const float lateral=(contact.x-pose.position.x)*right.x+
        (contact.y-pose.position.y)*right.y+(contact.z-pose.position.z)*right.z;
    const float threshold=0.10f*(std::max)(0.25f,pose.scale.x);
    pose.destructionHitSide=std::abs(lateral)>threshold ? (lateral>0?1.0f:-1.0f) : ((seed&1u)?1.0f:-1.0f);
    float length=std::sqrt(incoming.x*incoming.x+incoming.y*incoming.y+incoming.z*incoming.z);
    if(!std::isfinite(length) || length<0.001f) {
        incoming=Normalize(Vector3{world.m[2][0],world.m[2][1],world.m[2][2]}); length=1;
    }
    pose.destructionHitDirection={incoming.x/length,incoming.y/length,incoming.z/length};
    const float size=(std::clamp)((pose.scale.x+pose.scale.y+pose.scale.z)/6.0f,0.6f,1.7f);
    pose.destructionImpulse={size*(2.4f*incoming.x/length+0.65f*right.x*pose.destructionHitSide),
        size*(2.4f*incoming.y/length+0.65f*right.y*pose.destructionHitSide),
        size*(2.4f*incoming.z/length+0.65f*right.z*pose.destructionHitSide)};
}

inline float TwinShieldDroneDetachAge(const TwinShieldDronePose& pose,float side) {
    const float first=0.24f+0.018f*float(pose.destructionSeed%3u);
    // Keep the surviving wing on the rolling aircraft until the impact flash.
    return side==TwinShieldDestructionSide(pose) ? first :
        (pose.destructionImpactAge>=0 ? pose.destructionImpactAge+0.025f : 1.28f);
}

struct TwinShieldDroneDestructionFlight final {
    Vector3 position{},velocity{};
    float roll=0;
};

inline TwinShieldDroneDestructionFlight EvaluateTwinShieldDroneDestructionFlight(
        const TwinShieldDronePose& pose,float age) {
    age=std::isfinite(age)?(std::max)(0.0f,age):0.0f;
    if(pose.destructionImpactAge>=0) age=(std::min)(age,pose.destructionImpactAge);
    const auto anchor=BuildTwinShieldDroneWorldMatrix(pose);
    Vector3 right{anchor.m[0][0],0,anchor.m[0][2]};
    const float horizontal=std::sqrt(right.x*right.x+right.z*right.z);
    right=horizontal>0.001f?Vector3{right.x/horizontal,0,right.z/horizontal}:Vector3{1,0,0};
    const float side=TwinShieldDestructionSide(pose);
    const float loss=(std::max)(0.0f,age-0.14f),fall=(std::max)(0.0f,age-0.20f);
    const float kick=(1-std::exp(-age*38.0f))*std::exp(-age*11.0f);
    const float vibration=0.035f*std::sin(age*63.0f)*std::exp(-age*14.0f);
    const float imbalance=TwinShieldDestructionSmooth(age,0.14f,0.42f);
    TwinShieldDroneDestructionFlight result;
    result.roll=-side*(0.48f*kick+vibration+0.25f*imbalance+1.50f*loss+1.35f*loss*loss);
    const float drag=std::exp(-age*0.60f),travel=0.35f*age+0.65f*(1-drag)/0.60f;
    const float recoil=(1-std::exp(-age*5.0f))/5.0f;
    const float wave=std::sin(loss*2.8f),waveRate=2.8f*std::cos(loss*2.8f);
    const float slip=side*loss*loss*(1.20f+0.25f*wave);
    const float slipRate=side*(2*loss*(1.20f+0.25f*wave)+0.25f*loss*loss*waveRate);
    result.position={pose.position.x+pose.destructionDrift.x*travel+pose.destructionImpulse.x*recoil+right.x*slip,
        pose.position.y+pose.destructionDrift.y*travel+pose.destructionImpulse.y*recoil-4.9f*fall*fall,
        pose.position.z+pose.destructionDrift.z*travel+pose.destructionImpulse.z*recoil+right.z*slip};
    const float inertia=0.35f+0.65f*drag,impulse=std::exp(-age*5.0f);
    result.velocity={pose.destructionDrift.x*inertia+pose.destructionImpulse.x*impulse+right.x*slipRate,
        pose.destructionDrift.y*inertia+pose.destructionImpulse.y*impulse-9.8f*fall,
        pose.destructionDrift.z*inertia+pose.destructionImpulse.z*impulse+right.z*slipRate};
    return result;
}

// The renderer and the one-time ground sweep evaluate this same trajectory.
// Heading follows the dive while banking around the aircraft's own nose axis.
inline Matrix4x4 EvaluateTwinShieldDroneDestructionBody(const TwinShieldDronePose& pose,float age) {
    age=std::isfinite(age)?(std::max)(0.0f,age):0.0f;
    if(pose.destructionImpactAge>=0) age=(std::min)(age,pose.destructionImpactAge);
    const auto flight=EvaluateTwinShieldDroneDestructionFlight(pose,age);
    const float kick=(1-std::exp(-age*38.0f))*std::exp(-age*11.0f);
    const float horizontal=std::sqrt(flight.velocity.x*flight.velocity.x+flight.velocity.z*flight.velocity.z);
    const float heading=horizontal>0.05f?std::atan2(-flight.velocity.x,-flight.velocity.z):pose.rotation.y;
    const float driftLength=std::sqrt(pose.destructionDrift.x*pose.destructionDrift.x+pose.destructionDrift.z*pose.destructionDrift.z);
    const float baseHeading=driftLength>0.05f?std::atan2(-pose.destructionDrift.x,-pose.destructionDrift.z):pose.rotation.y;
    float initialTurn=std::remainder(baseHeading-pose.rotation.y,6.28318530718f);
    if(std::abs(initialTurn)>3.0f) initialTurn=TwinShieldDestructionSide(pose)*std::abs(initialTurn);
    const float yawChange=initialTurn+std::remainder(heading-pose.rotation.y-initialTurn,6.28318530718f);
    const float align=TwinShieldDestructionSmooth(age,0.16f,0.76f);
    const float pitch=std::atan2(flight.velocity.y,(std::max)(0.05f,horizontal));
    const Vector3 orientation{pose.rotation.x+(pitch-pose.rotation.x)*align+0.18f*kick*(1-align),
        pose.rotation.y+yawChange*align,pose.rotation.z};
    return Multiply(MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,pose.flightBank+flight.roll},Vector3{}),
        MakeAffineMatrix(pose.scale,orientation,flight.position));
}

inline Vector3 TwinShieldDroneDestructionExhaust(const TwinShieldDronePose& pose,float age) {
    const auto world=EvaluateTwinShieldDroneDestructionBody(pose,age);
    const Vector3 local{TwinShieldDestructionSide(pose)*0.78f,0.12f,0.24f};
    return {local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
        local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
        local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
}

struct TwinShieldDroneSmokePuff final {
    static constexpr float Interval=0.055f, Lifetime=0.95f, FallbackStop=1.02f;
    Vector3 position{};
    float radius=0,opacity=0,heat=0,rotation=0;
};

// Fixed emission times leave a world-space trail, independent of frame rate.
inline TwinShieldDroneSmokePuff EvaluateTwinShieldDroneSmokePuff(const TwinShieldDronePose& pose,
        float age,uint32_t index) {
    TwinShieldDroneSmokePuff result;
    const float born=0.10f+float(index)*TwinShieldDroneSmokePuff::Interval;
    const float stop=pose.destructionImpactAge>=0?pose.destructionImpactAge:TwinShieldDroneSmokePuff::FallbackStop;
    const float elapsed=age-born;
    if(!std::isfinite(age) || born>stop || elapsed<0 || elapsed>=TwinShieldDroneSmokePuff::Lifetime) return result;
    const auto origin=TwinShieldDroneDestructionExhaust(pose,born);
    const float seed=float((pose.destructionSeed*17u+index*13u)%97u);
    const float size=(std::clamp)((pose.scale.x+pose.scale.y+pose.scale.z)/6.0f,0.6f,1.7f);
    result.position={origin.x+pose.destructionDrift.x*elapsed*0.10f+0.15f*std::sin(seed)*elapsed,
        origin.y+elapsed*0.75f,origin.z+pose.destructionDrift.z*elapsed*0.10f+0.15f*std::cos(seed)*elapsed};
    result.radius=size*(0.16f+0.72f*elapsed)*(0.88f+0.04f*float(index%5u));
    result.opacity=(1-std::exp(-elapsed*45))*(1-TwinShieldDestructionSmooth(elapsed,0.40f,TwinShieldDroneSmokePuff::Lifetime))*0.54f;
    result.heat=std::exp(-elapsed*18)*0.18f;
    result.rotation=seed+elapsed*(index%2u?0.16f:-0.12f);
    return result;
}

inline Vector3 EvaluateTwinShieldDroneGroundProbe(const TwinShieldDronePose& pose,float age) {
    const auto world=EvaluateTwinShieldDroneDestructionBody(pose,age);
    const float bottom=0.55f*(std::max)({pose.scale.x,pose.scale.y,pose.scale.z});
    return {world.m[3][0],world.m[3][1]-bottom,world.m[3][2]};
}

// Frozen world-space breakup, independent of camera direction and actor retirement.
struct TwinShieldDroneBreakupFrame final {
    static constexpr float Lifetime=1.45f;
    Matrix4x4 bodyWorld{}, leftWorld{}, rightWorld{};
    float opacity=1.0f, coreOpacity=1.0f, coreGain=1.8f, hullFlash=0.0f;
};

inline TwinShieldDroneBreakupFrame EvaluateTwinShieldDroneBreakup(const TwinShieldDronePose& pose) {
    TwinShieldDroneBreakupFrame result;
    const float age=std::isfinite(pose.destructionAge) ? (std::max)(0.0f,pose.destructionAge) : 0.0f;
    const auto smooth=TwinShieldDestructionSmooth;
    const float pulse=smooth(age,0.0f,0.055f)*(1-smooth(age,0.10f,0.30f));
    result.coreGain=1.8f+pose.charge*3.2f+16.0f*pulse;
    result.coreOpacity=1-smooth(age,0.20f,0.44f);
    result.hullFlash=0.30f*pulse;
    result.opacity=pose.destructionImpactAge>=0
        ? 1-smooth(age,pose.destructionImpactAge,pose.destructionImpactAge+0.18f)
        : 1-smooth(age,1.02f,TwinShieldDroneBreakupFrame::Lifetime);
    result.bodyWorld=EvaluateTwinShieldDroneDestructionBody(pose,age);
    const float size=(std::max)(0.25f,(pose.scale.x+pose.scale.y+pose.scale.z)/6.0f);
    const auto panel=[&](float side) {
        const float detach=TwinShieldDroneDetachAge(pose,side);
        const float dt=(std::max)(0.0f,age-detach);
        const float spread=1.09f+pose.charge*0.14f,opening=0.08f+pose.charge*0.42f;
        const auto local=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,side*opening,0},Vector3{side*spread,0,0});
        if(age<=detach) return Multiply(local,result.bodyWorld);
        const auto releaseBody=EvaluateTwinShieldDroneDestructionBody(pose,detach);
        const auto release=Multiply(local,releaseBody);
        constexpr float h=0.001f;
        const auto before=Multiply(local,EvaluateTwinShieldDroneDestructionBody(pose,detach-h));
        const auto after=Multiply(local,EvaluateTwinShieldDroneDestructionBody(pose,detach+h));
        const auto sideAxis=Normalize(Vector3{releaseBody.m[0][0],releaseBody.m[0][1],releaseBody.m[0][2]});
        const bool first=side==TwinShieldDestructionSide(pose);
        const float spin=dt*(first?1.0f:0.70f)+dt*dt*(first?0.38f:0.22f);
        auto world=Multiply(MakeAffineMatrix(Vector3{1,1,1},
            Vector3{side*spin*0.72f,side*spin*0.95f,-side*spin*1.65f},Vector3{}),release);
        const float eject=side*(first?2.4f:1.6f)*size;
        world.m[3][0]+=((after.m[3][0]-before.m[3][0])/(2*h)+sideAxis.x*eject)*dt;
        world.m[3][1]+=((after.m[3][1]-before.m[3][1])/(2*h)+sideAxis.y*eject+(first?0.55f:0.20f))*dt-4.9f*dt*dt;
        world.m[3][2]+=((after.m[3][2]-before.m[3][2])/(2*h)+sideAxis.z*eject)*dt;
        return world;
    };
    result.leftWorld=panel(-1); result.rightWorld=panel(1);
    return result;
}
