#pragma once
#include "utils/math/Vector.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace RailGatlingRing {
inline constexpr float Radius=21.0f;
inline constexpr float ShaderRadius=.68f;
inline constexpr float GaugeRadius=26.5f;
inline constexpr float TickInner=30.0f,TickOuter=33.5f;
inline constexpr float TravelSeconds=1.35f;
inline constexpr float CompletionHalfExtent=52.0f;
inline constexpr float CounterShaderRadius=.72f;
inline constexpr float CounterRadius(int part) { return part==1?40.0f:34.0f; }
enum class Layer { Idle, Contact, Completion };
struct DischargeSegment final {Vector2 start{},end{};float width=0;Vector4 color{};};
struct Discharge final {std::array<DischargeSegment,4> segments{};unsigned count=0;};
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

// Four short sparks leave the contact point, outside the aiming area.
inline Discharge BuildDischarge(Vector2 center,float scale,float life,float contactAngle) {
    Discharge result;
    if(!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(scale) || scale<=0 ||
        !std::isfinite(life) || life<=0 || !std::isfinite(contactAngle))return result;
    const float fade=std::pow((std::clamp)(life,0.0f,1.0f),2.0f);
    const auto point=[&](float angle,float radius) {return Vector2{center.x+std::cos(angle)*radius*scale,
        center.y+std::sin(angle)*radius*scale};};
    for(unsigned i=0;i<result.segments.size();++i) {
        const float angle=contactAngle+(float(i)-1.5f)*.14f;
        result.segments[result.count++]={point(angle,Radius+2),point(angle,Radius+5+float(i%3)*2),
            .85f*scale,{.88f,.99f,1,fade*.84f}};
    }
    return result;
}
} // namespace RailGatlingRing
