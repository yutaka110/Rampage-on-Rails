#pragma once

#include "RailEnemyEnergyPalette.h"
#include "RailRearPursuitRules.h"
#include "utils/math/Vector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace RailCartImpactEnergy {
struct Stroke {
    Vector3 start{},end{};
    float opacity=0,width=0;
};
struct Frame {
    std::array<Stroke,80> strokes{};
    size_t count=0;
};

inline Vector3 Tint(std::string_view projectile,Vector4 fallback) {
    using RailEnemyEnergy::BatteryKind;
    const auto kind=RailEnemyEnergy::KindForProjectile(projectile);
    if(kind==BatteryKind::Heavy || projectile=="enemy_interceptor_homing" || projectile=="enemy_homing_soft")
        return {.52f,.12f,1.0f};
    if(IsFangChaseRound(projectile) || projectile=="enemy_boss_arc")
        return {1.0f,.07f,.20f};
    if(kind==BatteryKind::Gatling || projectile.starts_with("enemy_turret") ||
        projectile.starts_with("enemy_predictive") || projectile=="enemy_assault_magenta_slow" ||
        projectile=="enemy_rear_pursuit" || projectile=="enemy_short_intercept" ||
        projectile=="enemy_sniper_predictive" || projectile=="enemy_direct_readable" || projectile=="enemy_mothership_energy")
        return {1.0f,.40f,.07f};
    const float peak=(std::max)({fallback.x,fallback.y,fallback.z});
    if(!std::isfinite(peak) || peak<=0)return {1.0f,.40f,.07f};
    return {(std::max)(0.0f,fallback.x)/peak,(std::max)(0.0f,fallback.y)/peak,(std::max)(0.0f,fallback.z)/peak};
}

inline constexpr float Duration(bool heavy) {return heavy?.45f:.40f;}

