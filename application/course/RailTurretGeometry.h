#pragma once
#include <string_view>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "utils/math/Vector.h"
namespace RailTurret {
inline bool IsMesh(std::string_view mesh) { return mesh=="combat_turret" || mesh=="siege_turret" || mesh=="gatling_turret" || mesh=="carrier_heavy_turret"; }
struct Geometry { const char* baseMesh; const char* headMesh; float groundFootY,muzzleForward,muzzleSide,muzzleHeight,halfFootprint; const char* movingMesh=nullptr; };
inline Geometry ForMesh(std::string_view mesh) {
    return mesh=="gatling_turret" ? Geometry{"gatling_turret_base","gatling_turret_head",-2.55f,2.25f,0,0.32f,1.10f,"gatling_turret_rotor"} :
        mesh=="carrier_heavy_turret" ? Geometry{"carrier_heavy_turret_base","carrier_heavy_turret_head",-2.55f,2.10f,0,0.32f,1.25f,"carrier_heavy_turret_barrel"} :
        mesh=="siege_turret" ? Geometry{"siege_turret_base","siege_turret_head",-2.55f,1.95f,0.44f,0.32f,1.10f} :
        Geometry{"combat_turret_base","combat_turret_head",-0.47f,0.8272f,0.2538f,0.1598f,0.86f};
}
inline float GatlingRotation(float seconds) { return std::fmod(seconds*18.0f,6.28318530718f); }
inline constexpr float SpinSpeed=14.0f,SpinAcceleration=56.0f,SpinDeceleration=14.0f;
struct MotionState {
    float rotorAngle=0,rotorSpeed=0,lastFireAge=-100;
    uint64_t observedFireSequence=0;
    bool initialized=false;
};
// Exact integration of a bounded speed ramp; shared by delayed launch
// prediction and the rendered rotor, including frame rates and pause.
inline float AdvanceRotor(float& speed,float target,float seconds) {
    const float dt=(std::max)(0.0f,seconds),accel=target>speed?SpinAcceleration:-SpinDeceleration;
    const float ramp=(std::min)(dt,std::abs(target-speed)/std::abs(accel));
    const float end=speed+accel*ramp;
    const float angle=(speed+end)*.5f*ramp+target*(dt-ramp);
    speed=(std::clamp)(end,0.0f,SpinSpeed);return angle;
}
inline void ObserveFire(MotionState& state,float age,uint64_t sequence) {
    if(sequence>state.observedFireSequence){state.observedFireSequence=sequence;state.lastFireAge=age;}
}
inline void UpdateMotion(MotionState& state,float age,uint64_t sequence,float dt,bool charging,bool alive,bool rotating=true) {
    if(!state.initialized){state.initialized=true;state.observedFireSequence=sequence;}
    if(!alive){state.rotorSpeed=0;return;}
    const bool burst=state.lastFireAge>=0 && age-state.lastFireAge<1.25f;
    state.rotorAngle=std::fmod(state.rotorAngle+AdvanceRotor(state.rotorSpeed,rotating && (charging || burst)?SpinSpeed:0,dt),6.28318530718f);
}
inline float RotorAngle(const MotionState& state,float age,float delay=0) {
    if(!state.initialized)return GatlingRotation(age+delay);
    float speed=state.rotorSpeed;
    return state.rotorAngle+AdvanceRotor(speed,SpinSpeed,delay);
}
inline float Recoil(const MotionState& state,std::string_view mesh,float age) {
    if(!state.initialized || state.lastFireAge<0)return 0;
    float t=age-state.lastFireAge;
    if(t<0)return 0;
    const bool rapid=mesh=="gatling_turret";
    if(rapid)t-=float((std::min)(23,int((t+.00001f)/.05f)))*.05f;
    const float kick=rapid?.010f:.055f,finish=rapid?.047f:.72f,travel=rapid?.050f:.36f;
    if(t>=finish)return 0;
    if(t<kick)return travel*std::sin((std::max)(0.0f,t)/kick*1.570796327f);
    const float u=(t-kick)/(finish-kick);return travel*(1-u)*(1-u);
}
inline Vector3 MuzzleLocalAtPhase(std::string_view mesh,int barrel,float phase,float recoil=0) {
    const auto g=ForMesh(mesh);
    if(mesh=="gatling_turret") {
        const float angle=phase+float(barrel%6)*6.28318530718f/6;
        return {.40f*std::cos(angle),g.muzzleHeight+.40f*std::sin(angle),-g.muzzleForward+recoil};
    }
    return {(barrel&1?1.0f:-1.0f)*g.muzzleSide,g.muzzleHeight,-g.muzzleForward+recoil};
}
inline Vector3 MuzzleLocal(std::string_view mesh,int barrel,float seconds) {
    return MuzzleLocalAtPhase(mesh,barrel,GatlingRotation(seconds));
}
}
