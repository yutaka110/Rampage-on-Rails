#pragma once

#include "EnemyAttackLaneTelegraphRenderer.h"
#include "RailEnemyEnergyPalette.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

enum class RailWarningWeapon : unsigned char { Ordinary, Rapid, Heavy };
inline RailWarningWeapon RailWarningWeaponFor(const CourseEnemyActor& actor) {
    if(actor.desc.actorAssetId=="short_mothership_rapid") return RailWarningWeapon::Rapid;
    if(actor.desc.actorAssetId=="short_mothership_heavy") return RailWarningWeapon::Heavy;
    return RailWarningWeapon::Ordinary;
}

struct RailWarningSource {
    uint32_t actorId=0;
    uint64_t intent=0;
    RailWarningWeapon weapon=RailWarningWeapon::Ordinary;
    std::array<Vector3,2> muzzles{};
    size_t muzzleCount=0;
    float charge=0,radius=0.6f,flash=0;
    float visibleAge=0,onset=0;
    bool primary=false,fired=false,imminent=false;
};
struct RailWarningSprite {
    Vector3 center{},horizontal{},vertical{};
    Vector4 color{};
    float atlas=16;
};
struct RailIncomingWarning {
    uint64_t projectileId=0;
    Vector3 world{};
    float timeToApproach=0,missDistance=0,radius=0;
    bool active=false,critical=false,shootable=false;
};

// Sizes are in reference pixels at 720p, independent of enemy distance.
struct RailOrdinaryWarningStyle {
    float rayRadius=0,rayLength=0,coreRadius=0,opacity=0;
};
struct RailBatteryWarningStyle {
    float extent=0,coreRadius=0,stroke=0,opacity=0;
    int litStages=0;
};
struct RailBatteryWarningStroke {
    Vector2 start{},end{};
    float width=0;
    Vector4 ink{};
};

