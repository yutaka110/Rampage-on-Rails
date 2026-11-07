#pragma once
#include "CourseSpawnRuntime.h"
#include "RailBoostLaunch.h"
#include "RailVehicleRideArc.h"
#include "CourseAsset.h"
#include <array>
#include <cmath>

// A confirmed lock impact, not the release button, opens the escape route.
// Real time owns readability; scaled world time still owns lasers and travel.
class RailBridgeEscape final {
public:
    enum class Phase { Dormant, Warning, Turn, Aim, Release, Boost, Flight, Landing, Clear };
    static constexpr const char* TargetId="bridge_escape_release";
    static constexpr float FireSeconds=1.70f, CollapseSeconds=2.55f, ReleaseSeconds=1.05f;
    static constexpr float ImpactSeconds=FireSeconds+.14f;
    static constexpr float LandingSeconds=1.2f;
    static constexpr float LaunchSpeed=78.0f;
    void Begin(float distance,Vector3 source,float bridgeOrigin=0,float entrySpeed=36) {
        *this={};phase_=Phase::Warning;anchor_=distance;source_=source;
        const float stoppingRoom=(std::max)(90.0f,entrySpeed*entrySpeed/84.0f+50);
        arc_.start=bridgeOrigin+std::ceil((distance+stoppingRoom-bridgeOrigin)/20)*20;
    }
    void Tick(float dt,float distance,CourseSpawnRuntime& runtime) {
        if(!Active() || !std::isfinite(dt) || dt<=0)return;
        dt=(std::min)(dt,.1f);age_+=dt;phaseAge_+=dt;distance_=distance;
        if(phase_==Phase::Warning && age_>=ImpactSeconds)Enter(Phase::Turn);
        if(phase_==Phase::Turn && age_>=ImpactSeconds+CollapseSeconds) {
            Enter(Phase::Aim);
            CourseObstacleActorDesc target;target.id=TargetId;target.meshId="bridge_escape_clamp";
            target.spawnDistance=TargetRail().z;target.lateralOffset=TargetRail().x;target.verticalOffset=TargetRail().y;
            target.hitPoints=30;target.halfExtents={2.2f,2.6f,.8f};target.lifetime=180;
            target.payload=TargetId;target.color={1,.78f,.28f,1};
            const auto before=runtime.Obstacles().size();
            if(runtime.SpawnObstacle(target) && runtime.Obstacles().size()>before)target_=runtime.Obstacles().back().actorId;
        }
        if(phase_==Phase::Aim && phaseAge_>=(inFlight_?4.0f:5.5f))retryPending_=true;
        if(phase_==Phase::Release && phaseAge_>=ReleaseSeconds) {
            launchEntryRequest_=ApproachSpeed();
            launchStartAge_=age_;
            arc_.enabled=true;
            Enter(Phase::Boost);
        }
        if(phase_==Phase::Boost && distance>=arc_.Launch())Enter(Phase::Flight);
        if(phase_==Phase::Flight && distance>=arc_.Landing()){Enter(Phase::Landing);landed_=true;}
        if(phase_==Phase::Landing && phaseAge_>=LandingSeconds)Enter(Phase::Clear);
    }
    void ObserveRelease(uint32_t target){if(Aiming() && target==target_){inFlight_=true;phaseAge_=0;}}
    bool AcceptLockHit(uint32_t target,bool destroyed) {
        if(!Aiming() || target!=target_ || !destroyed)return false;
        releaseTimeScale_=TimeScale();
        inFlight_=false;retryPending_=false;Enter(Phase::Release);return true;
    }
    bool TakeRetry(){const bool value=retryPending_;retryPending_=false;return value;}
    bool TakeLanding(){const bool value=landed_;landed_=false;return value;}
    bool Active()const{return phase_!=Phase::Dormant;}
    bool Aiming()const{return phase_==Phase::Aim;}
    bool InFlight()const{return inFlight_;}
    bool OwnsControls()const{return Active() && !Aiming() && phase_!=Phase::Clear;}
    bool Complete()const{return phase_==Phase::Clear;}
    bool GapOpen()const{return Active() && age_>=ImpactSeconds;}
    bool CheckpointPending()const{return Aiming() && !checkpoint_;}
    void MarkCheckpoint(bool value){checkpoint_=value;}
    Phase Current()const{return phase_;}
    float Age()const{return age_;}
    float PhaseAge()const{return phaseAge_;}
    float Anchor()const{return anchor_;}
    uint32_t Target()const{return target_;}
    Vector3 Source()const{return source_;}
    const RailVehicleRideArc& Arc()const{return arc_;}
    Vector3 TargetRail()const{return {-6.6f,6.5f,arc_.start+25};}
    float Deploy()const{return phase_==Phase::Release?Smooth((phaseAge_-.14f)/(ReleaseSeconds-.14f)):phase_>=Phase::Boost?1.0f:0;}
    float TimeScale()const {
        if(Aiming())return inFlight_?.55f:.22f;
        if(phase_==Phase::Release) {
            constexpr float impactSeconds=.08f;
            if(phaseAge_<impactSeconds)return std::lerp(releaseTimeScale_,.12f,Smooth(phaseAge_/impactSeconds));
            return std::lerp(.12f,1.0f,Smooth((phaseAge_-impactSeconds)/.78f));
        }
        if(phase_==Phase::Flight) {
            const float slowIn=Smooth((distance_-arc_.Launch())/24);
            const float slowOut=Smooth((arc_.Landing()-distance_)/32);
            return 1.0f-.35f*slowIn*slowOut;
        }
        return 1.0f;
    }
    float RequestedSpeed()const {
        if(phase_<=Phase::Release)return ApproachSpeed();
        if(phase_<=Phase::Flight)return std::lerp(launchEntryRequest_,LaunchSpeed,Smooth(LaunchAge()/.48f));
        return std::lerp(LaunchSpeed,52.0f,LandingBlend());
    }
    RailBoostLaunch LaunchPresentation()const{return RailBoostLaunch(LaunchAge());}
    float LandingBlend()const {
        if(Complete())return 1.0f;
        return phase_==Phase::Landing?Smooth(phaseAge_/LandingSeconds):0.0f;
    }
    float RearWeight()const {
        return phase_==Phase::Warning?1.0f:0.0f;
    }
    float ImpactCutFlash()const {
        if(!Active())return 0.0f;
        const float t=age_-ImpactSeconds;
        return .85f*(1.0f-Smooth(std::abs(t)/(t<0?.045f:.09f)));
    }
    CourseCameraViewDefinition View(bool shooterAvailable=true)const {
        auto v=CourseCameraViewDefinition::Preset(CourseCameraViewMode::Custom);v.hideVehicle=false;
        const float rear=RearWeight()*(shooterAvailable?1.0f:1.0f-Smooth(age_/.72f));
        v.positionOffset={0,std::lerp(10.2f,7.8f,rear),std::lerp(-22.0f,17.0f,rear)};
        v.yaw=rear*3.14159265f;v.pitch=-.08f;v.fovY=std::lerp(.88f,.94f,rear);
        if(phase_==Phase::Turn) {
            const float sinceImpact=age_-ImpactSeconds;
            const float bridgeShot=1.0f-Smooth((sinceImpact-.94f)/1.31f);
            // See the joint from above and off the beam axis, then return to the cart.
            const float joint=arc_.Launch()+50;
            v.positionOffset={15.0f*bridgeShot,std::lerp(10.2f,14.0f,bridgeShot),
                std::lerp(-22.0f,joint-distance_-39.0f,bridgeShot)};
            v.yaw=std::atan2(-15.0f,39.0f)*bridgeShot;
            v.pitch=std::lerp(-.08f,-.39f,bridgeShot);
            v.fovY=std::lerp(.88f,.76f,bridgeShot);
        }
        if(phase_==Phase::Boost || phase_==Phase::Flight || phase_==Phase::Landing) {
            const float side=Smooth(LaunchAge()/.75f)*Smooth((distance_-arc_.start+28)/65);
            v.positionOffset={-14*side,10.2f-2.4f*side+arc_.Height(distance_),-22+4*side};
            v.yaw=.52f*side;
            v.fovY=.88f+.25f*side;
        }
        return v;
    }
private:
    float ApproachSpeed()const{return distance_>=arc_.start-24?0.0f:12.0f;}
    float LaunchAge()const{return launchStartAge_>=0 && !Complete()?age_-launchStartAge_:-1.0f;}
    static float Smooth(float t){t=(std::clamp)(t,0.0f,1.0f);return t*t*(3-2*t);}
    void Enter(Phase p){phase_=p;phaseAge_=0;}
    Phase phase_=Phase::Dormant;
    RailVehicleRideArc arc_{};
    Vector3 source_{};
    float anchor_=0,age_=0,phaseAge_=0,distance_=0;
    float releaseTimeScale_=.22f,launchStartAge_=-1,launchEntryRequest_=0;
    uint32_t target_=0;
    bool checkpoint_=false,inFlight_=false,retryPending_=false,landed_=false;
};
