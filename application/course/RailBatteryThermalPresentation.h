#pragma once
#include "RailBatteryThermalTypes.h"
#include "RailAttackWarningPresentation.h"
#include "RailMinibossCombatDirector.h"

inline RailBatteryThermalFrame BuildRailBatteryThermalFrame(const CourseEnemyActor& actor,
        const CourseSpawnRuntime& runtime,const RailMinibossCombatDirector* director=nullptr,
        const RailAttackWarningPresentation* warnings=nullptr,uint32_t counterOwner=0,float counterRemaining=0) {
    RailBatteryThermalFrame frame;
    const int part=RailMothership::PartIndex(actor.desc.actorAssetId);if(part<0)return frame;
    frame.valid=true;frame.heavy=part==1;frame.clock=actor.age;
    if(actor.desc.hitPoints<=0 || actor.combatState.phase==EnemyCombatPhase::Dying || actor.combatState.phase==EnemyCombatPhase::Retired) {
        frame.phase=RailBatteryThermalPhase::Destroyed;return frame;
    }
    // Execution uses max(behavior cooldown, authored fire interval). Keep the
    // visual denominator faithful without changing the encounter's gate.
    const float duration=(std::max)(actor.desc.fireInterval,director?director->CooldownDuration(runtime,part):
        (std::max)(.05f,actor.behaviorDefinition.attackCooldownSeconds));
    const float cooldown=director?director->CooldownRemaining(runtime,part):
        (std::max)(0.0f,actor.behaviorState.attackCooldownRemaining);
    const float firedAgo=actor.turretMotion.lastFireAge>=0?actor.age-actor.turretMotion.lastFireAge:-1;
    const bool hasFired=actor.fireSequence>0 && firedAgo>=0;
    const float fireDuration=frame.heavy?.14f:RailGatling::BurstSeconds;
    bool emitting=hasFired && firedAgo<fireDuration;
    if(!frame.heavy) {
        emitting=false;
        for(const auto& bullet:runtime.Bullets())if(bullet.ownerActorId==actor.actorId && !bullet.hitConsumed &&
                (bullet.launchDelayRemaining>0 || (bullet.active && bullet.age<.075f))) {emitting=true;break;}
    }
    if(hasFired && cooldown>0) {
        const float fraction=(std::clamp)(cooldown/(std::max)(.05f,duration-fireDuration),0.0f,1.0f);
        frame.heat=fraction;frame.coolingProgress=1-fraction;
        if(frame.heavy) {
            const float open=(std::clamp)((firedAgo-.14f)/.22f,0.0f,1.0f);
            const float close=(std::clamp)(cooldown/(duration*.20f),0.0f,1.0f);
            frame.ventOpen=open*open*(3-2*open)*close*close*(3-2*close);
        }
    }
    frame.phase=hasFired && cooldown>0?RailBatteryThermalPhase::Cooling:RailBatteryThermalPhase::Standby;
    frame.remainingSeconds=cooldown;frame.durationSeconds=duration;
    if(emitting) {
        frame.phase=RailBatteryThermalPhase::Firing;
        frame.firingProgress=(std::clamp)(firedAgo/(std::max)(.01f,fireDuration),0.0f,1.0f);
        frame.heat=frame.heavy?1:.35f+.65f*frame.firingProgress;
        frame.remainingSeconds=(std::max)(0.0f,fireDuration-firedAgo);frame.durationSeconds=fireDuration;
        frame.ventOpen=0;
    }
    const bool admitted=actor.fireEnvironmentReady && actor.behaviorState.attackIntentActive && actor.attackState.tokenReserved;
    const RailWarningSource* source=nullptr;
    if(warnings)for(const auto& cue:warnings->Sources())if(cue.actorId==actor.actorId &&
        cue.intent==actor.behaviorState.attackIntentSequence && !cue.fired){source=&cue;break;}
    if(admitted && (warnings?source!=nullptr:actor.behaviorState.telegraphPresented)) {
        frame.remainingSeconds=(std::max)(0.0f,actor.behaviorState.attackTimeRemaining);
        frame.durationSeconds=(std::max)(.05f,actor.behaviorDefinition.attackLeadSeconds);
        frame.charge=source?source->charge:(std::clamp)(1-frame.remainingSeconds/frame.durationSeconds,0.0f,1.0f);
        const bool imminent=source?source->imminent:frame.remainingSeconds<=.24f;
        frame.phase=imminent?RailBatteryThermalPhase::Imminent:RailBatteryThermalPhase::Charging;
        frame.heat=0;frame.ventOpen=0;
    }
    if(director){counterOwner=director->CounterOwner();counterRemaining=director->CounterRemaining();}
    if(counterOwner==actor.actorId && counterRemaining>0) {
        frame.phase=RailBatteryThermalPhase::Counter;frame.charge=0;
        frame.remainingSeconds=counterRemaining;frame.durationSeconds=director?director->CounterDuration():counterRemaining;
    }
    return frame;
}

