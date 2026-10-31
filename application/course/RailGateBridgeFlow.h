#pragma once
#include "CourseSpawnRuntime.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

// Course-specific rhythm, copied with the retry follower state. Combat and
// projectile truth remain authoritative; passing a battery never earns a clear.
class RailGateBridgeFlow final {
public:
    static constexpr float BridgeStart=1640, LookoutDistance=1730;
    static constexpr float ReleaseDuration=3.0f, LookoutEstablishDuration=2.8f;
    enum class Phase { Gate, Release, Lookout, Complete };
    void Reset() { *this={}; }
    void Observe(const CourseSpawnRuntime& runtime) {
        for(const auto& actor:runtime.Enemies()) {
            if(actor.desc.waveId=="short_turret_left") batteries_[0]=actor.actorId;
            if(actor.desc.waveId=="short_turret_right") batteries_[1]=actor.actorId;
            if(actor.desc.waveId=="short-bridge-lookout") lookout_=actor.actorId;
        }
    }
    void Notify(std::span<const EnemyCombatEvent> events) {
        for(const auto& event:events) if(event.kind==EnemyCombatEventKind::Defeated) {
            for(size_t i=0;i<batteries_.size();++i)
                if(batteries_[i]!=0 && event.actorId==batteries_[i]) defeated_[i]=true;
            if(lookout_!=0 && event.actorId==lookout_) lookoutDefeated_=true;
        }
    }
    void Update(float dt,float distance,const CourseSpawnRuntime& runtime,bool combatClear) {
        Observe(runtime);
        dispatchLookout_=false;
        dt=std::isfinite(dt)?(std::clamp)(dt,0.0f,0.25f):0;
        if(dt==0) return;
        clearPulse_=(std::max)(0.0f,clearPulse_-dt);
        if(phase_==Phase::Gate && distance<1950 && defeated_[0] && defeated_[1] && combatClear) {
            if(!clearAcknowledged_) {clearPulse_=1.2f;clearAcknowledged_=true;}
            if(distance>=BridgeStart && distance<1850) {phase_=Phase::Release;releaseSeconds_=0;}
        }
        if(phase_==Phase::Release) {
            // A late in-flight warning/projectile must not consume quiet time.
            if(combatClear) releaseSeconds_+=dt;
            if(releaseSeconds_>=ReleaseDuration && distance>=LookoutDistance && combatClear) {
                phase_=Phase::Lookout;dispatchLookout_=true;lookoutSeconds_=0;
            }
        } else if(phase_==Phase::Lookout) {
            for(const auto& actor:runtime.Enemies()) if(actor.actorId==lookout_ &&
                actor.terrainIngress.finished && actor.entranceExitState.targetable &&
                actor.screenPresenceAttackAllowed) lookoutSeconds_+=dt;
            if(lookoutSeconds_>=LookoutEstablishDuration || lookoutDefeated_ || distance>=2150)
                phase_=Phase::Complete;
        } else if(phase_==Phase::Gate && distance>=1850) {
            phase_=Phase::Complete;
        }
    }
    bool BlockNewEnemies(float distance) const {
        return phase_==Phase::Release || phase_==Phase::Lookout ||
            (distance>=1480 && distance<1900);
    }
    bool ProtectAuthoredCombat(float distance) const {
        return (distance>=1080 && distance<1900) ||
            phase_==Phase::Release || phase_==Phase::Lookout;
    }
    bool TakeLookout() const { return dispatchLookout_; }
    bool ClearedBatteries() const { return defeated_[0] && defeated_[1]; }
    float ScenicSpeed(float distance) const {
        return clearAcknowledged_ && distance>=1480 &&
            (phase_==Phase::Gate || phase_==Phase::Release) ? 36.0f : 0.0f;
    }
    float ClearPulse() const { return clearPulse_/1.2f; }
    float ReleaseSeconds() const { return releaseSeconds_; }
    Phase CurrentPhase() const { return phase_; }
private:
    std::array<uint32_t,2> batteries_{};
    std::array<bool,2> defeated_{};
    uint32_t lookout_=0;
    Phase phase_=Phase::Gate;
    float releaseSeconds_=0,lookoutSeconds_=0,clearPulse_=0;
    bool dispatchLookout_=false,lookoutDefeated_=false,clearAcknowledged_=false;
};