// Presentation only. Every charging source must belong to a lane admitted by
// the authoritative warning system. No timers, aim solutions or damage change.
class RailAttackWarningPresentation final {
public:
    static constexpr size_t MaxSources=3;
    void Reset(){sources_.clear();flashes_.clear();primary_=0;clock_=0;}
    void CancelActor(uint32_t id){std::erase_if(sources_,[&](const auto& s){return s.actorId==id;});flashes_.erase(id);}
    const auto& Sources() const{return sources_;}
    bool Presented(uint32_t id,uint64_t intent)const {
        return std::any_of(sources_.begin(),sources_.end(),[&](const auto& s){return s.actorId==id && s.intent==intent && !s.fired;});
    }
    void Update(const EnemyAttackTelegraphFrame& warnings,const EnemyAttackLaneTelegraphRenderFrame& lanes,
            const CourseSpawnRuntime& runtime,const RailPath& rail,float time) {
        if(!std::isfinite(time))time=clock_;
        if(time<clock_)Reset();
        const float dt=(std::max)(0.0f,time-clock_);
        std::array<RailWarningSource,MaxSources> previous{};
        const size_t previousCount=(std::min)(sources_.size(),MaxSources);
        std::copy_n(sources_.begin(),previousCount,previous.begin());
        clock_=time;sources_.clear();
        for(const auto& cue:warnings.cues) {
            if(sources_.size()>=MaxSources || !cue.onScreen || cue.occluded)continue;
            const auto actor=std::find_if(runtime.Enemies().begin(),runtime.Enemies().end(),[&](const auto& a){return a.actorId==cue.actorId;});
            if(actor==runtime.Enemies().end() || actor->desc.hitPoints<=0)continue;
            const bool fired=cue.phase==EnemyAttackTelegraphPhase::Fired;
            const bool admitted=std::any_of(lanes.lanes.begin(),lanes.lanes.end(),[&](const auto& l){return l.actorId==cue.actorId && l.attackIntentSequence==cue.attackIntentSequence && l.attackTokenId==cue.attackTokenId;});
            if(!fired && !admitted)continue;
            RailWarningSource source;source.actorId=cue.actorId;source.intent=cue.attackIntentSequence;
            source.weapon=RailWarningWeaponFor(*actor);source.fired=fired;
            source.imminent=cue.phase==EnemyAttackTelegraphPhase::Imminent;
            source.charge=(std::clamp)(cue.urgency,0.0f,1.0f);
            if(!fired) {
                const auto prior=std::find_if(previous.begin(),previous.begin()+previousCount,[&](const auto& p){
                    return p.actorId==source.actorId && p.intent==source.intent && !p.fired;});
                source.visibleAge=prior==previous.begin()+previousCount?0:prior->visibleAge+dt;
                // A single 240ms acquisition swell, tied to this attack intent.
                // It remains frozen during pause and never becomes a strobe.
                source.onset=(std::clamp)(1-source.visibleAge/.24f,0.0f,1.0f);
            }
            source.radius=source.weapon==RailWarningWeapon::Heavy?1.15f:source.weapon==RailWarningWeapon::Rapid?0.72f:0.55f;
            if(source.weapon!=RailWarningWeapon::Ordinary) {
                // Turret meshes do not populate the drone weaponMount cache.
                // Use the execution resolver: rapid lights two launch barrels,
                // while the single heavy round lights its actual barrel only.
                const int barrels=(std::clamp)(actor->desc.bulletCount,1,2);
                for(int barrel=0;barrel<barrels;++barrel)
                    source.muzzles[source.muzzleCount++]=ResolveEnemyProjectileWorldPosition(rail,
                        ResolveEnemyProjectileMuzzleRailPosition(*actor,barrel));
            } else if(actor->weaponMount.ready) {
                for(const auto& muzzle:actor->weaponMount.muzzleRail) {
                    const auto world=ResolveEnemyProjectileWorldPosition(rail,muzzle);
                    if(source.muzzleCount && Distance(world,source.muzzles[0])<0.1f)continue;
                    source.muzzles[source.muzzleCount++]=world;
                }
            } else {source.muzzles[0]=cue.worldPosition;source.muzzleCount=1;}
            if(fired) {
                auto& flash=flashes_[cue.actorId];
                if(flash.sequence!=cue.fireSequence){flash.sequence=cue.fireSequence;flash.started=time;flash.muzzles=source.muzzles;flash.count=source.muzzleCount;}
                const float age=(std::max)(0.0f,time-flash.started);
                source.flash=(std::max)(0.0f,1-age/0.12f);
                source.charge=0; // Keep the flash attached to the recoiling live barrel.
                if(source.flash<=0)continue;
            }
            sources_.push_back(source);
        }
        const auto retained=std::find_if(sources_.begin(),sources_.end(),[&](const auto& s){return s.actorId==primary_ && !s.fired;});
        if(retained==sources_.end()) {
            const auto next=std::find_if(sources_.begin(),sources_.end(),[](const auto& s){return !s.fired;});
            primary_=next==sources_.end()?0:next->actorId;
        }
        for(auto& source:sources_)source.primary=source.actorId==primary_;
        std::erase_if(flashes_,[&](const auto& entry){return time-entry.second.started>1;});
    }

    static RailOrdinaryWarningStyle OrdinaryStyle(const RailWarningSource& source) {
        const float c=(std::clamp)(source.charge,0.0f,1.0f);
        const float smooth=c*c*(3-2*c);
        // One inward motion follows the authoritative countdown. The compact
        // diamond is reserved for Imminent, so a looping sparkle cannot imply
        // that an enemy has fired or restarted its preparation.
        return {32-19*smooth+18*source.onset,source.imminent?9.0f:14.0f+6*source.onset,
            source.imminent?7.0f:4.0f,source.primary?1.0f:.85f};
    }

    static float OrdinaryWorldRadius(float physicalRadius,float worldPerPixel) {
        return worldPerPixel>0?(std::clamp)(physicalRadius,worldPerPixel*10,worldPerPixel*17):physicalRadius;
    }

    static RailBatteryWarningStyle BatteryStyle(const RailWarningSource& source) {
        const float c=(std::clamp)(source.charge,0.0f,1.0f),smooth=c*c*(3-2*c);
        const float opacity=source.primary?1.0f:.85f;
        if(source.weapon==RailWarningWeapon::Heavy)
            return {38-20*smooth+16*source.onset,source.imminent?11.0f:6.0f+3*c,source.imminent?6.0f:4.8f,opacity,0};
        // Three charging stages, not a projectile count. The real rapid
        // battery fires a pair; its paired bars remain on both sides.
        return {26-11*smooth+14*source.onset,source.imminent?5.5f:4.0f,source.imminent?4.3f:3.3f,opacity,
            source.imminent?3:(std::min)(3,1+int(c*3))};
    }

