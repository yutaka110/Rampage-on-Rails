#pragma once
#include "RailKillBoost.h"

struct RailKillBoostFeedbackFrame final {
    bool visible=false,ignitionSound=false,releaseSound=false;
    float strength=0,ignition=0;
};

inline bool RailBoostHasAcceleration(float speedBefore,float speedAfter,float requested,float slowdown) {
    return std::isfinite(speedBefore) && std::isfinite(speedAfter) && std::isfinite(requested) &&
        std::isfinite(slowdown) && slowdown<=0 && requested>0 &&
        (requested>speedBefore+.0001f || speedAfter>speedBefore+.0001f);
}

// Presentation follows achieved speed, not a key or a requested multiplier.
// Sound transitions are read-only, so rendering cannot consume a reward.
inline RailKillBoostFeedbackFrame BuildRailKillBoostFeedback(const RailKillBoost& boost,
    float achievedStrength,float previousAge,bool canAccelerate,float deltaTime) {
    RailKillBoostFeedbackFrame frame;
    frame.visible=boost.Active() && boost.ActivationAllowed();
    if(!frame.visible)return frame;
    frame.strength=std::isfinite(achievedStrength)?(std::clamp)(achievedStrength,0.0f,1.0f):0;
    frame.ignition=canAccelerate?boost.IgnitionPulse():0;
    if(!std::isfinite(deltaTime) || deltaTime<=0)return frame;
    frame.ignitionSound=boost.StartedThisFrame() && frame.ignition>0;
    constexpr float releaseAge=RailKillBoost::SpinSeconds+RailKillBoost::HoldSeconds;
    frame.releaseSound=previousAge>=0 && previousAge<releaseAge && boost.Age()>=releaseAge && frame.strength>.02f;
    return frame;
}
