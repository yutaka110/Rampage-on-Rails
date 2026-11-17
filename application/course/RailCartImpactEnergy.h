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
struct HotJunction {
    Vector3 position{};
    float intensity=0;
};
struct WheelFlash {
    Vector3 position{};
    float arrival=0,intensity=0;
    float heat=0;
};
struct Frame {
    std::array<Stroke,80> strokes{};
    std::array<Stroke,24> hotTips{};
    std::array<HotJunction,2> junctions{};
    std::array<WheelFlash,2> wheels{};
    size_t count=0;
    size_t hotTipCount=0;
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

inline constexpr float Duration=.40f;
inline constexpr float HeavyDuration=.60f;
inline constexpr float HeavyCoverArrival=.185f;
inline constexpr float HeavyReboundPeak=.20f;
inline constexpr float HeavySettleSeconds=.30f;
inline constexpr float GatlingDuration=.45f;
inline constexpr float GatlingFlashSeconds=.085f;
inline constexpr float WheelDelay=.04f;

inline float Smooth(float value,float begin,float end) {
    const float t=(std::clamp)((value-begin)/(end-begin),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}

inline float GatlingFlash(float age) {
    return Smooth(age,-.014f,.040f)*(1.0f-Smooth(age,.044f,GatlingFlashSeconds));
}

inline std::array<Stroke,3> GatlingResidual(Vector3 contact,Vector3 normal,float age,uint64_t seed) {
    const float side=contact.x<0?-1.0f:1.0f;
    if(contact.z>-2.0f) {
        // The weak discharge ends on the visible seam of the struck flank.
        contact={side*1.88f,.73f,-3.46f};
        normal={0,0,-1};
    }
    const Vector3 tangent=std::abs(normal.z)>.5f?Vector3{-side,0,0}:Vector3{0,0,-1};
    const auto step=[&](float distance,float drop) {
        return Vector3{contact.x+tangent.x*distance+normal.x*.075f, contact.y+drop,
            contact.z+tangent.z*distance+normal.z*.075f};
    };
    const float glow=.29f*(1.0f-Smooth(age,.078f,GatlingDuration));
    const float flicker=.84f+.16f*std::sin(age*38.0f+float(seed%29));
    const auto bend=step(.32f,-.12f);
    return {{{step(0,0),bend,glow*flicker,.047f},
        {bend,step(.71f,-.16f),glow*.83f,.038f},
        {bend,step(.55f,.022f),glow*.55f,.028f}}};
}

inline Frame Evaluate(Vector3 contact,float age,bool heavy,uint64_t seed,const std::array<Vector3,2>& rearHubs,
        bool drawStrokes=true) {
    Frame result;
    const float duration=heavy?HeavyDuration:Duration;
    if(!std::isfinite(age) || age<0 || age>=duration)return result;
    const auto point=[](Vector3 a,Vector3 b,float t) {
        return Vector3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
    };
    const auto length=[](Vector3 a,Vector3 b) {return std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);};
    const auto smooth=Smooth;
    const auto pulse=[&](float since,float rise,float decay) {
        return smooth(since,0.0f,rise)*(1.0f-smooth(since,rise,decay));
    };
    const float side=contact.x<0?-1.0f:1.0f;
    const float z=(std::clamp)(contact.z,-3.28f,3.28f);
    constexpr float sidePlate=2.36f,rearFace=-3.46f;
    const bool endHit=std::abs(contact.z)>3.24f && std::abs(contact.x)<1.81f;
    const Vector3 origin=endHit?
        Vector3{contact.x,(std::clamp)(contact.y,.39f,1.49f),contact.z<0?rearFace:3.36f}:
        Vector3{side*sidePlate,(std::clamp)(contact.y,.38f,1.92f),z};
    const float fade=1.0f-smooth(age,heavy?.24f:.25f,duration);
    const float trunkFade=heavy?1.0f-.84f*smooth(age,.27f,.45f):1.0f;
    const float sideSpeed=heavy?125.0f:132.0f;
    const float rearSpeed=38.0f;
    const float tick=std::floor(age*30.0f);
    const float variant=float(seed%97)*.713f;
    unsigned branch=0;
    const auto trace=[&](std::span<const Vector3> nodes,float starts,float speed,float width,float strength,
            bool movingTip=true) {
        const float branchSeed=variant+2.413f*++branch;
        for(size_t i=1;i<nodes.size();++i) {
            const auto a=nodes[i-1],b=nodes[i];
            const float span=length(a,b);
            if(span<.01f)continue;
            const float travelled=(age-starts)*speed;
            const float visibleTo=(std::clamp)(travelled/span,0.0f,1.0f);
            if(drawStrokes && visibleTo>0 && strength>.005f) {
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
                    const float sinceArrival=(travelled-span*to)/speed;
                    const float surge=1.0f-smooth(sinceArrival,.024f,.116f);
                    const float flicker=.5f+.5f*std::sin(tick*2.17f+branchSeed+float(cell)*1.91f);
                    result.strokes[result.count++]={point(first,last,from*cells-cell),point(first,last,to*cells-cell),
                        fade*strength*std::lerp(.37f+.15f*flicker,.82f+.18f*flicker,surge),
                        width*std::lerp(.57f,1.0f,surge)};
                    const float hotFrom=(std::max)(from,(travelled-.40f)/span);
                    if(movingTip && to>hotFrom+.006f && result.hotTipCount<result.hotTips.size()) {
                        const float heat=1.0f-smooth(travelled-span*to,.025f,.40f);
                        result.hotTips[result.hotTipCount++]={point(first,last,hotFrom*cells-cell),
                            point(first,last,to*cells-cell),fade*strength*heat,width*.37f};
                    }
                }
            }
            starts+=span/speed;
        }
        return starts;
    };