    static float BatteryWorldRadius(RailWarningWeapon weapon,float physicalRadius,float worldPerPixel) {
        if(worldPerPixel<=0)return physicalRadius;
        const float minimum=weapon==RailWarningWeapon::Heavy?17.0f:13.0f;
        const float maximum=weapon==RailWarningWeapon::Heavy?25.0f:20.0f;
        return (std::clamp)(physicalRadius,worldPerPixel*minimum,worldPerPixel*maximum);
    }

    // Local 720p strokes share a single contrast-backed HUD path. Their shape,
    // rather than hue or periodic flashing, identifies each battery.
    static std::vector<RailBatteryWarningStroke> BatteryStrokes(const RailWarningSource& source) {
        std::vector<RailBatteryWarningStroke> strokes;strokes.reserve(14);
        if(source.fired || source.weapon==RailWarningWeapon::Ordinary)return strokes;
        const auto& palette=RailEnemyEnergy::For(source.weapon==RailWarningWeapon::Heavy?RailEnemyEnergy::BatteryKind::Heavy:RailEnemyEnergy::BatteryKind::Gatling);
        const auto style=BatteryStyle(source);
        const auto line=[&](Vector2 a,Vector2 b,float width,Vector4 ink){strokes.push_back({a,b,width,ink});};
        if(source.weapon==RailWarningWeapon::Rapid) {
            for(int stage=0;stage<3;++stage) {
                const float y=float(stage-1)*11;
                const bool lit=stage<style.litStages;
                const Vector4 ink=lit?palette.ink:RailEnemyEnergy::Alpha(palette.ink,.30f);
                for(float side:{-1.0f,1.0f})
                    line({side*(style.extent+10),y-3},{side*style.extent,y+3},style.stroke,ink);
            }
            if(source.imminent)for(float side:{-1.0f,1.0f})
                line({side*4,-8},{side*4,8},3.0f,palette.hotInk);
        } else {
            constexpr float halfPi=1.570796327f;
            for(int wedge=0;wedge<4;++wedge) {
                const float angle=halfPi*wedge,dx=std::cos(angle),dy=std::sin(angle);
                const Vector2 tip{dx*style.extent,dy*style.extent};
                for(float side:{-1.0f,1.0f})
                    line({tip.x+dx*11-dy*side*7,tip.y+dy*11+dx*side*7},tip,style.stroke,palette.ink);
            }
            // A single large hexagonal core denotes the heavy shot. It becomes
            // its hottest tint only in the authoritative Imminent phase.
            for(int edge=0;edge<6;++edge) {
                const float a=1.047197551f*edge,b=1.047197551f*(edge+1);
                line({std::cos(a)*style.coreRadius,std::sin(a)*style.coreRadius},
                    {std::cos(b)*style.coreRadius,std::sin(b)*style.coreRadius},source.imminent?3.8f:2.6f,
                    RailEnemyEnergy::Alpha(palette.hotInk,source.imminent?1.0f:.55f));
            }
        }
        return strokes;
    }

