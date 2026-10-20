#pragma once

#include "WeaponFeedbackSystem.h"
#include "TwinShieldDronePose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace rail_primary_kill_detail {
inline Vector3 Add(Vector3 a,Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3 Scale(Vector3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline bool Finite(Vector3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
inline Vector3 Unit(Vector3 p,Vector3 fallback) {
    const float length=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    return length>0.0001f&&std::isfinite(length)?Scale(p,1/length):fallback;
}
inline float Ramp(float age,float begin,float end) {
    const float t=(std::clamp)((age-begin)/(end-begin),0.0f,1.0f);
    return t*t*(3-2*t);
}
}

enum class RailPrimaryKillStrokeKind { Strike,Cross,Spark };
struct RailPrimaryKillStroke final {
    Vector3 start{},end{};
    float width=0,opacity=0;
    RailPrimaryKillStrokeKind kind=RailPrimaryKillStrokeKind::Strike;
};
struct RailPrimaryKillCloud final {
    Vector3 center{};
    float radius=0,aspect=1,rotation=0,opacity=0,phase=0;
    uint32_t variant=0;
};
struct RailPrimaryKillFrame final {
    std::array<RailPrimaryKillStroke,14> strokes{};
    std::array<RailPrimaryKillCloud,6> fire{};
    std::array<RailPrimaryKillCloud,5> smoke{};
    size_t count=0;
    size_t fireCount=0,smokeCount=0;
    Vector3 center{};
    float flash=0;
};
struct RailPrimaryKill final {
    static constexpr float Lifetime=0.72f;
    uint64_t shotId=0;
    uint32_t actorId=0;
    Vector3 position{},normal{},incoming{};
    float size=1,age=0;
    TwinShieldDronePose dronePose{};
    bool hasDronePose=false;

    Vector3 CoreAt(float time) const {
        using namespace rail_primary_kill_detail;
        if(!hasDronePose) return Add(position,Scale(normal,(std::max)(0.10f,size*0.06f)));
        const auto body=EvaluateTwinShieldDroneDestructionBody(dronePose,time);
        return {body.m[3][0]-0.94f*body.m[2][0],body.m[3][1]-0.94f*body.m[2][1],
            body.m[3][2]-0.94f*body.m[2][2]};
    }
    RailPrimaryKillFrame Evaluate(Vector3 cameraRight,Vector3 cameraUp) const {
        using namespace rail_primary_kill_detail;
        RailPrimaryKillFrame frame;
        if(age<0 || !std::isfinite(age) || age>=Lifetime) return frame;
        auto right=Unit(cameraRight,{1,0,0}),up=Unit(cameraUp,{0,1,0});
        frame.center=CoreAt(age);
        if(hasDronePose) {
            const auto body=EvaluateTwinShieldDroneDestructionBody(dronePose,age);
            // Use the actual sensor plane rather than the collision sphere's
            // front. The strike follows the recoil and its brief local hold.
            right=Unit({body.m[0][0],body.m[0][1],body.m[0][2]},right);
            up=Unit({body.m[1][0],body.m[1][1],body.m[1][2]},up);
        }
        frame.flash=1-Ramp(age,0.006f,0.032f);
        const float sign=shotId%2?1.0f:-1.0f;
        const float tilt=sign*0.68f+0.12f*(incoming.x*right.x+incoming.y*right.y+incoming.z*right.z);
        const auto axis=Add(Scale(right,std::cos(tilt)),Scale(up,std::sin(tilt)));
        const auto stroke=[&](Vector3 center,Vector3 direction,float halfLength,float width,float opacity,RailPrimaryKillStrokeKind kind) {
            if(opacity<=0.001f || frame.count>=frame.strokes.size()) return;
            frame.strokes[frame.count++]={Add(center,Scale(direction,-halfLength)),
                Add(center,Scale(direction,halfLength)),width,opacity,kind};
        };
        // One short glint supports the internal burst; it no longer crosses
        // the entire shield silhouette or draws a persistent X over the hull.
        stroke(frame.center,axis,size*(0.65f+0.35f*Ramp(age,0,0.018f)),
            size*0.055f,(1-Ramp(age,0.020f,0.075f))*0.38f,RailPrimaryKillStrokeKind::Strike);
        // Ejected material keeps its emission frame in world space. Recoil
        // moves the sensor, but cannot pull already released fire/smoke back.
        auto emitRight=right,emitUp=up;
        if(hasDronePose) {
            const auto body=EvaluateTwinShieldDroneDestructionBody(dronePose,0.025f);
            emitRight=Unit({body.m[0][0],body.m[0][1],body.m[0][2]},right);
            emitUp=Unit({body.m[1][0],body.m[1][1],body.m[1][2]},up);
        }
        for(uint32_t i=0;i<6;++i) {
            const float born=i==0?0.0f:0.004f+0.005f*float(i),life=0.15f+0.008f*float(i%3);
            const float elapsed=age-born;
            if(elapsed<0 || elapsed>=life) continue;
            const float phase=elapsed/life,angle=float(i)*2.399963f+float(shotId%23)*0.41f;
            const auto radial=Add(Scale(emitRight,std::cos(angle)),Scale(emitUp,std::sin(angle)));
            const float travel=i==0?0.0f:size*(0.06f+2.4f*elapsed);
            const auto center=Add(CoreAt((std::min)(age,born+0.012f)),
                Add(Scale(radial,travel),Scale(normal,size*(0.08f+elapsed*0.6f))));
            const float radius=size*(i==0?0.18f:0.12f)+size*(i==0?0.80f:0.55f)*(1-std::exp(-elapsed*45));
            frame.fire[frame.fireCount++]={center,radius,0.85f+0.11f*float(i%3),angle,
                (1-Ramp(phase,0.28f,1.0f))*(i==0?0.90f:0.60f),phase,uint32_t((shotId+i*3)%16)};
        }
        for(uint32_t i=0;i<5;++i) {
            const float born=0.065f+0.012f*float(i),life=0.49f+0.025f*float(i%3),elapsed=age-born;
            if(elapsed<=0 || elapsed>=life) continue;
            const float angle=float(i)*2.399963f+float(shotId%19),phase=elapsed/life;
            const auto radial=Add(Scale(emitRight,std::cos(angle)),Scale(emitUp,std::sin(angle)));
            const auto center=Add(CoreAt(0.025f),Add(Scale(radial,size*(0.12f+0.65f*elapsed)),
                Add(Scale(normal,size*0.14f),Vector3{0,size*elapsed*0.85f,0})));
            frame.smoke[frame.smokeCount++]={center,size*(0.24f+0.70f*(1-std::exp(-elapsed*5))),
                1.05f+0.12f*float(i%2),angle+elapsed*0.45f,
                Ramp(elapsed,0,0.050f)*(1-Ramp(phase,0.18f,1.0f))*0.46f,phase,uint32_t((shotId+i)%64)};
        }
        for(int i=0;i<12;++i) {
            const float elapsed=age-(0.045f+0.002f*i),life=0.19f+0.018f*(i%4);
            if(elapsed<0 || elapsed>=life) continue;
            // Two narrow fracture fans follow the main strike instead of an
            // evenly spaced firework circle. Larger chips linger in the tail.
            const float angle=tilt+(i%2?3.14159265f:0.0f)+0.30f*std::sin(float(shotId%97)+i*2.399963f);
            const auto radial=Add(Scale(emitRight,std::cos(angle)),Scale(emitUp,std::sin(angle)));
            const auto velocity=Scale(Add(Scale(radial,11.0f+float(i%3)*3),Scale(normal,2)),size);
            const float drag=std::exp(-elapsed*5),travel=(1-drag)/5;
            const auto head=Add(CoreAt(0.045f+0.002f*i),Add(Scale(velocity,travel),Vector3{0,-4.9f*elapsed*elapsed,0}));
            const auto direction=Unit(Add(Scale(velocity,drag),Vector3{0,-9.8f*elapsed,0}),radial);
            const float fade=1-elapsed/life;
            stroke(head,direction,size*(0.07f+0.15f*fade),size*(i%4==0?0.05f:0.028f),fade*fade,RailPrimaryKillStrokeKind::Spark);
        }
        return frame;
    }
};

// Receives only accepted feedback matched to primary-shot IDs. No damage,
// critical-hit probability, new collision query or repeated kill is created.
class RailPrimaryKillPresentation final {
public:
    static constexpr size_t Capacity=16;
    void Reset() { kills_.clear(); history_={}; historyCursor_=newCount_=0; lastSequence_=0; }
    void Update(float dt) {
        newCount_=0;
        if(!std::isfinite(dt) || dt<=0) return;
        for(auto& kill:kills_) kill.age+=dt;
        std::erase_if(kills_,[](const auto& kill){return kill.age>=RailPrimaryKill::Lifetime;});
    }
    uint64_t LastObservedSequence() const { return lastSequence_; }
    bool ObserveFeedback(const WeaponFeedbackEvent& event,float size=1,const TwinShieldDronePose* pose=nullptr) {
        if(event.sequence==0 || event.sequence<=lastSequence_) return false;
        lastSequence_=event.sequence;
        return Enqueue(event,size,pose);
    }
    bool Enqueue(const WeaponFeedbackEvent& event,float size=1,const TwinShieldDronePose* pose=nullptr) {
        using namespace rail_primary_kill_detail;
        if(!event.shotId || !event.targetActorId || !event.destroyed || event.lockOnContact || event.blocked ||
            event.hitKind!=RailAimHitKind::Enemy || event.damageType!=WeaponDamageType::Energy ||
            event.feedbackKind!=HitFeedbackKind::Destroyed || !std::isfinite(event.appliedDamage) || event.appliedDamage<=0 ||
            !Finite(event.worldPosition) || !Finite(event.worldNormal) || !Finite(event.incomingDirection) || !std::isfinite(size) ||
            std::find(history_.begin(),history_.end(),event.shotId)!=history_.end()) return false;
        history_[historyCursor_++%history_.size()]=event.shotId;
        if(kills_.size()>=Capacity) kills_.erase(kills_.begin());
        kills_.push_back({event.shotId,event.targetActorId,event.worldPosition,Unit(event.worldNormal,{0,0,-1}),
            Unit(event.incomingDirection,{0,0,1}),(std::clamp)(size,0.65f,2.4f),0});
        if(pose && pose->visible && pose->primaryDestruction) {
            kills_.back().dronePose=*pose;
            kills_.back().hasDronePose=true;
        }
        ++newCount_;
        return true;
    }
    bool Visible() const { return !kills_.empty(); }
    bool ContainsActor(uint32_t actorId) const {
        return std::any_of(kills_.begin(),kills_.end(),[actorId](const auto& kill){return kill.actorId==actorId;});
    }
    size_t NewCount() const { return newCount_; }
    std::span<const RailPrimaryKill> Kills() const { return kills_; }
private:
    std::vector<RailPrimaryKill> kills_;
    std::array<uint64_t,128> history_{};
    size_t historyCursor_=0,newCount_=0;
    uint64_t lastSequence_=0;
};
