#pragma once
#include "RailMinibossCombatDirector.h"

enum class RailGatlingAimPhase { None, Guard, Counter };
struct RailGatlingAimCue {
    RailGatlingAimPhase phase=RailGatlingAimPhase::None;
    Vector3 world{};
    uint32_t owner=0;
    float progress=0;
};

// Presentation reads actual interception and counter state; it never moves
// the reticle, removes rounds or grants a counter window.
inline RailGatlingAimCue BuildRailGatlingAimCue(const RailMinibossCombatDirector& cycle,
    const CourseSpawnRuntime& runtime,const RailPath& rail,float playerDistance) {
    RailGatlingAimCue cue;
    Vector3 guard;
    if(cycle.GatlingGuardPoint(playerDistance,guard)) {
        if(std::none_of(runtime.Enemies().begin(),runtime.Enemies().end(),[&](const auto& actor){
            return actor.actorId==cycle.VolleyOwner() && actor.desc.hitPoints>0;}))return cue;
        cue.phase=RailGatlingAimPhase::Guard;cue.owner=cycle.VolleyOwner();
        cue.world=ResolveEnemyProjectileWorldPosition(rail,guard);
        cue.progress=float(cycle.InterceptedCount())/RailGatling::Rounds;
    } else if(cycle.CounterRemaining()>0) {
        for(const auto& actor:runtime.Enemies())if(actor.actorId==cycle.CounterOwner() && actor.desc.hitPoints>0) {
            cue.phase=RailGatlingAimPhase::Counter;cue.owner=actor.actorId;
            cue.world=ResolveEnemyProjectileWorldPosition(rail,{actor.desc.lateralOffset,
                actor.desc.verticalOffset,actor.desc.spawnDistance+actor.desc.distanceOffset});
            cue.progress=cycle.CounterRemaining()/cycle.CounterDuration();break;
        }
    }
    return cue;
}
