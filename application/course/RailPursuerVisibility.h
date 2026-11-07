#pragma once
#include "CourseSpawnRuntime.h"
#include "RailWorldRaycast.h"
#include "../terrain/RailPath.h"
#include <cmath>

// Reverse the actual shot segment so the firing actor can be the permitted
// endpoint. Obstacles, terrain and the other escort remain real occluders.
inline bool RailPursuerHasShotLine(const CourseEnemyActor& actor,Vector3 playerWorld,RailWorldRaycastInput context) {
    if(!context.railPath || actor.desc.hitPoints<=0 || !actor.weaponMount.ready)return false;
    for(int barrel=0;barrel<(actor.desc.actorAssetId=="rear_fang" && actor.fangCannonHitPoints>0?2:1);++barrel) {
        const auto target=ResolveEnemyProjectileWorldPosition(*context.railPath,actor.weaponMount.muzzleRail[barrel]);
        const Vector3 d{target.x-playerWorld.x,target.y-playerWorld.y,target.z-playerWorld.z};
        const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);if(length<.01f)return false;
        RailAimState ray;ray.valid=true;ray.worldRayOrigin=playerWorld;
        ray.worldRayDirection={d.x/length,d.y/length,d.z/length};ray.maxDistance=length;
        context.aim=&ray;context.includeVisualColumns=true;
        const auto hit=RailWorldRaycast::Query(context);
        if(hit.hit && !(hit.kind==RailAimHitKind::Enemy && hit.actorId==actor.actorId))return false;
    }
    return true;
}
