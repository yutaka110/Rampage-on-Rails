#pragma once

#include "RailLockOnTypes.h"
#include <algorithm>
#include <array>
#include <cmath>

// Pure screen-space layout shared by the native atlas and regression checks.
// The input cursor owns the white dot; confirmed targets own cyan clamps and
// an ordinal. Neither visual changes aim, lock eligibility or firing timing.
struct RailReticleSegment final {
    Vector2 start{},end{};
    float opacity=1.0f,strokeScale=1.0f;
};
struct RailLockReticleVisual final {
    static constexpr float CaptureDuration=0.15f;
    std::array<RailReticleSegment,40> segments{};
    size_t segmentCount=0;
    Vector2 center{},halfExtent{},ordinalPosition{};
    Vector4 ink{},edge{};
    float stroke=0,edgeWidth=0,dotSize=0,capture=0;
    bool visible=false;
};

inline RailLockReticleVisual BuildRailReticleCorners(Vector2 center,Vector2 extent,
        float length,float scale,float opacity) {
    RailLockReticleVisual result;
    if(!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(scale) ||
        !std::isfinite(opacity) || opacity<=0 || scale<=0 ||
        !std::isfinite(extent.x) || !std::isfinite(extent.y)) return result;
    result.visible=true;result.center=center;result.halfExtent=extent;
    result.stroke=1.6f*scale;result.edgeWidth=1.0f*scale;
    result.edge={0.008f,0.014f,0.022f,0.98f*(std::clamp)(opacity,0.0f,1.0f)};
    for(float sx:{-1.0f,1.0f}) for(float sy:{-1.0f,1.0f}) {
        const Vector2 corner{center.x+sx*extent.x,center.y+sy*extent.y};
        result.segments[result.segmentCount++]={corner,{corner.x-sx*length,corner.y}};
        result.segments[result.segmentCount++]={corner,{corner.x,corner.y-sy*length}};
    }
    return result;
}

inline RailLockReticleVisual BuildRailAimReticleVisual(Vector2 center,bool lockHeld,
        float scale,float opacity,float startupSpread=0) {
    if(!std::isfinite(scale) || !std::isfinite(startupSpread)) return {};
    scale=(std::clamp)(scale,0.5f,2.0f);
    const float radius=((lockHeld?19.0f:13.0f)+(std::clamp)(startupSpread,0.0f,1.0f)*9.0f)*scale;
    auto result=BuildRailReticleCorners(center,{radius,radius},5.0f*scale,scale,opacity);
    result.ink={0.96f,0.98f,1.0f,(std::clamp)(opacity,0.0f,1.0f)};
    result.dotSize=2.6f*scale;
    return result;
}