    Vector3 rearEntry=origin;
    float rearArrival=0.0f;
    if(origin.z>rearFace) {
        Vector3 onSide=origin;
        if(endHit) {
            onSide={side*sidePlate,origin.y,3.30f};
            rearArrival=trace(std::array{origin,Vector3{side*1.80f,origin.y,3.36f},onSide},0.0f,sideSpeed,.060f,.85f*trunkFade);
        }
        rearEntry={side*1.66f,1.39f,rearFace};
        const Vector3 sideSeam{side*sidePlate,std::lerp(origin.y,1.32f,.68f),std::lerp(onSide.z,-3.28f,.58f)};
        rearArrival=trace(std::array{onSide,sideSeam,Vector3{side*sidePlate,1.37f,-3.31f},rearEntry},
            rearArrival,sideSpeed,.064f,.86f*trunkFade);
    }
    const Vector3 split{std::lerp(rearEntry.x,side*.72f,.64f),std::lerp(rearEntry.y,.81f,.72f),rearFace};
    const Vector3 cross{std::lerp(split.x,-side*.76f,.72f),.55f,rearFace};
    const Vector3 farToe{-side*1.64f,.17f,rearFace};
    const float splitArrival=trace(std::array{rearEntry,split},rearArrival,rearSpeed,.098f,trunkFade);
    const float crossArrival=trace(std::array{split,cross},splitArrival,rearSpeed,.101f,trunkFade);
    const float farArrival=trace(std::array{cross,farToe},crossArrival,rearSpeed,.089f,.98f*trunkFade);
    result.junctions={HotJunction{split,pulse(age-splitArrival,.008f,.049f)*fade},
        HotJunction{cross,pulse(age-crossArrival,.010f,.045f)*fade*.82f}};
    const Vector3 nearToe{side*1.78f,.24f,rearFace};
    const float nearArrival=trace(std::array{split,Vector3{side*1.24f,.48f,rearFace},nearToe},
        splitArrival,rearSpeed*1.34f,.074f,.88f*trunkFade);
    const float firstWheelArrival=heavy?HeavyCoverArrival:(std::max)(nearArrival+.034f,farArrival+.030f-WheelDelay);
    for(size_t i=0;i<rearHubs.size();++i) {
        const auto& hub=rearHubs[i];
        const float wheelSide=std::copysign(1.0f,hub.x);
        const bool struckSide=wheelSide==side;
        const float coverBack=(std::clamp)(hub.z-.44f,-2.86f,-2.04f);
        const Vector3 cover{wheelSide*2.53f,.19f,coverBack};
        const Vector3 coverFoot{wheelSide*2.48f,-.27f,coverBack};
        const float starts=struckSide?nearArrival:farArrival;
        const float arrives=firstWheelArrival+(struckSide?0.0f:WheelDelay);
        const std::array feed{struckSide?nearToe:farToe,Vector3{wheelSide*2.27f,.17f,-3.38f},
            Vector3{wheelSide*2.53f,.14f,-2.90f},cover};
        const float feedLength=length(feed[0],feed[1])+length(feed[1],feed[2])+length(feed[2],feed[3]);
        trace(feed,starts,feedLength/(std::max)(.001f,arrives-starts),.072f,.93f*trunkFade);
        trace(std::array{cover,coverFoot,hub},arrives,38.0f,.057f,.82f);
        const float heat=heavy?pulse(age-arrives,HeavyReboundPeak-HeavyCoverArrival,.058f)*
            (struckSide?1.0f:.56f):0.0f;
        result.wheels[i]={cover,arrives,(std::max)(pulse(age-arrives,.011f,.081f),heat)*fade,heat};

        const float delay=struckSide?0.0f:.021f;
        const float repeat=heavy?pulse(age-.468f-delay,.025f,.090f)*.45f:
            pulse(age-.220f-delay,.012f,.045f)+pulse(age-.298f-delay,.010f,.040f)*.72f;
        const std::array spur{cover,Vector3{wheelSide*2.60f,-.025f,coverBack+.13f},
            Vector3{wheelSide*2.52f,-.16f,coverBack+.32f}};
        trace(spur,arrives,48.0f,.037f,repeat*.92f,false);
        if(drawStrokes && repeat>.12f && result.hotTipCount<result.hotTips.size()) {
            result.hotTips[result.hotTipCount++]={point(spur[1],spur[2],.55f),spur[2],repeat*fade*.81f,.022f};
        }
    }
    trace(std::array{split,Vector3{side*.38f,1.08f,rearFace},Vector3{side*.13f,1.42f,rearFace}},
        splitArrival,36.0f,.035f,.61f*trunkFade,false);
    trace(std::array{cross,Vector3{-side*.24f,.33f,rearFace},Vector3{-side*.28f,.05f,rearFace}},
        crossArrival,34.0f,.038f,.65f*trunkFade,false);
    return result;
}
} // namespace RailCartImpactEnergy
