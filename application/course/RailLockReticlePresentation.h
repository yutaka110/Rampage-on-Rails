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
    float opacity=1.0f,strokeScale=1.0f,highlight=0.0f;
};
struct RailLockReticleVisual final {
    static constexpr float CaptureDuration=0.15f, ReleaseDuration=0.12f;
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
    auto result=BuildRailReticleCorners(token.acquiredScreenPosition,{rest*1.12f+14.0f*scale*settle,rest+3.0f*scale*settle},
        8.0f*scale,scale,opacity);
    if(!result.visible) return result;
    result.segmentCount=0;
    const auto append=[&](Vector2 a,Vector2 b,float alpha=1.0f,float width=1.0f,float highlight=0.0f) {
        if(result.segmentCount<result.segments.size())
            result.segments[result.segmentCount++]={a,b,alpha,width,highlight};
    };
    const float rx=result.halfExtent.x,ry=result.halfExtent.y;
    // Two shield-like jaws slide inward and untwist as independent rigid
    // pieces. Their small centre notches face the drone without masking it.
    for(float side:{-1.0f,1.0f}) {
        const float angle=side*0.16f*settle,c=std::cos(angle),s=std::sin(angle);
        const auto point=[&](float x,float y) {
            const float dx=side*x;
            return Vector2{result.center.x+side*rx+c*dx-s*y,result.center.y+s*dx+c*y};
        };
        const std::array<Vector2,10> jaw{{{-10*scale,-ry},{-4*scale,-ry},{0,-ry+4*scale},
            {0,-6*scale},{-3*scale,-3*scale},{-3*scale,3*scale},{0,6*scale},
            {0,ry-4*scale},{-4*scale,ry},{-10*scale,ry}}};
        for(size_t i=1;i<jaw.size();++i) {
            float highlight=0;
            if(age>=0.035f && age<0.12f) {
                const float sweep=-1+2*(age-0.035f)/0.085f;
                const float delta=(jaw[i-1].y+jaw[i].y)/(2*ry)-sweep;
                highlight=std::exp(-delta*delta/0.055f)*std::sin((age-0.035f)/0.085f*3.14159265f);
            }
            append(point(jaw[i-1].x,jaw[i-1].y),point(jaw[i].x,jaw[i].y),1,1,highlight);
        }
        append(point(4*scale,-ry*0.38f),point(4*scale,ry*0.38f),0.38f,0.75f);
    }
    result.capture=pulse;
    result.ink={0.20f+0.55f*pulse,0.94f+0.06f*pulse,1.0f,
        (0.88f+0.12f*pulse)*(std::clamp)(opacity,0.0f,1.0f)};
    result.stroke=(1.6f+0.4f*pulse)*scale;
    result.ordinalPosition={result.center.x+result.halfExtent.x-4.0f*scale,
        result.center.y-result.halfExtent.y-10.0f*scale};
    return result;
}

// Authorized lasers own this brief opening/fade, so input release never needs
// another HUD history, wall-clock timer, target lookup or damage request.
inline RailLockReticleVisual BuildRailReleasedReticleVisual(const RailLockToken& token,
        float age,float scale,float opacity,float bodyRadiusPixels=0) {
    if(!std::isfinite(age) || age<0 || age>=RailLockReticleVisual::ReleaseDuration) return {};
    auto result=BuildRailConfirmedReticleVisual(token,token.acquiredTime+RailLockReticleVisual::CaptureDuration,
        scale,opacity,bodyRadiusPixels);
    if(!result.visible) return result;
    scale=(std::clamp)(scale,0.5f,2.0f);
    const float t=age/RailLockReticleVisual::ReleaseDuration,ease=1-(1-t)*(1-t);
    const auto open=[&](Vector2 point,float side) {
        const Vector2 pivot{result.center.x+side*result.halfExtent.x,result.center.y};
        const float angle=-side*0.10f*ease,c=std::cos(angle),s=std::sin(angle);
        const float dx=point.x-pivot.x,dy=point.y-pivot.y;
        return Vector2{pivot.x+side*6*scale*ease+c*dx-s*dy,pivot.y+s*dx+c*dy};
    };
    for(size_t i=0;i<result.segmentCount;++i) {
        auto& segment=result.segments[i];
        const float side=(segment.start.x+segment.end.x)*0.5f<result.center.x?-1.0f:1.0f;
        segment.start=open(segment.start,side);segment.end=open(segment.end,side);segment.highlight=0;
    }
    result.capture=0;result.ink.w*=(1-t)*(1-t);result.edge.w*=(1-t)*(1-t);
    result.halfExtent.x+=6*scale*ease;
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
