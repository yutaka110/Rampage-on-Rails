#pragma once
#include "EnemyCombatSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

// A flyby can protect enemies without taking the player's controls. Reward
// activation follows player control, independently of enemy-fire/camera cues.
struct RailKillBoostControlInput final {
    bool gameplayActive=false,playerInputEnabled=false,vehicleMounted=false;
    bool startupBlocked=true,previewOwnsRail=false;
    bool AllowsActivation()const {
        return gameplayActive && playerInputEnabled && vehicleMounted && !startupBlocked && !previewOwnsRail;
    }
};

// Run-owned reward state, copied with the speed director for both checkpoint
// retry and future travel. Rendering never increments charge or starts a boost.
class RailKillBoost final {
public:
    static constexpr unsigned RequiredKills=6;
    static constexpr float SpinSeconds=.80f,HoldSeconds=4.0f,ReleaseSeconds=.70f;
    static constexpr float AccelerationSeconds=.20f;
    static constexpr float LaunchFraction=.95f,TurnEndSeconds=.72f,AngularRampSeconds=.12f;
    static constexpr float MaximumLift=.40f,LandingCompression=.035f;
    static constexpr float IgnitionLeadSeconds=.06f,KillConfirmationSeconds=.16f;
    static constexpr float TotalSeconds=SpinSeconds+HoldSeconds+ReleaseSeconds;
    static constexpr float MaximumMultiplier=1.40f;
    static constexpr float KillExtensionSeconds=1.50f,MaximumHoldRemaining=6.0f;
    void Reset(){*this=RailKillBoost{};}
    bool ObserveDefeat(const EnemyCombatEvent& event) {
        if(event.kind!=EnemyCombatEventKind::Defeated || !event.actorId || !event.shotId ||
            event.feedbackKind!=HitFeedbackKind::Destroyed || !std::isfinite(event.appliedDamage) || event.appliedDamage<=0 ||
            !std::isfinite(event.remainingHitPoints) || event.remainingHitPoints>0 || event.actorAssetId.empty() ||
            event.actorAssetId.starts_with("short_mothership") || event.waveId.starts_with("ready") ||
            event.waveId.starts_with("startup") || !counted_.insert(event.actorId).second)return false;
        ++totalKills_;
        if(Active()) {
            const float before=Blend();
            const float previousExpiry=expiresAt_;
            const bool wasReleasing=age_>ReleaseStartAge();
            expiresAt_=(std::min)((std::max)(expiresAt_,age_+ReleaseSeconds)+KillExtensionSeconds,
                age_+MaximumHoldRemaining+ReleaseSeconds);
            if(wasReleasing) { recoveryAge_=age_;recoveryBlend_=before; }
            if(expiresAt_>previousExpiry+.00001f) {lastExtensionAge_=age_;++extensions_;}
        } else charge_=(std::min)(RequiredKills,charge_+1);
        return true;
    }
    // Called after each accepted defeat, before presentation is submitted.
    // Starting at age zero makes the point light audible/visible this frame;
    // authoritative movement advances on the next simulation step.
    bool ActivatePending(bool gameplayActive=true,bool activationAllowed=true) {
        if(!gameplayActive || !activationAllowed || Active() || charge_<RequiredKills)return false;
        charge_-=RequiredKills;age_=0;expiresAt_=TotalSeconds;recoveryAge_=-1;lastExtensionAge_=-1;
        ++serial_;started_=true;allowed_=true;return true;
    }
    void Update(float deltaTime,bool gameplayActive=true,bool activationAllowed=true) {
        if(!gameplayActive || !std::isfinite(deltaTime) || deltaTime<=0)return;
        started_=false;allowed_=activationAllowed;
        if(!allowed_)return; // retain a full charge and any interrupted effect
        float remaining=(std::min)(deltaTime,.25f);
        while(remaining>.000001f) {
            const float dt=(std::min)(remaining,1.0f/120.0f);
            ActivatePending(true,allowed_);
            if(Active()) {
                age_+=dt;
                if(age_>=expiresAt_-.00001f)age_=-1;
            }
            remaining-=dt;
        }
    }
    unsigned Charge()const{return charge_;}
    uint64_t TotalKills()const{return totalKills_;}
    uint64_t Serial()const{return serial_;}
    uint64_t Extensions()const{return extensions_;}
    bool Active()const{return age_>=0;}
    bool ActivationAllowed()const{return allowed_;}
    bool StartedThisFrame()const{return started_;}
    bool Spinning()const{return allowed_ && Active() && age_<SpinSeconds;}
    bool Accelerating()const{return allowed_ && Active() && (age_<=AccelerationSeconds+.05f ||
        (recoveryAge_>=0 && age_-recoveryAge_<=AccelerationSeconds+.05f));}
    float Age()const{return age_;}
    float ReleaseStartAge()const{return expiresAt_-ReleaseSeconds;}
    float Remaining()const{return Active()?(std::max)(0.0f,expiresAt_-age_):0;}
    float HoldRemaining()const{return Active()?(std::max)(0.0f,ReleaseStartAge()-age_):0;}
    float ExtensionPulse()const{return Active() && allowed_ && lastExtensionAge_>=0?
        1-Smooth((age_-lastExtensionAge_)/.35f):0;}
    float Blend()const {
        if(!Active() || !allowed_)return 0;
        if(age_<AccelerationSeconds)return LaunchFraction*Smooth(age_/AccelerationSeconds);
        if(age_<SpinSeconds)return LaunchFraction+(1-LaunchFraction)*Smooth((age_-AccelerationSeconds)/(SpinSeconds-AccelerationSeconds));
        const float release=age_>ReleaseStartAge()?Smooth((expiresAt_-age_)/ReleaseSeconds):1;
        const float recovery=recoveryAge_<0?1:recoveryBlend_+(1-recoveryBlend_)*Smooth((age_-recoveryAge_)/AccelerationSeconds);
        return (std::min)(release,recovery);
    }
    float SpeedMultiplier()const{return 1+(MaximumMultiplier-1)*Blend();}
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
        if(!Active() || !allowed_)return 0;
        return Smooth(age_/.06f)*(1-Smooth((age_-.08f)/.28f));
    }
    float ThrustPulse()const {
        if(!Active() || !allowed_)return 0;
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
        if(!Active() || !allowed_)return 0;
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
    std::unordered_set<uint32_t> counted_;
    unsigned charge_=0;
    uint64_t totalKills_=0,serial_=0;
    uint64_t extensions_=0;
    float age_=-1;
    float expiresAt_=TotalSeconds,recoveryAge_=-1,recoveryBlend_=1,lastExtensionAge_=-1;
    bool started_=false,allowed_=true;
};
