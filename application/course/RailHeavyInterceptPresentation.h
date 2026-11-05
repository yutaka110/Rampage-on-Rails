#pragma once
#include "RailLockOnTypes.h"
#include <algorithm>
#include <cmath>
#include <span>

// Observes real lock tokens and accepted interception events. It never awards
// locks, destroys a projectile, or changes the combat damage rules.
class RailHeavyInterceptPresentation final {
public:
    enum class Step { Hidden, Aim, Hold, Release, InFlight, Counter, Hit, Miss };
    static constexpr float ImpactScale=.10f, ImpactHoldSeconds=.035f, ImpactEndSeconds=.11f;
    void Reset(){*this={};}
    void ResetAttempt(){const bool intercepted=intercepted_;Reset();intercepted_=intercepted;}
    bool HasIntercepted()const{return intercepted_;}
    Step Current()const{return step_;}
    bool Visible()const{return step_!=Step::Hidden;}
    bool Defending()const{return step_>=Step::Aim && step_<=Step::InFlight;}
    unsigned Locks()const{return locks_;}
    uint64_t Projectile()const{return projectile_;}
    uint32_t Owner()const{return owner_;}
    float Age()const{return age_;}
    float SuccessAge()const{return successAge_;}
    Vector3 SuccessPoint()const{return successPoint_;}
    float Flash()const{return successPulse_?(std::clamp)(1-successAge_/.22f,0.0f,1.0f):0;}
    float SuccessOpacity()const{return successPulse_?(std::clamp)((.6f-successAge_)/.18f,0.0f,1.0f):0;}
    float TimeScale()const {
        if(successPulse_)return ImpactScale+(1-ImpactScale)*(std::clamp)((successAge_-ImpactHoldSeconds)/(ImpactEndSeconds-ImpactHoldSeconds),0.0f,1.0f);
        return 1;
    }
    void Begin(uint64_t projectile,uint32_t owner) {
        if(Defending() || !projectile || !owner || projectile==projectile_)return;
        projectile_=projectile;owner_=owner;locks_=0;age_=0;step_=Step::Aim;
    }
    void Advance(float realDt,bool live,bool ownerAlive,float counterRemaining,bool counterHit) {
        if(!std::isfinite(realDt) || realDt<=0)return; // pause/focus cannot age a prompt
        const float dt=(std::min)(realDt,.1f);
        age_+=dt;
        if(successPulse_) {successAge_+=dt;if(successAge_>.6f)successPulse_=false;}
        if(Defending() && (!live || !ownerAlive)) {step_=ownerAlive?Step::Miss:Step::Hidden;age_=0;}
        if(step_==Step::Counter) {
            if(!ownerAlive || counterHit){step_=counterHit?Step::Hit:Step::Hidden;age_=0;}
            else if(counterRemaining<=0 && successAge_>.1f)step_=Step::Hidden;
        }
        if((step_==Step::Hit && age_>1.2f) || (step_==Step::Miss && age_>1.8f))step_=Step::Hidden;
    }
    bool ObserveInput(std::span<const RailLockToken> tokens,std::span<const RailLockToken> release,bool held) {
        if(!Defending())return false;
        const auto count=[this](std::span<const RailLockToken> list) {
            return unsigned(std::count_if(list.begin(),list.end(),[this](const auto& t){
                return t.target.kind==RailLockTargetKind::Projectile && t.target.projectileId==projectile_;
            }));
        };
        const auto previous=step_;
        // Preserve the firing state while actual lasers travel. Partial release
        // stays targetable, so another lock can finish the still-living shell.
        if(count(release)>=2){locks_=2;step_=Step::InFlight;}
        else if(step_!=Step::InFlight) {
            locks_=(std::min)(2u,count(tokens));
            step_=locks_>=2?Step::Release:held?Step::Hold:Step::Aim;
        }
        return step_==Step::Release && previous!=Step::Release;
    }
    bool Intercept(uint64_t projectile,uint32_t owner,Vector3 point={}) {
        if(!Defending() || projectile!=projectile_ || owner!=owner_)return false;
        intercepted_=true;step_=Step::Counter;successAge_=0;age_=0;
        successPulse_=true;successPoint_=point;return true;
    }
private:
    Step step_=Step::Hidden;
    bool intercepted_=false;
    uint64_t projectile_=0;
    uint32_t owner_=0;
    unsigned locks_=0;
    float age_=0,successAge_=0;
    bool successPulse_=false;
    Vector3 successPoint_{};
};

// Teach the common control on the early LOCK formation, before the boss adds
// interception. Visibility never blocks play or changes input/combat clocks.
class RailLockIntroPresentation final {
public:
    void Reset(){*this={};}
    bool Visible()const{return visible_;}
    bool Completed()const{return completed_;}
    unsigned Locks()const{return locks_;}
    void Update(float dt,bool earlySection,bool targetVisible,unsigned locks,bool fired) {
        if(!std::isfinite(dt) || dt<=0)return;
        locks_=locks;
        if(!earlySection){visible_=false;return;}
        if(fired && visible_) {completed_=true;successAge_=0;}
        else if(completed_)successAge_+=dt;
        visible_=completed_?successAge_<1.2f:targetVisible;
    }
private:
    bool visible_=false,completed_=false;
    unsigned locks_=0;
    float successAge_=0;
};
