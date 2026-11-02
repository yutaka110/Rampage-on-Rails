#pragma once
#include "utils/math/Vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace RailGatlingRing {
inline constexpr float Radius=21.0f;
inline constexpr float ShaderRadius=.68f;
inline constexpr float GaugeRadius=26.5f;
inline constexpr float TickInner=30.0f,TickOuter=33.5f;
inline constexpr Vector4 Energy{.16f,.80f,1.0f,.96f};
struct DischargeSegment final {Vector2 start{},end{};float width=0;Vector4 color{};};
struct Discharge final {std::array<DischargeSegment,32> segments{};unsigned count=0;};
struct Guide final {
    std::array<DischargeSegment,48> track{},fill{};
    std::array<DischargeSegment,4> ticks{};
    unsigned fillCount=0;
    bool valid=false;
};

// The outside gauge represents actual intercepted rounds, clockwise from the
// top. It never advances from elapsed time or from merely holding the trigger.
inline Guide BuildGuide(Vector2 center,float scale,float progress) {
    Guide result;
    if(!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(scale) || scale<=0 ||
        !std::isfinite(progress))return result;
    progress=(std::clamp)(progress,0.0f,1.0f);
    constexpr float tau=6.283185307f,start=-tau*.25f;
    const auto point=[&](float angle,float radius) {return Vector2{center.x+std::cos(angle)*radius*scale,
        center.y+std::sin(angle)*radius*scale};};
    for(unsigned i=0;i<48;++i) {
        const float from=float(i)/48,to=float(i+1)/48;
        result.track[i]={point(start+tau*from,GaugeRadius),point(start+tau*to,GaugeRadius),
            2.2f*scale,{.015f,.06f,.08f,.52f}};
        if(from<progress)result.fill[result.fillCount++]={point(start+tau*from,GaugeRadius),
            point(start+tau*(std::min)(to,progress),GaugeRadius),1.7f*scale,{.28f,.92f,1,.96f}};
    }
    for(unsigned i=0;i<4;++i) {
        const float angle=start+tau*float(i)*.25f;
        result.ticks[i]={point(angle,TickInner),point(angle,TickOuter),1.1f*scale,{.88f,.98f,1,.94f}};
    }
    result.valid=true;
    return result;
}

// Bounded strokes hug the outside of the original circular guide. Neither the
// steady material nor this transient feedback paints across its open centre.
inline Discharge BuildDischarge(Vector2 center,float scale,float life,uint64_t sequence,float contactAngle) {
    Discharge result;
    if(!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(scale) || scale<=0 ||
        !std::isfinite(life) || life<=0 || !std::isfinite(contactAngle))return result;
    const float fade=std::pow((std::clamp)(life,0.0f,1.0f),1.35f);
    const auto point=[&](float angle,float radius) {return Vector2{center.x+std::cos(angle)*radius*scale,
        center.y+std::sin(angle)*radius*scale};};
    const auto add=[&](Vector2 a,Vector2 b,float width,Vector4 color) {
        if(result.count<result.segments.size())result.segments[result.count++]={a,b,width*scale,color};
    };
    for(unsigned arc=0;arc<2;++arc) {
        const unsigned steps=arc?7u:13u;
        const float base=contactAngle+(arc?2.35f:-.72f);
        const auto jitter=[&](unsigned i) {
            const uint64_t seed=(sequence+1)*0x9e3779b97f4a7c15ULL+(i+arc*19)*0x85ebca6bULL;
            return float((seed^(seed>>17))&255u)/255.0f;
        };
        Vector2 previous=point(base,Radius+1.0f+jitter(0)*2.8f);
        for(unsigned i=1;i<=steps;++i) {
            const Vector2 next=point(base+i*.11f,Radius+1.0f+jitter(i)*2.8f);
            add(previous,next,arc?.65f:1.0f,{.72f,.96f,1,fade*(arc?.62f:.95f)});
            previous=next;
        }
    }
    // The contact glint is outward-only, without a centre-covering starburst.
    for(unsigned i=0;i<4;++i) {
        const float angle=contactAngle+(float(i)-1.5f)*.17f;
        add(point(angle,Radius+2),point(angle,Radius+6+float(i%3)*2),.8f,{.88f,.99f,1,fade*.9f});
    }
    return result;
}
} // namespace RailGatlingRing
