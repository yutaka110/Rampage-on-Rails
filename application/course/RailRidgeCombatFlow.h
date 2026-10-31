#pragma once
#include "CourseSpawnRuntime.h"
#include "EnemyEncounterReadabilityDirector.h"
#include "RailWorldRaycast.h"
#include <array>
#include <cmath>
#include <span>

// The first ridge crest is at ~2513m on this authored course. All opponents
// keep their combat ownership; the vista only suspends new attack commitment.
class RailRidgeCombatFlow final {
public:
    static constexpr float Begin=1950, CrestBegin=2480, CrestEnd=2540;
    static constexpr float DiveBegin=2560, LastEntry=2680, End=2820;
    enum class Phase { Climb, Crest, Left, Right, Complete };
    void Reset() { *this={}; }
    static bool Active(float distance) { return distance>=Begin && distance<End; }
    static CourseEventMarker Arrival(int index,float distance) {
        CourseEventMarker event;
        event.distance=distance;event.type="enemy_wave";
        event.id=index==0?"ridge_dive_left":"ridge_dive_right";
        event.editorGuid=index==0?"ridge-dive-left":"ridge-dive-right";
        event.payload="route:support";
        return event;
    }
    void Notify(std::span<const EnemyCombatEvent> events) {
        for(const auto& event:events)if(event.kind==EnemyCombatEventKind::Defeated)
            for(size_t i=0;i<ids_.size();++i)if(ids_[i] && event.actorId==ids_[i])defeated_[i]=true;
    }
    void Update(float dt,float distance,const CourseSpawnRuntime& runtime,
            const EnemyEncounterReadabilityFrame& readable,const RailWorldRaycastInput* visibility=nullptr,
            bool climbBattleResolved=true) {
        arrival_=-1;
        if(!Active(distance)) {
            hold_=false;primaryBusy_=false;
            if(distance>=End)phase_=Phase::Complete;
            return;
        }
        primaryBusy_=!climbBattleResolved;
        unsigned auxiliary=0;
        for(const auto& actor:runtime.Enemies()) {
            if(actor.desc.waveId=="ridge-dive-left")ids_[0]=actor.actorId;
            if(actor.desc.waveId=="ridge-dive-right")ids_[1]=actor.actorId;
            if(!Living(actor))continue;
            primaryBusy_|=!actor.desc.sourcePlacementGuid.empty() || actor.desc.waveId=="short-bridge-lookout";
            auxiliary+=actor.desc.sourcePlacementGuid.empty();
        }
        dt=std::isfinite(dt)?(std::clamp)(dt,0.0f,0.25f):0;
        if(!entered_ && dt>0) {
            entered_=true;
            if(distance>CrestEnd)crestSeconds_=1.2f; // late checkpoint, not a second reveal
        }
        if(distance>=CrestBegin)crestSeconds_+=dt;
        hold_=distance>=CrestBegin && (distance<CrestEnd || crestSeconds_<1.2f);
        if(dt==0)return;
        if(phase_==Phase::Climb && distance>=CrestBegin)phase_=Phase::Crest;
        if(phase_==Phase::Crest && distance>=LastEntry)phase_=Phase::Complete;
        if(phase_==Phase::Crest && !hold_ && distance>=DiveBegin && !primaryBusy_ &&
                readable.truth.safeToResolveSession && auxiliary==0) {
            phase_=Phase::Left;arrival_=0;stageSeconds_=seenSeconds_=0;
        } else if(phase_==Phase::Left || phase_==Phase::Right) {
            stageSeconds_+=dt;
            const size_t index=phase_==Phase::Left?0:1;
            if(IsVisible(ids_[index],readable,visibility))seenSeconds_+=dt;
            if(phase_==Phase::Left && stageSeconds_>=1.25f && (seenSeconds_>=.8f || defeated_[0]) &&
                    !primaryBusy_ && auxiliary<2 && distance<2780) {
                phase_=Phase::Right;arrival_=1;stageSeconds_=seenSeconds_=0;
            } else if(phase_==Phase::Right && (seenSeconds_>=1.0f || defeated_[1])) {
                phase_=Phase::Complete;
            }
        }
    }
    bool SuppressReinforcements(float distance) const {
        if(!Active(distance))return false;
        return primaryBusy_ || (distance>=CrestBegin && phase_!=Phase::Complete) || hold_;
    }
    void ApplyAttackGates(CourseSpawnRuntime& runtime,float distance) const {
        if(!Active(distance))return;
        for(const auto& actor:runtime.Enemies()) {
            const float at=actor.desc.spawnDistance+actor.desc.distanceOffset;
            const bool climbBattery=actor.desc.actorAssetId=="short_turret" && at>2100 && at<2350;
            if(Living(actor) && (hold_ || (primaryBusy_ && climbBattery)))
                runtime.SetEnemyEncounterPacing(actor.actorId,true,false);
        }
    }
    bool HoldAttacks() const { return hold_; }
    bool PrimaryBusy() const { return primaryBusy_; }
    int ArrivalThisFrame() const { return arrival_; }
    float CrestSeconds() const { return crestSeconds_; }
    Phase CurrentPhase() const { return phase_; }
    const char* Label(float distance) const {
        return distance<CrestBegin?"RIDGE CLIMB":distance<DiveBegin?"RIDGE CREST":"CANYON DIVE";
    }
private:
    static bool Living(const CourseEnemyActor& actor) {
        return !actor.stationaryDormant && actor.desc.hitPoints>0 && !actor.entranceExitState.exitComplete &&
            actor.combatState.phase!=EnemyCombatPhase::Dying && actor.combatState.phase!=EnemyCombatPhase::Retired;
    }
    static bool IsVisible(uint32_t id,const EnemyEncounterReadabilityFrame& frame,
            const RailWorldRaycastInput* visibility) {
        for(const auto& proxy:frame.actors)if(proxy.actorId==id && proxy.onScreen && proxy.screenReadable && proxy.targetable) {
            if(!visibility || !visibility->aim)return true;
            auto aim=*visibility->aim;
            const auto delta=RailMothership::Add(proxy.worldPosition,RailMothership::Scale(aim.worldRayOrigin,-1));
            const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            if(distance<=.001f)return false;
            aim.valid=true;aim.worldRayDirection=RailMothership::Scale(delta,1/distance);
            aim.maxDistance=distance+4;aim.aimDistance=distance;aim.worldAimPoint=proxy.worldPosition;
            auto query=*visibility;query.aim=&aim;
            const auto hit=RailWorldRaycast::Query(query);
            return hit.hit && hit.kind==RailAimHitKind::Enemy && hit.actorId==id;
        }
        return false;
    }
    std::array<uint32_t,2> ids_{};
    std::array<bool,2> defeated_{};
    Phase phase_=Phase::Climb;
    float crestSeconds_=0,stageSeconds_=0,seenSeconds_=0;
    int arrival_=-1;
    bool entered_=false,hold_=false,primaryBusy_=false;
};
