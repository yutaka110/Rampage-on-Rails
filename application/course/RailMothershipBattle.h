#pragma once
#include "RailMothershipPresentation.h"
#include "CourseGameplayWaveRuntimeBridge.h"
#include <vector>

namespace RailMothership {
inline constexpr const char* BossActorId="short_mothership_core";
inline constexpr const char* BossWaveId="short-mothership";
// Render/socket contract: reactor sits outside the rear armour at local (0,-11,-70).
inline Frame AtReactor(const RailPath& rail,float distance,float lateral,float vertical) {
    const auto sample=rail.Evaluate(distance);
    Frame result;result.visible=true;result.railDistance=distance+70;
    result.right=sample.right;result.up=sample.up;result.forward=sample.tangent;
    const auto reactor=Add(sample.position,Add(Scale(sample.right,lateral),Scale(sample.up,vertical)));
    result.center=Add(reactor,Add(Scale(sample.up,11),Scale(sample.tangent,70)));
    result.railVertical=vertical+11;result.launchGlow=0.1f;
    return result;
}
class Battle final {
public:
    void Reset() { *this={}; }
    const Frame& Update(const Frame& escort,const CourseSpawnRuntime& runtime,
            CourseGameplayWavePhase phase,const RailPath& rail,float distance,float deltaTime) {
        explosions_.clear();frame_=escort;
        const float dt=std::isfinite(deltaTime)?(std::clamp)(deltaTime,0.0f,0.25f):0;
        const bool completed=phase==CourseGameplayWavePhase::Completed;
        std::array<const CourseEnemyActor*,2> parts{};
        const CourseEnemyActor* anchor=nullptr;
        for(const auto& actor:runtime.Enemies()) {
            const int index=PartIndex(actor.desc.actorAssetId);
            if(index<0) continue;
            parts[index]=&actor;
            if(actor.desc.hitPoints>0 && !anchor) anchor=&actor;
        }
        if(parts[0] || parts[1] || (hasBattle_ && lastBattle_.partsBattle)) {
            if(anchor && !completed) {
                const int index=PartIndex(anchor->desc.actorAssetId);
                frame_=AtReactor(rail,anchor->desc.spawnDistance+anchor->desc.distanceOffset,
                    anchor->desc.lateralOffset-PartLateral[index],anchor->desc.verticalOffset);
                frame_.partsBattle=frame_.battleActive=true;
                frame_.battleHealth=0;frame_.remainingParts=0;
                for(int i=0;i<2;++i) if(parts[i] && parts[i]->desc.hitPoints>0) {
                    const auto& part=*parts[i];
                    frame_.partHealth[i]=(std::clamp)(part.desc.hitPoints/
                        part.combatDefinition.maximumHitPoints,0.0f,1.0f);
                    frame_.partCharge[i]=part.behaviorState.attackIntentActive && part.attackState.tokenReserved
                        ? Smooth(1.0f-part.behaviorState.attackTimeRemaining/
                            (std::max)(0.05f,part.behaviorDefinition.attackLeadSeconds),0,1) : 0;
                    frame_.battleHealth+=frame_.partHealth[i]*0.5f;
                    ++frame_.remainingParts;
                }
                lastBattle_=frame_;hasBattle_=true;deathAge_=0;
            } else if(hasBattle_) {
                // Only the weapons were destroyed. The hull withdraws intact;
                // this encounter never claims to destroy the final boss.
                deathAge_+=dt;frame_=lastBattle_;
                frame_.battleActive=false;frame_.battleDefeated=frame_.battleRepelled=true;
                frame_.remainingParts=0;frame_.partHealth={};frame_.partCharge={};
                frame_.battleHealth=frame_.battleCharge=frame_.battleDestruction=0;
                frame_.center=Add(frame_.center,Add(Scale(frame_.forward,deathAge_*65),
                    Scale(frame_.up,deathAge_*deathAge_*7)));
                frame_.visible=deathAge_<3.0f;
            } else frame_.visible=false;
            return frame_;
        }
        const CourseEnemyActor* boss=nullptr;
        for(const auto& actor:runtime.Enemies()) if(actor.desc.actorAssetId==BossActorId) {boss=&actor;break;}
        if(boss && boss->desc.hitPoints>0) {
            deathAge_=0;explosionIndex_=0;
            frame_=AtReactor(rail,boss->desc.spawnDistance+boss->desc.distanceOffset,
                boss->desc.lateralOffset,boss->desc.verticalOffset);
            frame_.battleActive=true;
            frame_.battleHealth=(std::clamp)(boss->desc.hitPoints/boss->combatDefinition.maximumHitPoints,0.0f,1.0f);
            const bool charging=boss->behaviorState.attackIntentActive && boss->attackState.tokenReserved;
            frame_.battleCharge=charging ? Smooth(1.0f-boss->behaviorState.attackTimeRemaining/
                (std::max)(0.05f,boss->behaviorDefinition.attackLeadSeconds),0,1) : 0;
            lastBattle_=frame_;hasBattle_=true;
        } else if(completed || (boss && boss->desc.hitPoints<=0)) {
            // Completion comes from the authoritative Wave snapshot. A retry
            // cannot resurrect the carrier or replay its destruction sequence.
            if(hasBattle_) {
                deathAge_+=dt;frame_=lastBattle_;
                frame_.battleActive=false;frame_.battleDefeated=true;frame_.battleHealth=0;
                frame_.battleCharge=0;frame_.battleDestruction=(std::clamp)(deathAge_/2.4f,0.0f,1.0f);
                const float fall=deathAge_*deathAge_*6;
                frame_.center=Add(frame_.center,Add(Scale(frame_.forward,deathAge_*18),Scale(frame_.up,-fall)));
                const float bank=0.28f*frame_.battleDestruction;
                const auto oldRight=frame_.right,oldUp=frame_.up;
                frame_.right=Add(Scale(oldRight,std::cos(bank)),Scale(oldUp,-std::sin(bank)));
                frame_.up=Add(Scale(oldUp,std::cos(bank)),Scale(oldRight,std::sin(bank)));
                frame_.visible=deathAge_<2.4f;
                constexpr float times[]={0.0f,0.22f,0.52f,0.90f,1.35f};
                constexpr Vector3 points[]={{0,-11,-70},{-36,0,-35},{39,0,-25},{-18,8,5},{25,5,20}};
                while(explosionIndex_<5 && deathAge_>=times[explosionIndex_])
                    explosions_.push_back(frame_.World(points[explosionIndex_++]));
            } else frame_.visible=false;
        } else {
            hasBattle_=false;deathAge_=0;explosionIndex_=0;
            // Preserve the horizontal entrance, then clear the new high ridge
            // before descending toward the battle lane.
            if(distance>=1950) {
                const float altitude=rail.EvaluateExtended(distance+EscortForwardOffset).position.y+Height;
                frame_.center.y += (altitude-frame_.center.y)*Smooth(distance,1950,2250);
            }
            if(distance>=3100) {
                const auto approach=AtReactor(rail,distance+82,0,18);
                const float t=Smooth(distance,3100,3350);
                frame_.center=Mix(frame_.center,approach.center,t);
                const auto normalize=[](Vector3 v) { const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
                    return length>0.0001f ? Scale(v,1.0f/length) : Vector3{0,0,1}; };
                frame_.forward=normalize(Mix(escort.forward,approach.forward,t));
                frame_.right=normalize({frame_.forward.z,0,-frame_.forward.x});
                frame_.up={frame_.forward.y*frame_.right.z-frame_.forward.z*frame_.right.y,
                    frame_.forward.z*frame_.right.x-frame_.forward.x*frame_.right.z,
                    frame_.forward.x*frame_.right.y-frame_.forward.y*frame_.right.x};
                frame_.launchGlow=escort.launchGlow*(1-t);
            }
        }
        return frame_;
    }
    const Frame& Current() const { return frame_; }
    const std::vector<Vector3>& Explosions() const { return explosions_; }
private:
    Frame frame_{},lastBattle_{};
    std::vector<Vector3> explosions_;
    bool hasBattle_=false;
    float deathAge_=0;
    unsigned explosionIndex_=0;
};
}
