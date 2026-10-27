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
RailLockImpactFrame RailLockContact::Presentation() const {
    RailLockImpactFrame frame;
    if(!std::isfinite(age) || !std::isfinite(size) || age<0 || age>=Duration() || size<=0) return frame;
    const auto smooth=[](float t,float a,float b) {
        const float u=(std::clamp)((t-a)/(b-a),0.0f,1.0f);return u*u*(3-2*u);
    };
    const float scale=size*(destroyed?1.35f:0.85f);
    frame.burstPhase=(std::min)(1.0f,age/(destroyed?0.18f:0.13f));
    frame.burstRadius=scale*(0.46f+0.54f*(1-std::exp(-age*72)));
    frame.burstOpacity=1-smooth(frame.burstPhase,0.20f,1.0f);
    frame.haloOpacity=1-smooth(age,0.0f,0.065f);
    frame.shockExtent=scale*1.65f;
    if(age>=0.018f) {
        frame.shockPhase=(std::min)(1.0f,(age-0.018f)/(destroyed?0.22f:0.15f));
        frame.shockOpacity=smooth(frame.shockPhase,0,0.12f)*(1-smooth(frame.shockPhase,0.30f,1));
    }
    frame.fragments=destroyed?12u:7u;
    // Every confirmed lock arrival gets a weapon-sized penetrating flash.
    // A kill holds its peak longer and adds fracture/metal aftermath below.
    frame.pierceExtent=size*(destroyed?2.65f:1.85f)*(0.82f+0.18f*smooth(age,0,0.018f));
    frame.pierceOpacity=(1-smooth(age,destroyed?0.040f:0.018f,destroyed?0.145f:0.090f))*(destroyed?1.0f:0.72f);
    frame.jetOpacity=smooth(age,0.008f,0.022f)*(1-smooth(age,destroyed?0.065f:0.035f,destroyed?0.16f:0.095f));
    if(destroyed) {
        // Arrival light yields to the orange internal rupture; the remaining
        // contact lifetime is reserved for emitted metal, not a long flash.
        frame.burstPhase=(std::min)(1.0f,age/0.14f);
        frame.burstRadius=size*(0.36f+0.72f*(1-std::exp(-age*85)));
        frame.burstOpacity=1-smooth(age,0.040f,0.16f);
        frame.haloOpacity=1-smooth(age,0.012f,0.065f);
        frame.shockExtent=size*2.60f;
        frame.shockPhase=0.08f+0.92f*(std::clamp)(age/0.24f,0.0f,1.0f);
        frame.shockOpacity=(0.65f+0.35f*smooth(age,0,0.018f))*(1-smooth(age,0.085f,0.24f));
        frame.rupturePhase=(std::clamp)((age-0.055f)/0.18f,0.0f,1.0f);
        frame.ruptureRadius=size*(0.30f+0.82f*(1-std::exp(-frame.rupturePhase*4)));
        frame.ruptureOpacity=smooth(age,0.055f,0.085f)*(1-smooth(age,0.11f,0.235f));
    }
    return frame;
}
Vector3 RailLockContact::CenterAt(float time) const {
    if(destroyed && hasWreckPose) {
        const auto body=EvaluateTwinShieldDroneDestructionBody(wreckPose,time);
        return {body.m[3][0]-1.02f*body.m[2][0],body.m[3][1]-1.02f*body.m[2][1],body.m[3][2]-1.02f*body.m[2][2]};
    }
    return Add(position,Add(Scale(drift,time),Scale(normal,size*0.24f)));
}
RailLockDebrisFrame RailLockContact::Debris() const {
    RailLockDebrisFrame frame;
    if(!destroyed || !std::isfinite(age) || !std::isfinite(size) || size<=0 || age<0 || age>=Duration()) return frame;
    const auto anchor=hasWreckPose?BuildTwinShieldDroneWorldMatrix(wreckPose):MakeIdentity4x4();
    const auto right=Normalize(Vector3{anchor.m[0][0],anchor.m[0][1],anchor.m[0][2]});
    const auto up=Normalize(Vector3{anchor.m[1][0],anchor.m[1][1],anchor.m[1][2]});
    const float side=hasWreckPose?TwinShieldDestructionSide(wreckPose):(shotId%2?1.0f:-1.0f);
    const float detach=hasWreckPose?TwinShieldDroneDetachAge(wreckPose,side):0.24f;
    for(uint32_t i=0;i<frame.fragments.size();++i) {
        const bool armor=i>=4;
        const float born=armor?detach+0.003f*float(i-4):0.055f+0.012f*float(i);
        const float t=age-born,life=armor?0.42f+0.020f*float(i%4):0.25f+0.015f*float(i);
        if(t<0 || t>=life) continue;
        const float seed=float((shotId+i*17)%97)*0.731f;
        const float wing=armor?((i-4)%4==3?-side:side):(i%2?-1.0f:1.0f);
        Vector3 origin=CenterAt(born);
        Vector3 inherited=drift;
        if(armor && hasWreckPose) {
            auto release=wreckPose;release.destructionAge=born;
            const auto breakup=EvaluateTwinShieldDroneBreakup(release);
            const auto& shield=wing<0?breakup.leftWorld:breakup.rightWorld;
            origin={shield.m[3][0]-wing*0.06f*shield.m[0][0]-0.96f*shield.m[2][0],
                shield.m[3][1]-wing*0.06f*shield.m[0][1]-0.96f*shield.m[2][1],
                shield.m[3][2]-wing*0.06f*shield.m[0][2]-0.96f*shield.m[2][2]};
            inherited=EvaluateTwinShieldDroneDestructionFlight(wreckPose,born).velocity;
        }
        const auto velocity=Add(Scale(inherited,0.65f),Scale(Add(Scale(right,wing*(3.8f+1.3f*std::sin(seed))),
            Add(Scale(up,0.8f+2.2f*std::sin(seed*1.7f)),Scale(normal,1.5f+0.7f*std::cos(seed)))),size));
        const auto point=[&](float time) {
            return Add(origin,Add(Scale(velocity,(1-std::exp(-time*2.8f))/2.8f),{0,-4.9f*time*time,0}));
        };
        const float phase=t/life;
        auto& chip=frame.fragments[frame.count++];
        chip.position=point(t);chip.tail=point((std::max)(0.0f,t-(armor?0.042f:0.024f)));
        chip.radius=size*(armor?0.055f+0.012f*float(i%3):0.028f);
        chip.rotation=seed+t*(i%2?9.0f:-11.0f);
        chip.opacity=1-TwinShieldDestructionSmooth(phase,0.45f,1);
        chip.heat=1-TwinShieldDestructionSmooth(t,0.050f,armor?0.37f:0.19f);
    }
    return frame;
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
void RailLockVolley::Reset(){lasers_.clear();contacts_.clear();batches_.clear();hits_.clear();completed_.clear();nextId_=1;}
void RailLockVolley::UpdateDefeatPresentation(float dt) {
    hits_.clear();completed_.clear();batches_.clear();
    if(!std::isfinite(dt) || dt<=0) return;
    for(auto& contact:contacts_) contact.age+=dt;
    std::erase_if(contacts_,[](const auto& c){return c.age>=c.Duration();});
    for(auto& laser:lasers_) {
        laser.age+=dt;
        if(laser.resolved) continue;
        // Already committed shots finish their visible journey without damage.
        if(laser.age<laser.delay) continue;
        laser.launched=true;laser.position=laser.Curve(laser.Progress());
        if(laser.Progress()>=1) {laser.resolved=true;laser.endedAge=laser.age;}
    }
    std::erase_if(lasers_,[](const auto& s){return s.resolved && s.age-s.endedAge>=0.14f;});
}
bool RailLockVolley::Visible() const {
    return !contacts_.empty() || std::any_of(lasers_.begin(),lasers_.end(),[](const auto& s){return s.Opacity()>0.001f;});
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
    for(auto& contact:contacts_) contact.age+=dt;
    std::erase_if(contacts_,[](const auto& c){return c.age>=c.Duration();});
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
                request.lockOnContact=true;
                RailLockContact contact;contact.shotId=request.shotId;contact.actorId=request.targetActorId;
                contact.position=request.hitPoint;contact.normal=request.hitNormal;
                // Capture before lethal damage clears the live weapon mount.
                for(const auto& actor:runtime.Enemies()) if(actor.actorId==contact.actorId &&
                        actor.desc.meshId=="twin_shield_hull" && actor.weaponMount.ready) {
                    const auto& pose=actor.weaponMount.dronePose;
                    contact.size=(std::clamp)((pose.scale.x+pose.scale.y+pose.scale.z)/3,0.5f,4.0f);
                    if(pose.hasFlightVelocity) contact.drift=pose.flightVelocity;
                    break;
                }
                const auto damage=collision.ApplyWeaponHit(runtime,course,request);
                if(damage.destroyed && damage.hitKind==RailAimHitKind::Enemy)
                    for(auto& batch:batches_) if(batch.id==laser.volley) {++batch.defeats;break;}
                laser.hit=damage.damageApplied;
                if(laser.hit) {
                    hits_.push_back(damage);
                    contact.destroyed=damage.destroyed;
                    if(contact.destroyed) for(const auto& actor:runtime.Enemies()) {
                        if(actor.actorId!=contact.actorId || !actor.hasDefeatedPose) continue;
                        contact.wreckPose=actor.defeatedPose;
                        // Match the presentation bridge's frozen flight drift.
                        contact.wreckPose.destructionAge=0;contact.wreckPose.destructionSeed=actor.actorId;
                        contact.wreckPose.destructionDrift=contact.wreckPose.hasFlightVelocity?contact.wreckPose.flightVelocity:
                            Scale(rail.Evaluate(actor.desc.spawnDistance+actor.desc.distanceOffset).tangent,
                                (std::clamp)(actor.behaviorState.engagementBandVelocity,-30.0f,45.0f));
                        contact.hasWreckPose=contact.wreckPose.visible;
                        break;
                    }
                    if(contacts_.size()>=ContactCapacity) contacts_.erase(contacts_.begin());
                    contacts_.push_back(contact);
                }
            }
        }
        for(auto& batch:batches_) if(batch.id==laser.volley) {
            if(batch.pending) --batch.pending;
            if(laser.hit) ++batch.hits;
            break;
        }
    }
    for(const auto& batch:batches_) if(batch.pending==0) completed_.push_back({batch.tokens,batch.hits,batch.maxLocks,batch.defeats});
    std::erase_if(batches_,[](const auto& b){return b.pending==0;});
    std::erase_if(lasers_,[](const auto& s){return s.resolved && s.age-s.endedAge>=0.14f;});
}