    std::vector<RailWarningSprite> Sprites(Vector3 right,Vector3 up,Vector3 eye={},float pixelWorldScale=0)const {
        std::vector<RailWarningSprite> sprites;sprites.reserve(160);
        const auto emit=[&](Vector3 p,Vector3 h,Vector3 v,Vector4 c,float atlas=16.0f){sprites.push_back({p,h,v,c,atlas});};
        constexpr float pi=3.14159265358979323846f;
        for(const auto& source:sources_)for(size_t barrel=0;barrel<source.muzzleCount;++barrel) {
            const Vector3 forward{right.y*up.z-right.z*up.y,right.z*up.x-right.x*up.z,right.x*up.y-right.y*up.x};
            const auto& palette=RailEnemyEnergy::For(source.weapon==RailWarningWeapon::Heavy?RailEnemyEnergy::BatteryKind::Heavy:
                source.weapon==RailWarningWeapon::Rapid?RailEnemyEnergy::BatteryKind::Gatling:RailEnemyEnergy::BatteryKind::None);
            const auto anchor=source.muzzles[barrel];
            const float depth=(std::max)(0.0f,Dot(Sub(anchor,eye),forward));
            const float r=source.weapon==RailWarningWeapon::Ordinary
                ?OrdinaryWorldRadius(source.radius,depth*pixelWorldScale)
                :BatteryWorldRadius(source.weapon,source.radius,depth*pixelWorldScale);
            // Lift only the soft sprite surface off the physical barrel. The
            // muzzle/aim anchor itself is unchanged, and terrain still occludes.
            const auto muzzle=Add(anchor,Mul(forward,-.12f));
            if(source.fired) {
                const float a=source.flash;
                emit(muzzle,Mul(right,r*(1+1.8f*(1-a))),Mul(up,r*(1+1.8f*(1-a))),RailEnemyEnergy::Alpha(palette.halo,a*.65f));
                emit(muzzle,Mul(right,r*.58f),Mul(up,r*.58f),RailEnemyEnergy::Alpha(palette.hot,a));
                emit(muzzle,Mul(right,r*2.2f),Mul(up,r*.13f),RailEnemyEnergy::Alpha(palette.trail,a),26);
                if(source.weapon==RailWarningWeapon::Heavy)
                    emit(muzzle,Mul(right,r*.15f),Mul(up,r*1.8f),RailEnemyEnergy::Alpha(palette.hot,a),26);
                else if(source.weapon==RailWarningWeapon::Rapid)for(float side:{-1.0f,1.0f})
                    emit(Add(muzzle,Mul(right,side*r*.32f)),Mul(right,r*.12f),Mul(up,r*.80f),RailEnemyEnergy::Alpha(palette.hot,a),32);
                continue;
            }
            if(source.onset>0) {
                const float size=r*(2.0f+.8f*(1-source.onset));
                emit(muzzle,Mul(right,size),Mul(up,size),RailEnemyEnergy::Alpha(palette.halo,
                    source.onset*(source.primary?.60f:.48f)));
            }
            if(source.weapon==RailWarningWeapon::Ordinary) {
                const auto style=OrdinaryStyle(source);
                const float c=source.charge,gain=style.opacity;
                emit(muzzle,Mul(right,r*1.65f),Mul(up,r*1.65f),RailEnemyEnergy::Alpha(palette.halo,(.42f+.23f*c)*gain));
                emit(muzzle,Mul(right,r*.92f),Mul(up,r*.92f),RailEnemyEnergy::Alpha(palette.surface,.90f*gain));
                emit(muzzle,Mul(right,r*(source.imminent?.64f:.45f)),Mul(up,r*(source.imminent?.64f:.45f)),
                    RailEnemyEnergy::Alpha(palette.hot,(source.imminent?1.0f:.92f)*gain));
                for(int i=0;i<4;++i) {
                    const float angle=pi*.25f+pi*.5f*i;
                    const auto direction=Add(Mul(right,std::cos(angle)),Mul(up,std::sin(angle)));
                    const auto tangent=Add(Mul(up,std::cos(angle)),Mul(right,-std::sin(angle)));
                    const auto center=Add(muzzle,Mul(direction,r*style.rayRadius/10));
                    emit(center,Mul(direction,r*style.rayLength/20),Mul(tangent,r*.14f),
                        RailEnemyEnergy::Alpha(palette.trail,gain),32);
                }
                continue;
            }
            const auto style=BatteryStyle(source);
            const float c=source.charge,gain=style.opacity;
            const bool heavy=source.weapon==RailWarningWeapon::Heavy;
            emit(muzzle,Mul(right,r*(heavy?1.65f:1.25f)),Mul(up,r*(heavy?1.65f:1.25f)),
                RailEnemyEnergy::Alpha(palette.halo,(.44f+.26f*c)*gain));
            emit(muzzle,Mul(right,r*.90f),Mul(up,r*.90f),RailEnemyEnergy::Alpha(palette.surface,.95f*gain));
            if(heavy) {
                emit(muzzle,Mul(right,r*(source.imminent?.66f:.42f+.12f*c)),Mul(up,r*(source.imminent?.66f:.42f+.12f*c)),
                    RailEnemyEnergy::Alpha(palette.hot,(source.imminent?1.0f:.92f)*gain));
                for(int i=0;i<8;++i) {
                    const float angle=pi*.25f*i;
                    const auto direction=Add(Mul(right,std::cos(angle)),Mul(up,std::sin(angle)));
                    const auto tangent=Add(Mul(up,std::cos(angle)),Mul(right,-std::sin(angle)));
                    const auto center=Add(muzzle,Mul(direction,r*style.extent/17));
                    emit(center,Mul(direction,r*.42f),Mul(tangent,r*.18f),RailEnemyEnergy::Alpha(palette.trail,gain),32);
                }
            } else {
                for(float side:{-1.0f,1.0f}) {
                    emit(Add(muzzle,Mul(right,side*r*.28f)),Mul(right,r*.22f),Mul(up,r*(source.imminent?.68f:.48f)),
                        RailEnemyEnergy::Alpha(palette.hot,(source.imminent?1.0f:.92f)*gain),32);
                    for(int stage=0;stage<3;++stage) {
                        const auto center=Add(muzzle,Add(Mul(right,side*r*style.extent/13),Mul(up,r*float(stage-1))));
                        const float a=stage<style.litStages?.95f:.16f;
                        emit(center,Mul(right,r*.40f),Mul(up,r*.16f),RailEnemyEnergy::Alpha(palette.trail,a*gain),32);
                    }
                }
            }
        }
        return sprites;
    }

