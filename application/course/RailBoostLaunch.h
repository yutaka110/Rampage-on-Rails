#pragma once
#include <algorithm>
#include <cmath>

// Visual launch sampled by both the defeat reward and the bridge escape.
class RailBoostLaunch final {
public:
    static constexpr float SpinSeconds=.80f,TurnEndSeconds=.72f,AngularRampSeconds=.12f;
    static constexpr float MaximumLift=.40f,LandingCompression=.035f,IgnitionLeadSeconds=.06f;

    explicit RailBoostLaunch(float age=-1) : age_(age) {}
    bool Spinning()const{return age_>=0 && age_<SpinSeconds;}
    float SpinDegrees()const{return Spinning()?360.0f*SpinPhase():0;}
    float Lift()const {
        if(!Spinning())return 0;
        if(age_<.18f)return MaximumLift*Smooth((age_-.02f)/.16f);
        if(age_<.50f)return MaximumLift;
        if(age_<TurnEndSeconds)return MaximumLift*(1-Smooth((age_-.50f)/(TurnEndSeconds-.50f)));
        const float t=(age_-TurnEndSeconds)/(SpinSeconds-TurnEndSeconds);
        const float settle=std::sin(3.141592654f*t);
        return -LandingCompression*settle*settle;
    }
    float SurgePulse()const {
        if(age_<0)return 0;
        return Smooth(age_/.06f)*(1-Smooth((age_-.08f)/.28f));
    }
    float ThrustPulse()const {
        if(age_<0)return 0;
        // Point ignition leads a directional jet; the same launch age drives
        // chassis load and camera catch-up, independently of render frequency.
        return Smooth(age_/.035f)*(1-Smooth((age_-.09f)/.25f));
    }
    float BodyCompression()const {
        if(!Spinning())return 0;
        const float preload=age_<.07f?std::sin(3.141592654f*age_/.07f):0;
        const float landing=age_>TurnEndSeconds?
            std::sin(3.141592654f*(age_-TurnEndSeconds)/(SpinSeconds-TurnEndSeconds)):0;
        return -.075f*preload*preload-.050f*landing*landing;
    }
    float BodyPitchDegrees()const {
        if(!Spinning())return 0;
        return -3.5f*SurgePulse()-.65f*BodyCompression()/.05f;
    }
    float OccupantPitchDegrees()const {
        if(!Spinning())return 0;
        return -2.2f*Smooth((age_-.045f)/.06f)*(1-Smooth((age_-.16f)/.25f));
    }
    float IgnitionPulse()const {
        if(age_<0)return 0;
        const float t=(std::clamp)(1-age_/.30f,0.0f,1.0f);return t*t;
    }
private:
    float SpinPhase()const {
        // Integrate a smooth angular-velocity ramp, a steady middle and a
        // smooth stop. The previous single smoothstep concentrated rotation
        // into a very fast middle. Landing has its own final 80 ms.
        const float duration=TurnEndSeconds-IgnitionLeadSeconds;
        const float t=(std::clamp)(age_-IgnitionLeadSeconds,0.0f,duration);
        const float area=duration-AngularRampSeconds;
        const auto integral=[](float x){return x*x*x*(1-.5f*x);};
        if(t<AngularRampSeconds)return AngularRampSeconds*integral(t/AngularRampSeconds)/area;
        if(t<=duration-AngularRampSeconds)return (t-.5f*AngularRampSeconds)/area;
        return 1-AngularRampSeconds*integral((duration-t)/AngularRampSeconds)/area;
    }
    static float Smooth(float t){t=(std::clamp)(t,0.0f,1.0f);return t*t*(3-2*t);}
    float age_;
};