inline Frame Evaluate(Vector3 contact,float age,bool heavy,uint64_t seed,const std::array<Vector3,2>& rearHubs) {
    Frame result;
    if(!std::isfinite(age) || age<0 || age>=Duration(heavy))return result;
    const auto point=[](Vector3 a,Vector3 b,float t) {
        return Vector3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
    };
    const auto length=[](Vector3 a,Vector3 b) {return std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);};
    const auto smooth=[](float value,float begin,float end) {
        const float t=(std::clamp)((value-begin)/(end-begin),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    };
    const float side=contact.x<0?-1.0f:1.0f;
    const float z=(std::clamp)(contact.z,-3.28f,3.28f);
    constexpr float sidePlate=2.36f,rearFace=-3.46f;
    const bool endHit=std::abs(contact.z)>3.24f && std::abs(contact.x)<1.81f;
    const Vector3 origin=endHit?
        Vector3{contact.x,(std::clamp)(contact.y,.39f,1.49f),contact.z<0?rearFace:3.36f}:
        Vector3{side*sidePlate,(std::clamp)(contact.y,.38f,1.92f),z};
    const float fade=1.0f-smooth(age,heavy?.28f:.23f,Duration(heavy));
    const float speed=heavy?106.0f:112.0f;
    const float head=.35f+age*speed;
    const float tick=std::floor(age*36.0f);
    const float variant=float(seed%97)*.713f;
    unsigned branch=0;
    const auto trace=[&](std::span<const Vector3> nodes,float offset,float width,float strength) {
        const float branchSeed=variant+2.413f*++branch;
        for(size_t i=1;i<nodes.size() && result.count<result.strokes.size();++i) {
            const auto a=nodes[i-1],b=nodes[i];
            const float span=length(a,b);
            if(span<.01f)continue;
            const float visibleTo=(std::clamp)((head-offset)/span,0.0f,1.0f);
            if(visibleTo>0) {
                const int cells=(std::clamp)(int(std::ceil(span/.49f)),1,14);
                const bool onEndFace=std::abs(a.z)>3.30f && std::abs(b.z)>3.30f && a.z*b.z>0;
                const auto knot=[&](int index) {
                    auto p=point(a,b,float(index)/cells);
                    if(index==0 || index==cells)return p;
                    const float bend=std::sin(tick*1.57f+branchSeed+float(index)*4.43f+float(i));
                    if(onEndFace) {
                        // Bend across the rear panel in its own plane.
                        p.x-=bend*.19f*(b.y-a.y)/span;
                        p.y+=bend*.19f*(b.x-a.x)/span;
                        p.z+=std::copysign(.045f*std::abs(bend),p.z);
                    } else {
                        p.y+=bend*.11f*(b.z-a.z)/span;
                        p.z-=bend*.11f*(b.y-a.y)/span;
                        p.x+=std::copysign(.035f*std::abs(bend),p.x);
                    }
                    return p;
                };
                for(int cell=0;cell<cells && result.count<result.strokes.size();++cell) {
                    const float from=float(cell)/cells;
                    const float to=(std::min)(visibleTo,float(cell+1)/cells);
                    if(to<=from+.001f)continue;
                    const auto first=knot(cell),last=knot(cell+1);
                    const float sinceArrival=(head-offset-span*to)/speed;
                    const float surge=1.0f-smooth(sinceArrival,.026f,.16f);
                    const float flicker=.5f+.5f*std::sin(tick*2.17f+branchSeed+float(cell)*1.91f);
                    result.strokes[result.count++]={point(first,last,from*cells-cell),point(first,last,to*cells-cell),
                        fade*strength*std::lerp(.34f+.30f*flicker,.86f+.14f*flicker,surge),
                        width*std::lerp(.68f,1.0f,surge)};
                }
            }
            offset+=span;
        }
        return offset;
    };

    Vector3 rearEntry=origin;
    float rearArrival=0.0f;
    if(origin.z>rearFace) {
        Vector3 onSide=origin;
        if(endHit) {
            onSide={side*sidePlate,origin.y,3.30f};
            rearArrival=trace(std::array{origin,Vector3{side*1.80f,origin.y,3.36f},onSide},0.0f,.066f,.89f);
        }
        rearEntry={side*1.66f,1.39f,rearFace};
        const Vector3 sideSeam{side*sidePlate,std::lerp(origin.y,1.32f,.68f),std::lerp(onSide.z,-3.28f,.58f)};
        rearArrival=trace(std::array{onSide,sideSeam,Vector3{side*sidePlate,1.37f,-3.31f},rearEntry},
            rearArrival,.069f,.88f);
    }
    const Vector3 split{std::lerp(rearEntry.x,side*.72f,.64f),std::lerp(rearEntry.y,.81f,.72f),rearFace};
    const Vector3 cross{std::lerp(split.x,-side*.76f,.72f),.55f,rearFace};
    const Vector3 farToe{-side*1.64f,.17f,rearFace};
    const float splitArrival=trace(std::array{rearEntry,split},rearArrival,.102f,1.0f);
    const float crossArrival=trace(std::array{split,cross},splitArrival,.101f,1.0f);
    const float farArrival=trace(std::array{cross,farToe},crossArrival,.090f,.98f);
    const Vector3 nearToe{side*1.78f,.24f,rearFace};
    const float nearArrival=trace(std::array{split,Vector3{side*1.24f,.48f,rearFace},nearToe},
        splitArrival,.079f,.94f);
    for(const auto& hub:rearHubs) {
        const float wheelSide=std::copysign(1.0f,hub.x);
        const bool struckSide=wheelSide==side;
        const float coverBack=(std::clamp)(hub.z-.44f,-2.86f,-2.04f);
        const Vector3 cover{wheelSide*2.53f,.19f,coverBack};
        const Vector3 coverFoot{wheelSide*2.48f,-.27f,coverBack};
        trace(std::array{struckSide?nearToe:farToe,Vector3{wheelSide*2.27f,.17f,-3.38f},
            Vector3{wheelSide*2.53f,.14f,-2.90f},cover,coverFoot,hub},
            struckSide?nearArrival:farArrival,.073f,.93f);
        trace(std::array{cover,Vector3{wheelSide*2.54f,-.05f,coverBack+.23f}},
            (struckSide?nearArrival:farArrival)+1.55f,.044f,.76f);
    }
    trace(std::array{split,Vector3{side*.38f,1.08f,rearFace},Vector3{side*.13f,1.42f,rearFace}},
        splitArrival,.041f,.62f);
    trace(std::array{cross,Vector3{-side*.24f,.33f,rearFace},Vector3{-side*.28f,.05f,rearFace}},
        crossArrival,.042f,.66f);
    return result;
}
} // namespace RailCartImpactEnergy