inline RailLockReticleVisual BuildRailConfirmedReticleVisual(const RailLockToken& token,
        float elapsed,float scale,float opacity,float bodyRadiusPixels=0) {
    if(!std::isfinite(elapsed) || !std::isfinite(token.acquiredTime) ||
        !std::isfinite(scale) || !std::isfinite(bodyRadiusPixels)) return {};
    scale=(std::clamp)(scale,0.5f,2.0f);
    const float age=(std::max)(0.0f,elapsed-token.acquiredTime);
    const float pulse=(std::max)(0.0f,1.0f-age/RailLockReticleVisual::CaptureDuration);
    const float settle=pulse*pulse*pulse;
    const float rest=(std::clamp)((std::max)(23.0f*scale,bodyRadiusPixels*1.12f+5.0f*scale),23.0f*scale,60.0f*scale);
    const float radius=rest+12.0f*scale*settle;
    auto result=BuildRailReticleCorners(token.acquiredScreenPosition,{radius*1.12f,radius},
        8.0f*scale,scale,opacity);
    if(!result.visible) return result;
    result.segmentCount=0;
    const auto append=[&](Vector2 a,Vector2 b,float alpha=1.0f,float width=1.0f) {
        if(result.segmentCount<result.segments.size())
            result.segments[result.segmentCount++]={a,b,alpha,width};
    };
    const auto point=[&](float x,float y) {return Vector2{result.center.x+x,result.center.y+y};};
    const float rx=result.halfExtent.x,ry=result.halfExtent.y;
    // Four open, chamfered clamps. The bevel echoes the drone's armour; the
    // centre and middle of each edge stay clear even with eight stored locks.
    const float bevel=4.0f*scale,leg=7.0f*scale;
    for(float sx:{-1.0f,1.0f}) for(float sy:{-1.0f,1.0f}) {
        const auto a=point(sx*(rx-bevel-leg),sy*ry);
        const auto b=point(sx*(rx-bevel),sy*ry);
        const auto c=point(sx*rx,sy*(ry-bevel));
        const auto d=point(sx*rx,sy*(ry-bevel-leg));
        append(a,b);append(b,c);append(c,d);
    }
    // Small inward jaws remain outside the silhouette, with a single inward
    // snap on acquisition. They never oscillate while the target is held.
    for(float side:{-1.0f,1.0f}) {
        const float tip=side*(rx+(4.0f+5.0f*settle)*scale);
        const float back=tip+side*4.0f*scale;
        append(point(back,-3.0f*scale),point(tip,0),0.76f,0.85f);
        append(point(tip,0),point(back,3.0f*scale),0.76f,0.85f);
    }
    for(float side:{-1.0f,1.0f})
        append(point(-2.0f*scale,side*(ry+4.0f*scale)),
            point(2.0f*scale,side*(ry+4.0f*scale)),0.50f,0.75f);
    // Two short opposed arcs sweep inward once and disappear before hold.
    // No complete ring, rotating hold animation or target-centre flash.
    if(pulse>0.001f) {
        constexpr float pi=3.14159265358979323846f;
        const float arcRadius=radius+(7.0f+9.0f*settle)*scale;
        for(float side:{0.0f,1.0f}) for(int i=0;i<8;++i) {
            const float angle=side*pi-0.36f+0.32f*settle+0.72f*float(i)/8.0f;
            const float next=angle+0.72f/8.0f;
            append(point(std::cos(angle)*arcRadius*1.12f,std::sin(angle)*arcRadius),
                point(std::cos(next)*arcRadius*1.12f,std::sin(next)*arcRadius),
                pulse*0.65f,0.7f);
        }
    }
    result.capture=pulse;
    result.ink={0.20f+0.80f*pulse,0.94f+0.06f*pulse,1.0f,
        (0.88f+0.12f*pulse)*(std::clamp)(opacity,0.0f,1.0f)};
    result.stroke=(1.6f+0.4f*pulse)*scale;
    result.ordinalPosition={result.center.x+result.halfExtent.x-1.0f*scale,
        result.center.y-result.halfExtent.y-11.0f*scale};
    return result;
}

struct RailLockCaptureSound final {
    bool play=false,full=false;
    float volume=0,pitch=1,pan=0;
};

inline RailLockCaptureSound BuildRailLockCaptureSound(const RailLockDebugFrame& frame,
        int maximumLocks,uint32_t viewportWidth,float gameplayDeltaTime) {
    RailLockCaptureSound result;
    if(!std::isfinite(gameplayDeltaTime) || gameplayDeltaTime<=0 || viewportWidth==0 ||
        frame.acceptedThisFrame<=0 || frame.acquiredTokens.empty()) return result;
    const auto point=frame.acquiredTokens.back().acquiredScreenPosition;
    if(!std::isfinite(point.x) || !std::isfinite(point.y)) return result;
    const int count=static_cast<int>(frame.tokens.size());
    result.play=true;result.full=count>=(std::max)(1,maximumLocks);
    result.volume=result.full?0.30f:0.25f;
    // One semitone per confirmed target. Capacity has its own short tone.
    result.pitch=result.full?1.0f:std::pow(2.0f,float((std::clamp)(count,1,8)-1)/12.0f);
    result.pan=(std::clamp)(point.x/float(viewportWidth)*2-1,-0.65f,0.65f);
    return result;
}
