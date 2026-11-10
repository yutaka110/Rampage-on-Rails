#pragma once
#include "PlayerHitboxSystem.h"
#include <algorithm>
#include <cmath>

namespace RailVeilRearBeam {
inline constexpr float TrackingSeconds = 1.10f;
inline constexpr float LockSeconds = .70f;
inline constexpr float ChargeSeconds = TrackingSeconds + LockSeconds;
inline constexpr float FadeSeconds = .22f;
inline constexpr float Speed = 135.0f;
inline constexpr float HalfWidth = 2.70f;
inline constexpr float HalfHeight = 1.60f;

enum class Phase { Inactive, Tracking, Locked, Flash };
struct Events { bool locked=false, fired=false, hit=false; };

struct Frame {
    Phase phase = Phase::Inactive;
    Vector3 source{}; // Lateral offset, height and rail distance of the discharge socket.
    float height = 1.4f;
    float end = 0, front = 0, secondsToFire = 0, flashAge = 0;
    float fadeAge = 0;
    bool contacted = false;
    bool firstChargeFrame = false;

    void Begin(float playerDistance, float playerHeight) {
        *this = {};
        phase = Phase::Tracking;
        height = playerHeight;
        // Cruising reaches the advancing front before this mark. A cart
        // that builds speed during the charge can get beyond it first.
        end = playerDistance + 36.45f * ChargeSeconds + 12.0f;
        firstChargeFrame = true;
        secondsToFire = ChargeSeconds;
    }

    bool Charging() const { return phase==Phase::Tracking || phase==Phase::Locked; }
    float Fade() const { return 1-(std::min)(1.0f,fadeAge/FadeSeconds); }

    Vector2 Center(float distance,Vector3 muzzle) const {
        // The short aperture taper joins the muzzle to the centerline.
        const float u=(std::clamp)((distance-muzzle.z)/14.0f,0.0f,1.0f);
        const float blend=u*u*(3-2*u);
        return {std::lerp(muzzle.x,0.0f,blend),std::lerp(muzzle.y,height,blend)};
    }

    Vector2 Center(float distance) const { return Center(distance,source); }

    void Launch(Vector3 muzzle) {
        phase=Phase::Flash;
        source=muzzle;
        front=muzzle.z;
        secondsToFire=0;
    }

    bool Advance(float dt,const PlayerHitboxRuntimeState& player,float frameStart=0) {
        const float uncapped=front+dt*Speed;
        const float lastFront=front;
        const float untilEnd=(std::clamp)((end-lastFront)/Speed,0.0f,dt);
        flashAge+=dt;
        front=(std::min)(uncapped,end);
        fadeAge+=dt-untilEnd;
        if(contacted || untilEnd<=0 || !player.initialized || player.motionHistoryResetThisFrame)
            return false;

        // Solve the crossing against the moving cart before clamping the tip.
        // A cart beyond the fixed endpoint has escaped, including during a long frame.
        const float cartStart=std::lerp(player.previousDistance,player.distance,frameStart);
        const float separation=cartStart-lastFront;
        const float closing=(uncapped-lastFront)-(player.distance-cartStart);
        if(separation<0 || closing<=0 || separation>closing)return false;
        const float t=separation/closing;
        const float distance=std::lerp(lastFront,uncapped,t);
        if(distance>end)return false;
        const float motionT=std::lerp(frameStart,1.0f,t);
        const auto center=Center(distance);
        const float x=std::lerp(player.previousLateralOffset,player.lateralOffset,motionT)-center.x;
        const float y=std::lerp(player.previousVerticalOffset,player.verticalOffset,motionT)-center.y;
        const float outsideX=(std::max)(0.0f,std::abs(x)-HalfWidth);
        const float outsideY=(std::max)(0.0f,std::abs(y)-HalfHeight);
        contacted=std::hypot(outsideX,outsideY)<=player.hurtRadius;
        return contacted;
    }
};
}
