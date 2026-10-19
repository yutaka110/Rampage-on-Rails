#include "RailLockVolley.h"
#include "CourseSpawnRuntime.h"
#include "CourseCollisionSystem.h"
#include "../terrain/RailPath.h"
#include <algorithm>
#include <cmath>

namespace {
Vector3 Add(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 Scale(Vector3 v,float s){return {v.x*s,v.y*s,v.z*s};}
float Length(Vector3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
Vector3 Point(const RailPath& rail,float d,float x,float y){
    const auto s=rail.Evaluate(d); return Add(s.position,Add(Scale(s.right,x),Scale(s.up,y)));
}
}
float RailLockLaser::Progress() const {
    const float clock=resolved?endedAge:age;
    return (std::clamp)((clock-delay)/(std::max)(0.01f,duration),0.0f,1.0f);
}
float RailLockLaser::Opacity() const {
    if(!launched) return 0;
    return resolved ? (std::max)(0.0f,1.0f-(age-endedAge)/0.14f) : 1.0f;
}
Vector3 RailLockLaser::Curve(float progress) const {
    const float t=(std::clamp)(progress,0.0f,1.0f);
    const float u=t*(0.65f+0.35f*t),v=1-u;
    const auto delta=Add(end,Scale(start,-1));
    const float length=Length(delta);
    const auto direction=length>0.001f?Scale(delta,1/length):Vector3{0,0,1};
    const auto right=direction.x*direction.x+direction.z*direction.z>0.00001f ?
        Normalize(Vector3{direction.z,0,-direction.x}):Vector3{1,0,0};
    const float side=(index&1u)?-1.0f:1.0f;
    const float spread=(std::min)(length*0.12f,1.1f+0.4f*float(index%4));
    const Vector3 fan=Add(Scale(right,side*spread),Vector3{0,0.8f+0.3f*float(index%3),0});
    const auto a=Add(start,Add(Scale(delta,0.22f),fan));
    const auto b=Add(start,Add(Scale(delta,0.72f),Scale(fan,0.55f)));
    return Add(Add(Scale(start,v*v*v),Scale(a,3*v*v*u)),Add(Scale(b,3*v*u*u),Scale(end,u*u*u)));
}
void RailLockVolley::Reset(){lasers_.clear();batches_.clear();hits_.clear();completed_.clear();nextId_=1;}
bool RailLockVolley::Visible() const {
    return std::any_of(lasers_.begin(),lasers_.end(),[](const auto& s){return s.Opacity()>0.001f;});
}
bool RailLockVolley::TargetPosition(RailLockTargetHandle target,const RailPath& rail,
        const CourseSpawnRuntime& runtime,Vector3& position) {
    if(target.kind==RailLockTargetKind::Enemy) for(const auto& e:runtime.Enemies()) {
        if(e.actorId!=target.actorId) continue;
        if(e.desc.hitPoints<=0 || (e.combatState.initialized && !e.combatState.canReceiveDamage)) return false;
        if(e.desc.meshId=="twin_shield_hull" && e.weaponMount.ready) {
            const auto m=BuildTwinShieldDroneWorldMatrix(e.weaponMount.dronePose);
            position={m.m[3][0]-0.77f*m.m[2][0],m.m[3][1]-0.77f*m.m[2][1],m.m[3][2]-0.77f*m.m[2][2]};
        } else position=Point(rail,e.desc.spawnDistance+e.desc.distanceOffset,e.desc.lateralOffset,e.desc.verticalOffset);
        return true;
    }
    if(target.kind==RailLockTargetKind::Obstacle) for(const auto& o:runtime.Obstacles()) {
        if(o.actorId!=target.actorId) continue;
        if(!o.desc.breakable || o.desc.hitPoints<=0) return false;
        position=Point(rail,o.desc.spawnDistance+o.desc.distanceOffset,o.desc.lateralOffset,o.desc.verticalOffset);return true;
    }
    return false;
}
size_t RailLockVolley::Enqueue(std::span<const RailLockToken> tokens,std::span<const WeaponShot> weapons,
        Vector3 muzzle,const RailPath& rail,const CourseSpawnRuntime& runtime,const RailLockSettings& settings) {
    if(tokens.empty() || weapons.empty() || rail.Length()<=0 || !settings.Validate() ||
        !std::isfinite(muzzle.x) || !std::isfinite(muzzle.y) || !std::isfinite(muzzle.z)) return 0;
    Batch batch; batch.id=nextId_++;batch.tokens=static_cast<uint32_t>(tokens.size());batch.maxLocks=settings.maxLocks;
    const size_t count=(std::min)(tokens.size(),weapons.size());
    const size_t limit=(std::min)(Capacity,static_cast<size_t>(settings.lockVfxMaxConcurrentShots));
    for(size_t i=0;i<count && lasers_.size()<limit;++i) {
        if(std::any_of(lasers_.begin(),lasers_.end(),[&](const auto& s){return s.weapon.shotId==weapons[i].shotId;})) continue;
        if(!std::isfinite(weapons[i].range) || !std::isfinite(weapons[i].damage) || weapons[i].damage<0) continue;
        Vector3 end;
        if(!TargetPosition(tokens[i].target,rail,runtime,end)) continue;
        const float distance=Length(Add(end,Scale(muzzle,-1)));
        if(!std::isfinite(distance) || distance>weapons[i].range || weapons[i].shotId==0) continue;
        RailLockLaser laser;laser.target=tokens[i].target;laser.weapon=weapons[i];
        laser.start=laser.position=muzzle;laser.end=end;laser.index=static_cast<uint32_t>(i);laser.volley=batch.id;
        laser.delay=(std::max)(0.0f,settings.lockVfxReleaseShotInterval)*float(i);
        laser.duration=(std::clamp)(distance/(std::max)(1.0f,settings.lockVfxTravelDistanceDivisor),
            settings.lockVfxTravelDurationMin,settings.lockVfxTravelDurationMax);
        lasers_.push_back(laser);++batch.pending;
    }
    if(batch.pending) batches_.push_back(batch);
    return batch.pending;
}
void RailLockVolley::Update(float dt,Vector3 muzzle,const RailPath& rail,CourseSpawnRuntime& runtime,
        CourseCollisionSystem& collision,const CourseAsset* course) {
    hits_.clear();completed_.clear();
    if(!std::isfinite(dt) || dt<=0) return;
    for(auto& laser:lasers_) {
        laser.age+=dt;
        if(laser.resolved) continue;
        Vector3 target;
        const bool valid=TargetPosition(laser.target,rail,runtime,target);
        if(valid) laser.end=target;
        if(laser.age<laser.delay && valid) continue;
        if(!laser.launched && valid) {laser.start=laser.position=muzzle;laser.launched=true;}
        const auto previous=laser.position;
        if(laser.launched) laser.position=laser.Curve(laser.Progress());
        if(valid && laser.Progress()<1) continue;
        laser.resolved=true;laser.endedAge=laser.age;
        if(valid) {
            auto origin=previous;
            auto delta=Add(laser.end,Scale(origin,-1));
            float distance=Length(delta);
            if(distance<0.001f){origin=laser.start;delta=Add(laser.end,Scale(origin,-1));distance=Length(delta);}
            if(distance>0.001f) {
                WeaponHitRequest request;request.shotId=laser.weapon.shotId;request.targetActorId=laser.target.actorId;
                request.hitKind=laser.target.kind==RailLockTargetKind::Enemy?RailAimHitKind::Enemy:RailAimHitKind::Obstacle;
                request.damageType=laser.weapon.damageType;request.rayOrigin=origin;
                request.rayDirection=Scale(delta,1/distance);request.hitPoint=laser.end;
                request.hitNormal=Scale(request.rayDirection,-1);request.hitDistance=distance;request.baseDamage=laser.weapon.damage;
                const auto damage=collision.ApplyWeaponHit(runtime,course,request);
                laser.hit=damage.damageApplied;
                if(laser.hit) hits_.push_back(damage);
            }
        }
        for(auto& batch:batches_) if(batch.id==laser.volley) {
            if(batch.pending) --batch.pending;
            if(laser.hit) ++batch.hits;
            break;
        }
    }
    for(const auto& batch:batches_) if(batch.pending==0) completed_.push_back({batch.tokens,batch.hits,batch.maxLocks});
    std::erase_if(batches_,[](const auto& b){return b.pending==0;});
    std::erase_if(lasers_,[](const auto& s){return s.resolved && s.age-s.endedAge>=0.14f;});
}
