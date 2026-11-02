#pragma once
#include "CourseSpawnRuntime.h"
#include "RailLockOnTypes.h"

inline Vector3 ResolveEnemyShieldLockEndpoint(RailLockTargetHandle target,Vector3 muzzle,
    Vector3 bodyPoint,const CourseSpawnRuntime& runtime) {
    if(target.kind!=RailLockTargetKind::Enemy)return bodyPoint;
    for(const auto& actor:runtime.Enemies())if(actor.actorId==target.actorId &&
        actor.energyShield.Active(actor.desc.energyShield) && actor.weaponMount.ready) {
        const auto delta=EnemyEnergyShield::Sub(bodyPoint,muzzle);
        const auto hit=EnemyEnergyShield::Intersect(actor.weaponMount.dronePose,muzzle,delta,
            std::sqrt(EnemyEnergyShield::Dot(delta,delta)));
        if(hit.hit)return hit.position;
    }
    return bodyPoint;
}