// Bounded world effects attached to the aimed head. All flow uses gameplay
// age or the real charge fraction; pause and checkpoint need no new clock.
inline std::vector<RailWarningSprite> BuildRailBatteryThermalSprites(const CourseEnemyActor& actor,
        const RailBatteryThermalFrame& frame,const RailPath& rail,Vector3 rotation,Vector3 right,Vector3 up) {
    std::vector<RailWarningSprite> sprites;sprites.reserve(32);
    if(!frame.valid || frame.phase==RailBatteryThermalPhase::Destroyed || frame.phase==RailBatteryThermalPhase::Inactive)return sprites;
    const auto sample=rail.Evaluate(actor.desc.spawnDistance+actor.desc.distanceOffset);
    const auto center=ResolveEnemyProjectileWorldPosition(sample,{actor.desc.lateralOffset,actor.desc.verticalOffset,
        actor.desc.spawnDistance+actor.desc.distanceOffset});
    const float size=actor.desc.radius;
    const auto world=MakeAffineMatrix({size,size,size},rotation,center);
    const auto point=[&](Vector3 p){return Vector3{p.x*world.m[0][0]+p.y*world.m[1][0]+p.z*world.m[2][0]+world.m[3][0],
        p.x*world.m[0][1]+p.y*world.m[1][1]+p.z*world.m[2][1]+world.m[3][1],p.x*world.m[0][2]+p.y*world.m[1][2]+p.z*world.m[2][2]+world.m[3][2]};};
    const bool charging=frame.phase==RailBatteryThermalPhase::Charging || frame.phase==RailBatteryThermalPhase::Imminent;
    const bool firing=frame.phase==RailBatteryThermalPhase::Firing,counter=frame.phase==RailBatteryThermalPhase::Counter;
    const auto& palette=RailEnemyEnergy::For(frame.heavy?RailEnemyEnergy::BatteryKind::Heavy:RailEnemyEnergy::BatteryKind::Gatling);
    const Vector4 ink=counter?Vector4{2.2f,1.3f,.25f,1}:palette.trail;
    const auto glow=[&](Vector3 at,float radius,float alpha,Vector4 color){
        if(alpha>.001f)sprites.push_back({point(at),{right.x*radius*size,right.y*radius*size,right.z*radius*size},
            {up.x*radius*size,up.y*radius*size,up.z*radius*size},RailEnemyEnergy::Alpha(color,alpha),16});};
    const auto streak=[&](Vector3 a,Vector3 b,float width,float alpha,Vector4 color){
        const auto from=point(a),to=point(b);const Vector3 half{(to.x-from.x)*.5f,(to.y-from.y)*.5f,(to.z-from.z)*.5f};
        const Vector3 middle{(to.x+from.x)*.5f,(to.y+from.y)*.5f,(to.z+from.z)*.5f};
        if(alpha>.001f)sprites.push_back({middle,half,{up.x*width*size,up.y*width*size,up.z*width*size},RailEnemyEnergy::Alpha(color,alpha),32});};
    for(float side:{-1.0f,1.0f}) {
        if(!frame.heavy)for(int cell=0;cell<3;++cell) {
            const float stage=(std::clamp)(frame.charge*3-cell,0.0f,1.0f);
            const float light=charging?.12f+.78f*stage:firing?.85f:frame.heat*.50f;
            const Vector3 position{side*.85f,.08f+float(cell)*.20f,-.27f};
            glow(position,.10f,light,ink);
        }
        if(frame.heavy && (charging || firing)) {
            const Vector3 port{side*.85f,.50f,-.86f},muzzle{0,.32f,-RailTurret::ForMesh(actor.desc.meshId).muzzleForward};
            const float t=frame.charge*frame.charge;
            const Vector3 flow{port.x+(muzzle.x-port.x)*t,port.y+(muzzle.y-port.y)*t,port.z+(muzzle.z-port.z)*t};
            glow(port,.18f,firing?.45f:.25f+.55f*frame.charge,palette.halo);
            if(charging){streak(port,flow,.055f,.25f+.35f*frame.charge,palette.trail);glow(flow,.09f,.60f,palette.hot);}
        }
        const bool venting=(frame.phase==RailBatteryThermalPhase::Cooling || counter) && frame.heat>0;
        if(venting) {
            const float gate=frame.heavy?frame.ventOpen:1;
            for(int slot=0;slot<3;++slot)glow({side*(frame.heavy?.86f:.85f),frame.heavy?.75f:.08f+slot*.20f,frame.heavy?.32f-slot*.19f:-.28f},
                frame.heavy?.13f:.09f,frame.heat*gate*.50f,ink);
            for(int wisp=0;wisp<3;++wisp) {
                const float age=frame.clock*.85f+float(wisp)/3;
                const float phase=age-std::floor(age);
                const float alpha=frame.heat*gate*std::sin(phase*3.14159265f)*(frame.heavy?.24f:.14f);
                const Vector3 at{side*(.86f+phase*.24f),.80f+phase*(frame.heavy?1.2f:.8f),.12f};
                const float r=(frame.heavy?.22f:.14f)+phase*.10f;
                if(alpha>.001f)sprites.push_back({point(at),{right.x*r*size,right.y*r*size,right.z*r*size},
                    {up.x*r*size*2,up.y*r*size*2,up.z*r*size*2},
                    {frame.heavy?.80f:1.0f,.72f,frame.heavy?.90f:.55f,alpha},52});
            }
        }
    }
    return sprites;
}