    static RailIncomingWarning Incoming(const CourseSpawnRuntime& runtime,const RailPath& rail,
            float playerDistance,float lateral,float vertical,float playerSpeed,float playerRadius=1.6f) {
        RailIncomingWarning best;float bestTime=2;
        const auto player=ResolveEnemyProjectileWorldPosition(rail,{lateral,vertical,playerDistance});
        constexpr float step=.01f;
        // Rail distance is not uniformly parameterized within every cubic.
        // Forecast through the same world transform as actual movement.
        const auto playerNext=ResolveEnemyProjectileWorldPosition(rail,{lateral,vertical,playerDistance+playerSpeed*step});
        const auto playerVelocity=Mul(Sub(playerNext,player),1/step);
        for(const auto& shot:runtime.Bullets()) {
            if(!shot.active || shot.hitConsumed)continue;
            const float distance=shot.spawnDistance+shot.distanceOffset;
            const auto position=ResolveEnemyProjectileWorldPosition(rail,{shot.lateralOffset,shot.verticalOffset,distance});
            const auto next=ResolveEnemyProjectileWorldPosition(rail,{shot.lateralOffset+shot.lateralSpeed*step,
                shot.verticalOffset+shot.verticalSpeed*step,distance+shot.forwardSpeed*step});
            const auto velocity=Mul(Sub(next,position),1/step);
            const auto delta=Sub(position,player),relative=Sub(velocity,playerVelocity);
            const float speed2=Dot(relative,relative);
            if(speed2<.01f)continue;
            const float time=-Dot(delta,relative)/speed2;
            if(time<0 || time>1.15f || time>=bestTime)continue;
            const float miss=std::sqrt(Dot(Add(delta,Mul(relative,time)),Add(delta,Mul(relative,time))));
            if(miss>playerRadius+shot.radius+.6f)continue;
            bestTime=time;best={shot.projectileId,position,time,miss,shot.radius,true,time<=.40f,
                shot.shootDownEnabled && HasDefenseResponse(shot.defenseResponses,EnemyAttackDefenseResponse::ShootDown)};
        }
        return best;
    }
private:
    struct Flash {uint64_t sequence=0;float started=0;std::array<Vector3,2> muzzles{};size_t count=0;};
    static Vector3 Add(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
    static Vector3 Sub(Vector3 a,Vector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
    static Vector3 Mul(Vector3 a,float s){return {a.x*s,a.y*s,a.z*s};}
    static float Dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static float Distance(Vector3 a,Vector3 b){const auto d=Sub(a,b);return std::sqrt(Dot(d,d));}
    std::vector<RailWarningSource> sources_;
    std::unordered_map<uint32_t,Flash> flashes_;
    uint32_t primary_=0;
    float clock_=0;
};
