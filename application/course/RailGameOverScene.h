#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "GameSessionSystem.h"
#include "RailVehicleRenderer.h"
#include "RailVehicleBreakawayGeometry.h"
#include "RailVehicleWheelContactPresentationBridge.h"
#include "RailVehicleOccupantActor.h"
#include "CourseRailTrackMeshBakePipeline.h"
#include "../terrain/RailPath.h"
#include "../terrain/TerrainGroundSurface.h"

namespace rail_game_over_detail {
inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3 Mul(Vector3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vector3 a,Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vector3 Cross(Vector3 a,Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline Vector3 Unit(Vector3 v,Vector3 fallback={0,0,1}) {
    const float n=std::sqrt(Dot(v,v));return n>0.0001f?Mul(v,1/n):fallback;
}
inline Vector3 Origin(const Matrix4x4& m) { return {m.m[3][0],m.m[3][1],m.m[3][2]}; }
inline Matrix4x4 Basis(Vector3 right,Vector3 up,Vector3 forward,Vector3 position) {
    Matrix4x4 m=MakeIdentity4x4();const Vector3 rows[]{right,up,forward,position};
    for(int i=0;i<4;++i) {m.m[i][0]=rows[i].x;m.m[i][1]=rows[i].y;m.m[i][2]=rows[i].z;}
    return m;
}
inline Matrix4x4 RollAt(Vector3 pivot,Vector3 forward,Vector3 up,float radians) {
    forward=Unit(forward);const auto right=Unit(Cross(up,forward),{1,0,0});up=Cross(forward,right);
    auto frame=Basis(right,up,forward,pivot);
    auto roll=MakeIdentity4x4();const float c=std::cos(radians),sn=std::sin(radians);
    // Row-vector clockwise rotation: the left wheels rise around the right rail.
    roll.m[0][0]=c;roll.m[0][1]=-sn;roll.m[1][0]=sn;roll.m[1][1]=c;
    return Multiply(Multiply(Inverse(frame),roll),frame);
}
inline float Ramp(float t,float a,float b) {
    const float u=(std::clamp)((t-a)/(b-a),0.0f,1.0f); return u*u*(3-2*u);
}
inline float CameraEase(float t,float a,float b) {
    const float u=(std::clamp)((t-a)/(b-a),0.0f,1.0f);
    return u*u*u*(u*(u*6-15)+10);
}
inline Vector3 Mix(Vector3 a,Vector3 b,float t) { return Add(Mul(a,1-t),Mul(b,t)); }
inline Vector3 Point(Vector3 p,const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
}

enum class RailGameOverAction { None, Checkpoint, Title };
struct RailGameOverInput {
    bool enabled=false, up=false, down=false, confirm=false, retry=false, title=false;
    bool pointerValid=false, pointerPressed=false;
    float pointerX=0,pointerY=0,width=1600,height=900;
};
struct RailGameOverButton {
    float x,y,width,height;
    bool Contains(float px,float py) const { return px>=x && px<=x+width && py>=y && py<=y+height; }
};
struct RailGameOverLayout {
    float scale,menuCenterX,centerY;
    RailGameOverButton preview;
};
inline RailGameOverLayout GameOverLayout(float width,float height) {
    return {(std::min)(width/1600.0f,height/900.0f),width*0.72f,height*0.5f,
        {width*0.035f,height*0.235f,width*0.445f,height*0.61f}};
}
inline RailGameOverButton GameOverButton(float width,float height,int index) {
    const auto layout=GameOverLayout(width,height);const float s=layout.scale;
    return {layout.menuCenterX-240*s,layout.centerY+(62+index*76)*s,480*s,60*s};
}

struct RailGameOverSmoke { Vector3 center{};float radius=0,opacity=0;uint32_t seed=0;bool blast=false; };
struct RailGameOverDebris { Vector3 center{},tail{};float radius=0,opacity=0; };
struct RailGameOverFire { Vector3 center{};float radius=0,opacity=0,phase=0,rotation=0,aspect=1;uint32_t variant=0;bool fault=false; };
struct RailGameOverShock { Vector3 center{},right{1,0,0},forward{0,0,1};float radius=0,phase=0,opacity=0; };
struct RailGameOverSpark { Vector3 head{},tail{};float opacity=0; };
struct RailGameOverFrame {
    bool active=false, wreck=false, menuReady=false,grounded=false,bouncing=false;
    bool wheelDetached=false,sidePanelDetached=false,sidePanelPeeling=false,cornerContact=false,shattered=false;
    float elapsed=0,blackout=0,titleOpacity=0,menuOpacity=0,speedRatio=1;
    float cameraFovY=0.82f,impactFlash=0,blastLight=0,cameraBlend=0,hudOpacity=0,groundContactTime=0,secondaryContactTime=-1,recontactTime=-1,stopTime=-1,slideDistance=0,trackDistance=0,shatterTime=-1;
    int selection=0;
    Vector3 cameraEye{},cameraTarget{},cameraUp{0,1,0},groundContactPoint{},blastOrigin{};
    RailVehicleRenderFrame vehicle{},sidePanel{};
    std::array<RailVehicleRenderFrame,RailVehicleBreakaway::FinalPieceCount> fractures{};
    size_t fractureCount=0,faultBurstCount=0;
    RailVehicleWheelContactPresentationFrame wheels{};
    RailVehicleOccupantActorFrame occupant{};
    std::array<RailGameOverSmoke,64> smoke{};
    std::array<RailGameOverSmoke,32> dust{};
    std::array<RailGameOverDebris,48> debris{};
    RailGameOverShock shock{};
    std::array<RailGameOverFire,8> fire{};
    std::array<RailGameOverSpark,256> sparks{};
    size_t smokeCount=0,sparkCount=0,dustCount=0,debrisCount=0,fireCount=0;
};

// Presentation owns the wreck. Authoritative rail distance, combat and retry
// snapshots remain frozen, so derailment can never corrupt the checkpoint.
class RailGameOverScene final {
public:
    using GroundQuery=std::function<bool(Vector3,float,TerrainGroundSurfaceHit&)>;
    struct FaultBurst { float time,side,strength; };
    // Six short electrical/mechanical ruptures: left, right, then quicker.
    static constexpr std::array<FaultBurst,6> FaultBursts{{
        {0.28f,-1,0.65f},{0.52f,1,0.72f},{0.73f,-1,0.80f},
        {0.91f,1,0.88f},{1.07f,-1,0.96f},{1.21f,1,1.05f}
    }};
    static constexpr float CameraMoveBegin=0.40f, CameraMoveEnd=1.10f;
    static constexpr float UnstableBegin=0.16f, BrakeBegin=0.36f, WheelLiftBegin=1.18f, DerailBegin=1.48f, DetachBegin=1.63f;
    static constexpr float PanelPeelBegin=DerailBegin+0.03f,PanelDetachTime=DetachBegin+0.28f,WheelDetachTime=DetachBegin+0.05f;
    static constexpr float CornerFallDuration=0.18f,BounceDuration=0.70710678f,BounceEnd=CornerFallDuration+BounceDuration;
    static constexpr float FadeBegin=2.67f, FadeEnd=3.17f, TitleBegin=3.27f, MenuBegin=3.67f;
    void Reset() { *this=RailGameOverScene{}; }
    const RailGameOverFrame& Frame() const { return frame_; }
    bool Active() const { return frame_.active; }
    float RecontactTime() const { return hasRecontact_?DetachBegin+impactFlight_+CornerFallDuration+hopContactAge_:-1.0f; }
    float StopTime() const { return hasRecontact_?RecontactTime()+slideDuration_:-1.0f; }
    float ShatterTime() const { return panelSupported_ && hasRecontact_?RecontactTime():-1.0f; }
    float FadeStartTime() const {
        if(ShatterTime()>=0) return (std::max)({FadeBegin,StopTime()+0.18f,ShatterTime()+0.85f});
        if(hasRecontact_) return (std::max)(FadeBegin,StopTime()+0.18f);
        return hasImpact_?(std::max)(FadeBegin,DetachBegin+impactFlight_+CornerFallDuration+1.4f):FadeBegin;
    }
    float FadeEndTime() const { return FadeStartTime()+FadeEnd-FadeBegin; }
    void Begin(const GameSessionRuntimeState& session,const RailVehicleRuntimeState& state,
        const RailVehicleActorFrame& actor,const RailVehicleRenderFrame& vehicle,
        const RailVehicleWheelContactPresentationFrame& wheels,
        const RailVehicleOccupantActorFrame& occupant,Vector3 eye,Vector3 forward,const RailPath& path,
        Vector3 cameraUp={0,1,0},float cameraFovY=0.82f,
        const CourseRailTrackMeshBakeResult* track=nullptr,const RailVehicleTrackContactPoseSettings* trackSettings=nullptr,
        GroundQuery groundQuery={}) {
        Reset(); frame_.active=true;
        frame_.wreck=session.endReason==GameSessionEndReason::PlayerDestroyed;
        initialActor_=actor;initialVehicle_=vehicle;initialWheels_=wheels;initialOccupant_=occupant;
        groundQuery_=std::move(groundQuery);
        distance_=state.distance;speed_=(std::max)(0.0f,state.speed);
        initialEye_=eye;initialTarget_=rail_game_over_detail::Add(eye,rail_game_over_detail::Mul(forward,24));
        initialUp_=rail_game_over_detail::Unit(cameraUp,{0,1,0});
        initialFovY_=std::isfinite(cameraFovY)?(std::clamp)(cameraFovY,0.15f,2.8f):0.82f;
        const auto base=path.Evaluate(distance_);
        // Capture actual wheel/rail contacts, not a guessed body-centre pivot.
        for(size_t i=0;i<contacts_.size();++i) contacts_[i]=wheels.valid?wheels.wheels[i].railContact:
            rail_game_over_detail::Add(base.position,rail_game_over_detail::Add(
                rail_game_over_detail::Mul(base.right,i%2?1.9f:-1.9f),rail_game_over_detail::Mul(base.tangent,i<2?-3.2f:3.2f)));
        using namespace rail_game_over_detail;
        track_=track && track->valid?track:nullptr;
        if(track_) for(const auto& instance:track_->instances) {
            if(instance.part==CourseRailTrackMeshPart::LeftRail) rails_[0].push_back(&instance);
            if(instance.part==CourseRailTrackMeshPart::RightRail) rails_[1].push_back(&instance);
        }
        for(size_t i=0;i<4;++i) {
            const auto local=Add(contacts_[i],Mul(base.position,-1));
            contactOffsets_[i]=trackSettings?(i<2?-0.5f:0.5f)*trackSettings->wheelbase:Dot(local,base.tangent);
            contactLanes_[i]=Dot(local,base.right);contactHeights_[i]=Dot(local,base.up);
            contactResidual_[i]=Add(contacts_[i],Mul(RailAnchorAt(0,i,path).point,-1));
        }
        auto initialSupport=Support(0,path).world;initialSupportInverse_=Inverse(initialSupport);
        initialBodyLocal_=Multiply(initialVehicle_.worldMatrix,initialSupportInverse_);
        auto originalBody=initialVehicle_.worldMatrix;initialBodyInverse_=Inverse(originalBody);
        bodyHeight_=trackSettings?trackSettings->bodyPivotHeightAboveContacts+trackSettings->contactClearance:Origin(initialBodyLocal_).y;
        const auto eyeOffset=Add(initialEye_,Mul(Origin(initialVehicle_.worldMatrix),-1));
        rearCameraBack_=(std::max)(18.0f,-Dot(eyeOffset,base.tangent)+2.0f);
        rearCameraHeight_=(std::max)(6.0f,Dot(eyeOffset,base.up)+1.0f);
        if(frame_.wreck) {PrepareRelease(path);PrepareBlast();PrepareParts(path);PrepareFinale(path);}
        if(!session.canRetry) frame_.selection=1;
        Evaluate(path);
    }
    RailGameOverAction Update(float dt,const RailPath& path,bool canRetry,const RailGameOverInput& in={}) {
        if(!Active()) return RailGameOverAction::None;
        const bool readyBefore=frame_.menuReady;
        if(std::isfinite(dt) && dt>0) frame_.elapsed+=(std::min)(dt,0.1f);
        Evaluate(path);
        if(!canRetry) frame_.selection=1;
        // Consume every edge during the reveal; opening input cannot activate
        // an action on the very frame that the menu becomes available.
        if(!in.enabled || !readyBefore || !frame_.menuReady) { pointerKnown_=false;return RailGameOverAction::None; }
        if(in.up || in.down) frame_.selection=canRetry?1-frame_.selection:1;
        if(in.pointerValid) {
            const bool moved=pointerKnown_ && (std::abs(in.pointerX-pointerX_)>1 || std::abs(in.pointerY-pointerY_)>1);
            pointerKnown_=true;pointerX_=in.pointerX;pointerY_=in.pointerY;
            for(int i=0;i<2;++i) if((i!=0 || canRetry) && GameOverButton(in.width,in.height,i).Contains(pointerX_,pointerY_)) {
                if(moved || in.pointerPressed) frame_.selection=i;
                if(in.pointerPressed) return i==0?RailGameOverAction::Checkpoint:RailGameOverAction::Title;
            }
        } else pointerKnown_=false;
        if(in.title) return RailGameOverAction::Title;
        if(in.retry && canRetry) return RailGameOverAction::Checkpoint;
        if(in.confirm) return frame_.selection==0 && canRetry?RailGameOverAction::Checkpoint:RailGameOverAction::Title;
        return RailGameOverAction::None;
    }
private:
    static constexpr float RailDrag=0.14f, ReleaseRoll=40.0f, GroundRetention=0.48f, GroundDrag=3.2f;
    static constexpr float FallGravity=28.0f,ReboundGravity=52.0f,ReboundHeight=3.25f;
    static constexpr float Radians=0.01745329252f;
    float RailSpeedRatio(float t) const { return std::exp(-RailDrag*(std::max)(0.0f,t-BrakeBegin)); }
    float Travel(float t) const {
        const float coast=(std::min)(t,BrakeBegin),brake=(std::max)(0.0f,t-BrakeBegin);
        return speed_*(coast+(1-std::exp(-RailDrag*brake))/RailDrag);
    }
    struct WreckPose { Matrix4x4 delta=MakeIdentity4x4(),world=MakeIdentity4x4();bool grounded=false,bouncing=false;Vector3 contact{}; };
    struct RailAnchor { Vector3 point{},right{1,0,0},up{0,1,0},forward{0,0,1}; };
    struct RailSupport { std::array<RailAnchor,4> wheels{};Matrix4x4 world=MakeIdentity4x4();Vector3 centre{},up{0,1,0},forward{0,0,1}; };
    RailAnchor RailAnchorAt(float t,size_t wheel,const RailPath& path) const {
        using namespace rail_game_over_detail;
        const float d=distance_+Travel(t)+contactOffsets_[wheel];
        RailAnchor out;const auto& rail=rails_[wheel%2];
        if(!rail.empty()) {
            auto found=std::upper_bound(rail.begin(),rail.end(),d,
                [](float value,const CourseRailTrackBakedInstance* item){return value<item->startDistance;});
            if(found!=rail.begin()) --found;
            const auto& segment=**found;
            const float u=(std::clamp)((d-segment.startDistance)/(segment.endDistance-segment.startDistance),0.0f,1.0f);
            // Sample the actual rendered rail-head top, including its straight
            // baked chord and course-selected gauge/height, not a parallel rig.
            out.point=Point({0,0.5f,u-0.5f},segment.worldMatrix);
            out.right=Unit({segment.worldMatrix.m[0][0],segment.worldMatrix.m[0][1],segment.worldMatrix.m[0][2]},{1,0,0});
            out.up=Unit({segment.worldMatrix.m[1][0],segment.worldMatrix.m[1][1],segment.worldMatrix.m[1][2]},{0,1,0});
            out.forward=Unit({segment.worldMatrix.m[2][0],segment.worldMatrix.m[2][1],segment.worldMatrix.m[2][2]});
        } else {
            const auto sample=path.EvaluateExtended(d);
            out.point=Add(sample.position,Add(Mul(sample.right,contactLanes_[wheel]),Mul(sample.up,contactHeights_[wheel])));
            out.right=sample.right;out.up=sample.up;out.forward=sample.tangent;
        }
        out.point=Add(out.point,Mul(contactResidual_[wheel],1-CameraEase(t,0,0.25f)));
        return out;
    }
    RailSupport Support(float t,const RailPath& path) const {
        using namespace rail_game_over_detail;
        RailSupport out;
        for(size_t i=0;i<4;++i) {out.wheels[i]=RailAnchorAt(t,i,path);out.centre=Add(out.centre,Mul(out.wheels[i].point,0.25f));}
        const auto rear=Mul(Add(out.wheels[0].point,out.wheels[1].point),0.5f);
        const auto front=Mul(Add(out.wheels[2].point,out.wheels[3].point),0.5f);
        out.forward=Unit(Add(front,Mul(rear,-1)));
        auto right=Add(Add(out.wheels[1].point,Mul(out.wheels[0].point,-1)),Add(out.wheels[3].point,Mul(out.wheels[2].point,-1)));
        right=Unit(Add(right,Mul(out.forward,-Dot(right,out.forward))),{1,0,0});
        out.up=Unit(Cross(out.forward,right),{0,1,0});
        out.world=Basis(right,out.up,out.forward,out.centre);return out;
    }
    Matrix4x4 Transport(float t,const RailPath& path) const {
        return Multiply(initialSupportInverse_,Support(t,path).world);
    }
    float Lean(float t) const {
        using namespace rail_game_over_detail;
        const float vibration=(1.1f+0.8f*std::sin(t*29)+0.3f*std::sin(t*47))*Ramp(t,UnstableBegin,0.42f)*
            (1-Ramp(t,WheelLiftBegin-0.15f,WheelLiftBegin));
        float rupture=0;
        for(const auto& burst:FaultBursts) {
            const float age=t-burst.time;
            if(age>=0 && age<0.15f) rupture-=burst.side*burst.strength*1.9f*std::sin(age*46)*std::exp(-age*28)*(1-Ramp(age,0.09f,0.15f));
        }
        return vibration+rupture+WheelLean(t);
    }
    float WheelLean(float t) const {
        const float lift=(std::clamp)((t-WheelLiftBegin)/(DetachBegin-WheelLiftBegin),0.0f,1.0f);
        return ReleaseRoll*lift*lift;
    }
    WreckPose RailPose(float t,const RailPath& path) const {
        using namespace rail_game_over_detail;
        const float railTime=(std::min)(t,DetachBegin);
        const auto support=Support(railTime,path);
        const auto transport=Multiply(initialSupportInverse_,support.world);
        const auto rear=support.wheels[1].point,front=support.wheels[3].point;
        const auto pivot=Mul(Add(rear,front),0.5f);
        const auto sample=path.EvaluateExtended(distance_+Travel(railTime));
        const auto axis=Unit(Add(front,Mul(rear,-1)),sample.tangent);
        const auto roll=RollAt(pivot,axis,support.up,Lean(railTime)*Radians);
        WreckPose out;
        if(track_) {
            // Let the old cosmetic bank/suspension settle onto the running
            // gear before the actual one-sided lift, retaining the fatal pose.
            const float settle=CameraEase(railTime,0.12f,0.70f);
            const auto row=[&](int i){return Vector3{initialBodyLocal_.m[i][0],initialBodyLocal_.m[i][1],initialBodyLocal_.m[i][2]};};
            const auto localRight=Unit(Mix(Unit(row(0),{1,0,0}),{1,0,0},settle),{1,0,0});
            const auto upHint=Unit(Mix(Unit(row(1),{0,1,0}),{0,1,0},settle),{0,1,0});
            const auto localForward=Unit(Cross(localRight,upHint));
            const auto localUp=Cross(localForward,localRight);
            auto localPosition=Origin(initialBodyLocal_);
            localPosition=Mix(localPosition,{0,bodyHeight_,0},settle);
            const auto local=Basis(Mul(localRight,std::sqrt(Dot(row(0),row(0)))),
                Mul(localUp,std::sqrt(Dot(row(1),row(1)))),Mul(localForward,std::sqrt(Dot(row(2),row(2)))),localPosition);
            out.world=Multiply(Multiply(local,support.world),roll);
            out.delta=Multiply(initialBodyInverse_,out.world);
        } else {
            out.delta=Multiply(transport,roll);out.world=Multiply(initialVehicle_.worldMatrix,out.delta);
        }
        return out;
    }
    RailVehicleWheelContactVisual AttachedWheel(float t,size_t index,const RailPath& path) const {
        using namespace rail_game_over_detail;
        if(t<=0) return initialWheels_.wheels[index];
        const auto support=Support(t,path);const auto& anchor=support.wheels[index];
        const auto pivot=Mul(Add(support.wheels[1].point,support.wheels[3].point),0.5f);
        const auto axis=Unit(Add(support.wheels[3].point,Mul(support.wheels[1].point,-1)),support.forward);
        const auto roll=RollAt(pivot,axis,support.up,WheelLean(t)*Radians);
        auto out=initialWheels_.wheels[index];
        const auto length=[&](int row) {const auto& m=out.worldMatrix.m;return std::sqrt(m[row][0]*m[row][0]+m[row][1]*m[row][1]+m[row][2]*m[row][2]);};
        const float width=length(0),diameter=length(1),depth=length(2);
        const float radius=track_?diameter*0.5f:std::sqrt(Dot(Add(out.axleCenter,Mul(contacts_[index],-1)),Add(out.axleCenter,Mul(contacts_[index],-1))));
        const float angle=initialVehicle_.wheelRotationRadians+Travel(t)/(std::max)(0.05f,radius);
        const auto up=Add(Mul(anchor.up,std::cos(angle)),Mul(anchor.forward,std::sin(angle)));
        const auto forward=Add(Mul(anchor.forward,std::cos(angle)),Mul(anchor.up,-std::sin(angle)));
        const auto axle=Add(anchor.point,Mul(anchor.up,radius));
        out.worldMatrix=Multiply(Basis(Mul(anchor.right,width),Mul(up,diameter),Mul(forward,depth),axle),roll);
        out.railContact=Point(anchor.point,roll);out.axleCenter=Point(axle,roll);
        return out;
    }
    float Clearance(const Matrix4x4& world,Vector3* contact=nullptr,Vector3* normal=nullptr,Vector3* localContact=nullptr) const {
        using namespace rail_game_over_detail;
        float minimum=std::numeric_limits<float>::infinity();
        if(!groundQuery_) return minimum;
        struct Probe {float gap;TerrainGroundSurfaceHit hit;Vector3 local;};
        std::array<Probe,26> probes{};size_t count=0;
        const float ceiling=Origin(world).y+0.05f;
        for(float x:{-2.31f,0.0f,2.31f}) for(float y:{-0.68f,0.60f,1.88f}) for(float z:{-3.48f,0.0f,3.48f}) {
            if(x==0 && y==0.60f && z==0) continue;
            const auto p=Point({x,y,z},world);TerrainGroundSurfaceHit hit;
            if(!groundQuery_(p,ceiling,hit) || !std::isfinite(hit.position.y) || hit.normal.y<0.5f) continue;
            const float gap=p.y-hit.position.y;probes[count++]={gap,hit,{x,y,z}};minimum=(std::min)(minimum,gap);
        }
        if(count && localContact) {
            const auto found=std::min_element(probes.begin(),probes.begin()+count,[](const auto& a,const auto& b){return a.gap<b.gap;});
            *localContact=found->local;if(contact) *contact=found->hit.position;if(normal) *normal=found->hit.normal;
            return minimum;
        }
        if(count && (contact || normal)) {
            Vector3 sum{},normals{};float hits=0;
            for(size_t i=0;i<count;++i) if(probes[i].gap<=minimum+0.01f) {
                sum=Add(sum,probes[i].hit.position);normals=Add(normals,probes[i].hit.normal);++hits;
            }
            if(contact) *contact=Mul(sum,1/(std::max)(1.0f,hits));
            if(normal) *normal=Unit(normals,{0,1,0});
        }
        return minimum;
    }
    float AirRoll(float flight) const { return angularSpeed_*0.30f*(1-std::exp(-flight/0.23f)); }
    float AirPitch(float flight) const { return -34*Radians*(1-std::exp(-flight/0.28f)); }
    float AirYaw(float flight) const { return -30*Radians*(1-std::exp(-flight/0.50f)); }
    WreckPose AirPose(float flight) const {
        using namespace rail_game_over_detail;
        float roll=AirRoll(flight),pitch=AirPitch(flight),yaw=AirYaw(flight);
        WreckPose out;
        out.delta=Multiply(releaseDelta_,RollAt(releaseCentre_,releaseAxis_,releaseUp_,roll));
        out.delta=Multiply(out.delta,RollAt(releaseCentre_,releaseRight_,releaseUp_,pitch));
        out.delta=Multiply(out.delta,RollAt(releaseCentre_,Vector3{0,1,0},releaseForward_,yaw));
        const auto shift=Add(Mul(releaseVelocity_,flight),Vector3{0,-0.5f*FallGravity*flight*flight,0});
        out.delta=Multiply(out.delta,MakeTranslateMatrix(shift));
        out.world=Multiply(initialVehicle_.worldMatrix,out.delta);
        return out;
    }
    void PrepareRelease(const RailPath& path) {
        using namespace rail_game_over_detail;
        const auto support=Support(DetachBegin,path);
        const auto rear=support.wheels[1].point,front=support.wheels[3].point;
        const auto pivot=Mul(Add(rear,front),0.5f);
        const auto sample=path.EvaluateExtended(distance_+Travel(DetachBegin));
        releaseAxis_=Unit(Add(front,Mul(rear,-1)),sample.tangent);
        releaseUp_=support.up;releaseForward_=support.forward;releaseRight_=Unit(Cross(releaseUp_,releaseForward_),sample.right);
        const auto release=RailPose(DetachBegin,path);
        releaseDelta_=release.delta;releaseCentre_=Origin(release.world);
        angularSpeed_=2*ReleaseRoll/(DetachBegin-WheelLiftBegin)*Radians;
        const auto tangentVelocity=Cross(Mul(releaseAxis_,-angularSpeed_),Add(releaseCentre_,Mul(pivot,-1)));
        // Leave the rail at running speed, with a sharp outward kick. The
        // roll still starts around the loaded wheels, then damps in flight.
        releaseVelocity_=Add(Add(Mul(releaseForward_,speed_*(RailSpeedRatio(DetachBegin)+0.07f)),tangentVelocity),
            Add(Mul(releaseRight_,5.2f),Vector3{0,-1.1f,0}));
        hasImpact_=false;
        if(!groundQuery_) return;
        // Only the resident rendered floor may trigger a landing. Missing
        // ground stays airborne through blackout, never an invented rail plane.
        impactFlight_=0;hasImpact_=Clearance(release.world)<=0;
        if(!hasImpact_) {
            float previous=0;
            for(int i=1;i<=4*240;++i) {
                const float current=float(i)/240;
                if(Clearance(AirPose(current).world)<=0) {
                    float lo=previous,hi=current;
                    for(int j=0;j<16;++j) {const float mid=(lo+hi)*0.5f;if(Clearance(AirPose(mid).world)>0) lo=mid;else hi=mid;}
                    impactFlight_=hi;hasImpact_=true;break;
                }
                previous=current;
            }
        }
        if(!hasImpact_) return;
        auto impact=AirPose(impactFlight_);
        const float depth=Clearance(impact.world);
        impactDelta_=Multiply(impact.delta,MakeTranslateMatrix({0,-depth,0}));
        const auto impactWorld=Multiply(initialVehicle_.worldMatrix,impactDelta_);
        impactCentre_=Origin(impactWorld);Vector3 groundNormal{0,1,0};Clearance(impactWorld,&impactContact_,&groundNormal,&impactLocalContact_);impactNormal_=groundNormal;
        const auto impactVelocity=Add(releaseVelocity_,Vector3{0,-FallGravity*impactFlight_,0});
        skidVelocity_=Mul(Add(impactVelocity,Mul(groundNormal,-Dot(impactVelocity,groundNormal))),GroundRetention);
    }
    struct SkidMotion { float distance=0,speed=1; };
    SkidMotion SkidAt(float age) const {
        // First edge impact, a second loss of momentum when the side falls,
        // a short rebound, and finally a readable ground skid.
        SkidMotion out;float remaining=(std::max)(0.0f,age);
        const auto advance=[&](float duration,float drag) {
            const float dt=(std::min)(remaining,duration);
            out.distance+=out.speed*(1-std::exp(-drag*dt))/drag;
            out.speed*=std::exp(-drag*dt);remaining-=dt;
        };
        advance(CornerFallDuration,2.0f);
        if(age>=CornerFallDuration) out.speed*=0.72f;
        advance(BounceDuration,0.30f);
        advance(remaining,GroundDrag);
        return out;
    }
    WreckPose CornerPose(float age) const {
        using namespace rail_game_over_detail;
        const auto skid=SkidAt(age);
        const auto initial=Multiply(initialVehicle_.worldMatrix,impactDelta_);
        const auto row=[&](int i){return Vector3{initial.m[i][0],initial.m[i][1],initial.m[i][2]};};
        const auto finalRight=Mul(impactNormal_,-1);
        const auto forward=Unit(Add(row(2),Mul(impactNormal_,-Dot(row(2),impactNormal_))),releaseForward_);
        const float u=(std::clamp)(age/CornerFallDuration,0.0f,1.0f),fall=u*u;
        const auto right=Unit(Mix(Unit(row(0)),finalRight,fall),finalRight);
        const auto up=Unit(Cross(Mix(Unit(row(2)),forward,fall),right),releaseRight_);
        const auto heading=Unit(Cross(right,up),forward);
        auto world=Basis(Mul(right,std::sqrt(Dot(row(0),row(0)))),Mul(up,std::sqrt(Dot(row(1),row(1)))),
            Mul(heading,std::sqrt(Dot(row(2),row(2)))),{});
        // Keep the first corner attached to its moving ground contact. The
        // opposite side accelerates downward and arrives after 180 ms.
        const auto anchor=Add(impactContact_,Mul(skidVelocity_,skid.distance));
        world.m[3][0]=anchor.x-Point(impactLocalContact_,world).x;
        world.m[3][1]=anchor.y-Point(impactLocalContact_,world).y;
        world.m[3][2]=anchor.z-Point(impactLocalContact_,world).z;
        WreckPose out;out.world=world;out.grounded=true;
        const float depth=Clearance(out.world);
        if(std::isfinite(depth)) {
            out.world=Multiply(out.world,MakeTranslateMatrix({0,-depth,0}));
            Clearance(out.world,&out.contact);
        } else {
            out.grounded=false;
            out.world=Multiply(out.world,MakeTranslateMatrix({0,-0.5f*FallGravity*age*age,0}));
        }
        out.delta=Multiply(initialBodyInverse_,out.world);return out;
    }
    WreckPose BlastAir(float age) const {
        using namespace rail_game_over_detail;
        const float u=(std::clamp)(age/BounceDuration,0.0f,1.0f);
        auto world=Multiply(blastWorld_,RollAt(blastCentre_,blastForward_,blastNormal_,40*Radians*(1-std::exp(-age/0.24f))));
        world=Multiply(world,RollAt(blastCentre_,blastRight_,blastNormal_,-24*Radians*Ramp(u,0,1)));
        world=Multiply(world,RollAt(blastCentre_,blastNormal_,blastForward_,-38*Radians*Ramp(u,0,1)));
        const auto shift=Add(Mul(blastVelocity_,age),Vector3{0,-0.5f*ReboundGravity*age*age,0});
        WreckPose out;out.world=Multiply(world,MakeTranslateMatrix(shift));out.bouncing=true;out.contact=blastContact_;
        out.delta=Multiply(initialBodyInverse_,out.world);return out;
    }
    float HopLiftAt(float time) const {
        if(!hasBlast_) return 0;
        const float age=time-(DetachBegin+impactFlight_+CornerFallDuration);
        if(age<=0) return 0;
        const float air=hasRecontact_?(std::min)(age,hopContactAge_):age;
        const float height=(std::max)(0.0f,std::sqrt(2*ReboundGravity*ReboundHeight)*air-0.5f*ReboundGravity*air*air);
        return hasRecontact_ && age>hopContactAge_?height*(1-rail_game_over_detail::Ramp(age-hopContactAge_,0,0.18f)):height;
    }
    float SlideDistance(float age) const {
        const float t=(std::clamp)(age,0.0f,slideDuration_);
        return slideSpeed_*t-0.5f*slideDeceleration_*t*t;
    }
    WreckPose SlidePose(float age) const {
        using namespace rail_game_over_detail;
        const auto row=[&](int i){return Vector3{landingWorld_.m[i][0],landingWorld_.m[i][1],landingWorld_.m[i][2]};};
        const auto rightGoal=Mul(landingNormal_,-1);
        const auto forward=Unit(Add(row(2),Mul(landingNormal_,-Dot(row(2),landingNormal_))),blastForward_);
        const float blend=CameraEase(age,0,0.14f);
        const auto right=Unit(Mix(Unit(row(0)),rightGoal,blend),rightGoal);
        const auto up=Unit(Cross(Mix(Unit(row(2)),forward,blend),right),blastRight_);
        auto world=Basis(Mul(right,std::sqrt(Dot(row(0),row(0)))),Mul(up,std::sqrt(Dot(row(1),row(1)))),
            Mul(Unit(Cross(right,up),forward),std::sqrt(Dot(row(2),row(2)))),{});
        const auto anchor=Add(landingContact_,Mul(slideDirection_,SlideDistance(age)));
        const auto localPoint=Point(landingLocal_,world);
        world.m[3][0]=anchor.x-localPoint.x;world.m[3][1]=anchor.y-localPoint.y;world.m[3][2]=anchor.z-localPoint.z;
        WreckPose out;out.world=world;
        const float depth=Clearance(world);
        if(std::isfinite(depth)) {
            out.world=Multiply(world,MakeTranslateMatrix({0,-depth,0}));Clearance(out.world,&out.contact);out.grounded=true;
        } else out.world=Multiply(world,MakeTranslateMatrix({0,-0.5f*FallGravity*age*age,0}));
        out.delta=Multiply(initialBodyInverse_,out.world);return out;
    }
    void PrepareBlast() {
        using namespace rail_game_over_detail;
        if(!hasImpact_) return;
        const auto side=CornerPose(CornerFallDuration);
        if(!side.grounded) return;
        hasBlast_=true;blastWorld_=side.world;blastCentre_=Origin(side.world);blastContact_=side.contact;
        Clearance(side.world,nullptr,&blastNormal_);
        blastForward_=Unit(Add(releaseForward_,Mul(blastNormal_,-Dot(releaseForward_,blastNormal_))),releaseForward_);
        blastRight_=Unit(Cross(blastNormal_,blastForward_),releaseRight_);
        blastOrigin_=Point(RailVehicleBreakaway::DriveBlastLocal,blastWorld_);
        auto travel=Add(Mul(skidVelocity_,std::exp(-2*CornerFallDuration)),Add(Mul(blastForward_,4.5f),Mul(blastRight_,2.8f)));
        const float horizontal=std::sqrt(travel.x*travel.x+travel.z*travel.z);
        if(horizontal<10) travel=Mul(travel,10/(std::max)(0.01f,horizontal));
        blastVelocity_=Add(travel,Vector3{0,std::sqrt(2*ReboundGravity*ReboundHeight),0});
        bool lifted=false;float previous=0;
        for(int tick=1;tick<=3*240;++tick) {
            const float age=float(tick)/240,gap=Clearance(BlastAir(age).world);
            if(gap>0.002f) lifted=true;
            if(lifted && gap<=0) {
                float lo=previous,hi=age;
                for(int i=0;i<16;++i) {const float mid=(lo+hi)*0.5f;if(Clearance(BlastAir(mid).world)>0) lo=mid;else hi=mid;}
                hopContactAge_=hi;hasRecontact_=true;
                landingWorld_=BlastAir(hi).world;
                landingWorld_=Multiply(landingWorld_,MakeTranslateMatrix({0,-Clearance(landingWorld_),0}));
                Clearance(landingWorld_,&landingContact_,&landingNormal_,&landingLocal_);
                auto velocity=Add(blastVelocity_,Vector3{0,-ReboundGravity*hi,0});
                velocity=Add(velocity,Mul(landingNormal_,-Dot(velocity,landingNormal_)));
                const float speed=std::sqrt(velocity.x*velocity.x+velocity.z*velocity.z);
                slideDirection_=speed>0.01f?Mul(velocity,1/speed):blastForward_;
                slideSpeed_=(std::clamp)(speed*0.90f,7.5f,12.0f);
                slideDeceleration_=slideSpeed_*slideSpeed_/(2*2.5f);slideDuration_=slideSpeed_/slideDeceleration_;
                break;
            }
            previous=age;
        }
    }
    WreckPose Pose(float t,const RailPath& path) const {
        if(t<DetachBegin) return RailPose(t,path);
        const float flight=t-DetachBegin;
        if(!hasImpact_ || flight<impactFlight_) return AirPose(flight);
        const float age=flight-impactFlight_;
        if(age<CornerFallDuration || !hasBlast_) return CornerPose(age);
        const float hop=age-CornerFallDuration;
        if(!hasRecontact_ || hop<hopContactAge_) return BlastAir(hop);
        return SlidePose(hop-hopContactAge_);
    }
    Matrix4x4 HingedPanel(float t,const RailPath& path) const {
        using namespace rail_game_over_detail;
        const auto body=Pose(t,path).world;
        const float u=(std::clamp)((t-PanelPeelBegin)/(PanelDetachTime-PanelPeelBegin),0.0f,1.0f);
        const float angle=32*Radians*u*u*(3-2*u);
        const Vector3 hinge{RailVehicleBreakaway::PanelCentre.x,RailVehicleBreakaway::PanelCentre.y,RailVehicleBreakaway::PanelMinimum.z};
        const auto pivot=Point(hinge,body);
        const Vector3 axis{body.m[1][0],body.m[1][1],body.m[1][2]},hint{body.m[2][0],body.m[2][1],body.m[2][2]};
        return Multiply(body,RollAt(pivot,axis,hint,angle));
    }
    struct PartMotion {
        Matrix4x4 world=MakeIdentity4x4();
        Vector3 localCentre{},centre{},velocity{},axis{0,0,1},normal{0,1,0},skid{};
        Vector3 minimum{RailVehicleBreakaway::TrimMinimum.x,RailVehicleBreakaway::PanelMinimum.y,RailVehicleBreakaway::TrimMinimum.z};
        Vector3 maximum=RailVehicleBreakaway::TrimMaximum;
        float spin=0,contactAge=-1;
        bool wheel=false;
    };
    Matrix4x4 PartAir(const PartMotion& part,float age) const {
        using namespace rail_game_over_detail;
        auto world=Multiply(part.world,RollAt(part.centre,part.axis,releaseUp_,part.spin*age));
        return Multiply(world,MakeTranslateMatrix(Add(Mul(part.velocity,age),Vector3{0,-0.5f*FallGravity*age*age,0})));
    }
    float PartClearance(const PartMotion& part,const Matrix4x4& world,Vector3* normal=nullptr) const {
        using namespace rail_game_over_detail;
        float minimum=std::numeric_limits<float>::infinity();
        if(!groundQuery_) return minimum;
        // Probe below the top of the whole rotating piece. A centre-only
        // ceiling loses uphill ground under a long or tilted fragment.
        const auto top=[&](const Matrix4x4& m) {
            const Vector3 half=part.wheel?Vector3{0.5f,0.5f,0.5f}:Mul(Add(part.maximum,Mul(part.minimum,-1)),0.5f);
            return Point(part.localCentre,m).y+half.x*std::abs(m.m[0][1])+half.y*std::abs(m.m[1][1])+half.z*std::abs(m.m[2][1]);
        };
        const float ceiling=(std::max)(top(part.world),top(world))+0.1f;
        const auto sample=[&](Vector3 local) {
            const auto p=Point(local,world);TerrainGroundSurfaceHit hit;
            if(groundQuery_(p,ceiling,hit) && std::isfinite(hit.position.y) && p.y-hit.position.y<minimum) {
                minimum=p.y-hit.position.y;if(normal) *normal=hit.normal;
            }
        };
        if(part.wheel) {
            for(float x:{-0.5f,0.5f}) for(int i=0;i<16;++i) {
                const float a=float(i)*6.28318530718f/16;
                sample({x,0.5f*std::cos(a),0.5f*std::sin(a)});
            }
        } else {
            for(float x:{part.minimum.x,part.maximum.x})
                for(float y:{part.minimum.y,part.maximum.y})
                    for(float z:{part.minimum.z,part.maximum.z}) sample({x,y,z});
        }
        return minimum;
    }
    Matrix4x4 PartPose(const PartMotion& part,float age) const {
        using namespace rail_game_over_detail;
        if(part.contactAge<0 || age<part.contactAge) return PartAir(part,age);
        const float skid=age-part.contactAge;
        auto world=PartAir(part,part.contactAge);
        const auto centre=Point(part.localCentre,world);
        world=Multiply(world,RollAt(centre,part.axis,releaseUp_,part.spin*0.12f*(1-std::exp(-skid/0.12f))));
        world=Multiply(world,MakeTranslateMatrix(Mul(part.skid,(1-std::exp(-7*skid))/7)));
        const float depth=PartClearance(part,world);
        if(!std::isfinite(depth)) return Multiply(world,MakeTranslateMatrix({0,-0.5f*FallGravity*skid*skid,0}));
        const float u=(std::clamp)(skid/0.20f,0.0f,1.0f);
        return Multiply(world,MakeTranslateMatrix(Add(Vector3{0,-depth,0},Mul(part.normal,4*0.12f*u*(1-u)))));
    }
    Matrix4x4 RigidWheel(float t,size_t index,const RailPath& path) const {
        auto world=AttachedWheel((std::min)(t,DetachBegin),index,path).worldMatrix;
        if(t<DetachBegin) return world;
        auto inverse=releaseDelta_;inverse=Inverse(inverse);
        return Multiply(world,Multiply(inverse,Pose(t,path).delta));
    }
    // Cache deterministic mesh contacts once, away from the event frame.
    void ResolvePartContact(PartMotion& part,float horizon=4.0f,float frequency=240.0f,bool departing=false) {
        using namespace rail_game_over_detail;
        if(!groundQuery_) return;
        float previous=0;
        for(int step=departing?1:0;step<=int(horizon*frequency);++step) {
            const float a=float(step)/frequency;
            if(PartClearance(part,PartAir(part,a))<=0) {
                float lo=previous,hi=a;
                for(int i=0;i<16;++i) {const float mid=(lo+hi)*0.5f;if(PartClearance(part,PartAir(part,mid))>0) lo=mid;else hi=mid;}
                part.contactAge=hi;
                PartClearance(part,PartAir(part,hi),&part.normal);
                const auto velocity=Add(part.velocity,Vector3{0,-FallGravity*hi,0});
                part.skid=Mul(Add(velocity,Mul(part.normal,-Dot(velocity,part.normal))),part.wheel?0.50f:0.30f);
                break;
            }
            previous=a;
        }
    }
    void PrepareFinale(const RailPath& path) {
        using namespace rail_game_over_detail;
        if(ShatterTime()<0) return;
        // The landing itself ruptures the cart; there is no stopped hold
        // between the bounce, the impact flash and the independent pieces.
        const auto landing=Pose(ShatterTime(),path);
        finalOrigin_=Add(Point({0,0.5f,0},landing.world),Vector3{0,0.2f,0});
        finalContact_=landing.contact;
        constexpr size_t wheelSlots[]{0,1,3};
        for(size_t i=0;i<finalParts_.size();++i) {
            auto& part=finalParts_[i];part.wheel=i>=RailVehicleBreakaway::FractureParts.size();
            if(part.wheel) {
                part.world=RigidWheel(ShatterTime(),wheelSlots[i-RailVehicleBreakaway::FractureParts.size()],path);
            } else {
                const auto& shape=RailVehicleBreakaway::FractureParts[i];
                part.minimum=shape.minimum;part.maximum=shape.maximum;
                part.localCentre=Mul(Add(shape.minimum,shape.maximum),0.5f);part.world=landing.world;
            }
            part.centre=Point(part.localCentre,part.world);
            // Side housings extend beyond the old hull's contact envelope.
            // Resolve existing overlap before launching independent rigid parts,
            // instead of treating their outward launch as a fresh ground hit.
            const float depth=PartClearance(part,part.world);
            if(std::isfinite(depth) && depth<0) {
                part.world=Multiply(part.world,MakeTranslateMatrix({0,-depth,0}));
                part.centre=Point(part.localCentre,part.world);
            }
            auto radial=Add(part.centre,Mul(finalOrigin_,-1));
            radial=Add(radial,Mul(landingNormal_,-Dot(radial,landingNormal_)));
            const float phase=float(i)*2.399963f;
            radial=Unit(radial,Add(Mul(blastRight_,std::sin(phase)),Mul(blastForward_,std::cos(phase))));
            // Heavy, recognizable pieces open out in every direction. Spin is
            // mostly around the floor normal so a large panel can lift first.
            part.velocity=Add(Mul(radial,part.wheel?9.0f:5.8f+float(i%3)*1.1f),
                Add(Mul(landingNormal_,7.2f+float(i%3)*0.6f),Mul(slideDirection_,slideSpeed_*0.70f)));
            part.axis=Unit(Add(Mul(landingNormal_,0.85f),Mul(radial,0.28f)));
            part.spin=part.wheel?9.0f:1.8f+float(i%3)*0.35f;
            ResolvePartContact(part,1.50f,120.0f,true);
        }
    }
    void PrepareParts(const RailPath& path) {
        using namespace rail_game_over_detail;
        panelSupported_=initialVehicle_.meshId==RailVehicleBreakaway::IntactMesh ||
            initialVehicle_.fallbackMeshId==RailVehicleBreakaway::IntactMesh;
        const auto prepare=[&](PartMotion& part,float time,bool wheel) {
            part.wheel=wheel;
            part.localCentre=wheel?Vector3{}:RailVehicleBreakaway::PanelCentre;
            const auto source=[&](float t) {return wheel?RigidWheel(t,RailVehicleBreakaway::WheelIndex,path):HingedPanel(t,path);};
            part.world=source(time);part.centre=Point(part.localCentre,part.world);
            const auto before=Point(part.localCentre,source(time-0.001f)),after=Point(part.localCentre,source(time+0.001f));
            part.velocity=Mul(Add(after,Mul(before,-1)),500);
            const auto bodyBefore=Origin(Pose(time-0.001f,path).world),bodyAfter=Origin(Pose(time+0.001f,path).world);
            const auto bodyVelocity=Mul(Add(bodyAfter,Mul(bodyBefore,-1)),500);
            const auto side=Mul(Vector3{part.world.m[0][0],0,part.world.m[0][2]},-1);
            const auto outward=Unit(side,Mul(releaseRight_,-1));
            if(wheel) {
                // Keep the cart's travel, discard the rising wheel's launch
                // velocity: heavy iron leaves along a low outward arc.
                part.velocity=Add(bodyVelocity,Add(Mul(outward,3.0f),Mul(releaseForward_,1.2f)));
                part.velocity.y=(std::min)(0.45f,bodyVelocity.y+0.20f);
            } else {
                part.velocity=Add(part.velocity,Add(Mul(outward,0.70f),Mul(releaseForward_,-0.30f)));
                part.velocity.y=(std::min)(part.velocity.y,bodyVelocity.y+0.60f);
            }
            part.axis=Unit(wheel?Add(releaseRight_,Mul(releaseAxis_,0.45f)):
                Add(releaseAxis_,Add(Mul(releaseRight_,0.35f),Mul(Vector3{0,1,0},0.55f))));
            part.spin=wheel?9.0f:2.8f;
            ResolvePartContact(part);
        };
        if(initialWheels_.valid) prepare(wheelPart_,WheelDetachTime,true);
        if(panelSupported_) prepare(panelPart_,PanelDetachTime,false);
    }
    void Evaluate(const RailPath& path) {
        using namespace rail_game_over_detail;
        const float t=frame_.elapsed;
        const float delay=FadeStartTime()-FadeBegin;
        frame_.blackout=Ramp(t,frame_.wreck?FadeStartTime():0.15f,frame_.wreck?FadeEndTime():0.75f);
        const float title=frame_.wreck?TitleBegin+delay:0.9f,menu=frame_.wreck?MenuBegin+delay:1.3f;
        frame_.titleOpacity=Ramp(t,title,title+0.3f);frame_.menuOpacity=Ramp(t,menu,menu+0.25f);
        frame_.menuReady=t>=menu+0.25f;
        const float impactTime=DetachBegin+impactFlight_;
        frame_.speedRatio=RailSpeedRatio((std::min)(t,DetachBegin));
        if(hasImpact_ && t>=impactTime) frame_.speedRatio*=GroundRetention*SkidAt((std::min)(t-impactTime,CornerFallDuration)).speed;
        if(hasBlast_ && t>=impactTime+CornerFallDuration) {
            float velocity=std::sqrt(blastVelocity_.x*blastVelocity_.x+blastVelocity_.z*blastVelocity_.z);
            if(hasRecontact_ && t>=RecontactTime()) velocity=(std::max)(0.0f,slideSpeed_-slideDeceleration_*(t-RecontactTime()));
            frame_.speedRatio=(std::clamp)(velocity/(std::max)(1.0f,speed_),0.0f,1.0f);
        }
        frame_.groundContactTime=hasImpact_?impactTime:-1.0f;
        frame_.secondaryContactTime=hasBlast_?impactTime+CornerFallDuration:-1.0f;
        frame_.recontactTime=RecontactTime();frame_.stopTime=StopTime();
        frame_.shatterTime=ShatterTime();frame_.shattered=frame_.shatterTime>=0 && t>=frame_.shatterTime;frame_.fractureCount=0;
        frame_.slideDistance=hasRecontact_ && t>=RecontactTime()?SlideDistance(t-RecontactTime()):0;
        frame_.blastOrigin=blastOrigin_;
        frame_.cornerContact=hasImpact_ && t>=impactTime && t<impactTime+CornerFallDuration;
        frame_.trackDistance=distance_+Travel((std::min)(t,DetachBegin));
        frame_.smokeCount=frame_.sparkCount=frame_.dustCount=frame_.debrisCount=frame_.fireCount=0;frame_.blastLight=0;frame_.shock={};
        frame_.vehicle=initialVehicle_;frame_.wheels=initialWheels_;frame_.occupant=initialOccupant_;
        frame_.cameraEye=initialEye_;frame_.cameraTarget=initialTarget_;frame_.cameraUp=initialUp_;
        frame_.cameraFovY=initialFovY_;frame_.faultBurstCount=0;
        if(!frame_.wreck) return;
        for(const auto& burst:FaultBursts) if(t>=burst.time) ++frame_.faultBurstCount;
        // Cap physical animation once covered; menu idling cannot grow a fall
        // indefinitely or keep emitting hidden particles.
        const float physical=(std::min)(t,FadeEndTime());
        const auto body=Pose(physical,path);
        const auto& delta=body.delta;
        frame_.vehicle.worldMatrix=body.world;frame_.vehicle.visible=initialVehicle_.visible && t<FadeEndTime();
        frame_.vehicle.wheelRotationRadians=initialVehicle_.wheelRotationRadians+Travel((std::min)(physical,DetachBegin))/0.62f;
        frame_.grounded=body.grounded;frame_.bouncing=body.bouncing;
        frame_.groundContactPoint=body.contact;
        auto releaseInverse=releaseDelta_;releaseInverse=Inverse(releaseInverse);
        const auto freeDelta=Multiply(releaseInverse,delta);
        for(size_t i=0;i<frame_.wheels.wheels.size();++i) {
            auto& wheel=frame_.wheels.wheels[i];
            wheel=AttachedWheel((std::min)(physical,DetachBegin),i,path);
            if(physical>=DetachBegin) {
                wheel.worldMatrix=Multiply(wheel.worldMatrix,freeDelta);
                wheel.axleCenter=Point(wheel.axleCenter,freeDelta);wheel.railContact=Point(wheel.railContact,freeDelta);
            }
            wheel.supported=physical<(i%2?DetachBegin:WheelLiftBegin);wheel.visible=frame_.vehicle.visible;
        }
        frame_.wheelDetached=initialWheels_.valid && physical>=WheelDetachTime;
        if(frame_.wheelDetached) {
            auto& wheel=frame_.wheels.wheels[RailVehicleBreakaway::WheelIndex];
            wheel.worldMatrix=PartPose(wheelPart_,physical-WheelDetachTime);
            wheel.axleCenter=Origin(wheel.worldMatrix);wheel.railContact=wheel.axleCenter;wheel.supported=false;
        }
        frame_.sidePanelDetached=panelSupported_ && physical>=PanelDetachTime;
        frame_.sidePanelPeeling=panelSupported_ && physical>=PanelPeelBegin && physical<PanelDetachTime;
        frame_.sidePanel={};
        if(frame_.sidePanelDetached || frame_.sidePanelPeeling) {
            frame_.vehicle.meshId=RailVehicleBreakaway::DamagedMesh;
            frame_.vehicle.fallbackMeshId=RailVehicleBreakaway::DamagedMesh;
            frame_.sidePanel=frame_.vehicle;frame_.sidePanel.meshId=RailVehicleBreakaway::PanelMesh;
            frame_.sidePanel.fallbackMeshId=RailVehicleBreakaway::PanelMesh;
            frame_.sidePanel.worldMatrix=frame_.sidePanelDetached?PartPose(panelPart_,physical-PanelDetachTime):HingedPanel(physical,path);
        }
        frame_.occupant.worldMatrix=Multiply(frame_.occupant.worldMatrix,delta);
        frame_.occupant.position=Point(frame_.occupant.position,delta);
        frame_.occupant.visible=initialOccupant_.visible && frame_.vehicle.visible;
        if(frame_.shattered) {
            frame_.vehicle.visible=false;frame_.occupant.visible=false;
            for(size_t i=0;i<frame_.wheels.wheels.size();++i)
                if(i!=RailVehicleBreakaway::WheelIndex) frame_.wheels.wheels[i].visible=false;
            frame_.wheels.visibleWheelCount=frame_.wheels.wheels[RailVehicleBreakaway::WheelIndex].visible?1:0;
            constexpr size_t wheelSlots[]{0,1,3};
            for(size_t i=0;t<FadeEndTime() && i<finalParts_.size();++i) {
                auto& part=frame_.fractures[frame_.fractureCount++];part=initialVehicle_;
                part.meshId=i<RailVehicleBreakaway::FractureParts.size()?RailVehicleBreakaway::FractureParts[i].mesh:
                    initialWheels_.wheels[wheelSlots[i-RailVehicleBreakaway::FractureParts.size()]].meshId;
                part.fallbackMeshId=part.meshId;
                part.worldMatrix=PartPose(finalParts_[i],physical-frame_.shatterTime);part.visible=initialVehicle_.visible;
            }
        }
        // Keep the gameplay rig moving with the rail while failure begins.
        // Stay behind the running rail. Only pull back/up and tilt toward the
        // cart; a side orbit makes its connection to the track hard to judge.
        const float cameraTime=(std::min)(physical,DetachBegin);
        const auto cameraTransport=Transport(cameraTime,path);
        const auto anchor=path.EvaluateExtended(distance_+Travel(cameraTime));
        const auto centre=Point(rail_game_over_detail::Origin(initialVehicle_.worldMatrix),cameraTransport);
        const auto target=Add(centre,Mul(anchor.up,0.65f));
        const auto revealEye=Add(target,Add(Mul(anchor.tangent,-rearCameraBack_),Mul(anchor.up,rearCameraHeight_)));
        const auto gameplayEye=Point(initialEye_,cameraTransport);
        const auto gameplayTarget=Point(initialTarget_,cameraTransport);
        const auto gameplayUp=Unit(Add(Point(Add(initialEye_,initialUp_),cameraTransport),Mul(gameplayEye,-1)),initialUp_);
        frame_.cameraBlend=CameraEase(t,CameraMoveBegin,CameraMoveEnd);
        frame_.cameraEye=Mix(gameplayEye,revealEye,frame_.cameraBlend);
        frame_.cameraTarget=Mix(gameplayTarget,target,frame_.cameraBlend);
        frame_.cameraUp=Unit(Mix(gameplayUp,{0,1,0},frame_.cameraBlend),{0,1,0});
        frame_.cameraFovY=initialFovY_;
        if(physical>DetachBegin) {
            const float flight=physical-DetachBegin;
            const auto follow=[&](float response) {
                const float coast=response*(1-std::exp(-flight/response));
                const auto lagged=Pose(DetachBegin+flight-coast,path);
                return Add(Add(Origin(lagged.world),Mul(releaseCentre_,-1)),
                    Mul(releaseForward_,speed_*RailSpeedRatio(DetachBegin)*coast));
            };
            // A closed-form lag preserves outgoing rail velocity at release.
            // The eye keeps coasting after impact while the gaze catches up.
            const auto eyeShift=follow(0.26f);
            frame_.cameraEye=Add(frame_.cameraEye,{eyeShift.x,eyeShift.y*0.45f,eyeShift.z});
            frame_.cameraTarget=Add(frame_.cameraTarget,follow(0.08f));
            const float gazeCoast=0.08f*(1-std::exp(-flight/0.08f));
            frame_.cameraTarget.y-=0.60f*HopLiftAt(DetachBegin+flight-gazeCoast);
        }
        const float impactAge=t-impactTime,sideTime=impactTime+CornerFallDuration,sideAge=t-sideTime;
        const auto impulse=[&](float age,float strength) {
            if(!hasImpact_ || age<0 || age>=0.25f) return;
            const float envelope=1-Ramp(age,0,0.25f);
            const auto shake=Add(Mul(Vector3{0,1,0},std::sin(age*67)*strength*envelope),
                Mul(releaseRight_,std::sin(age*47)*strength*0.48f*envelope));
            frame_.cameraEye=Add(frame_.cameraEye,shake);frame_.cameraTarget=Add(frame_.cameraTarget,shake);
        };
        impulse(impactAge,0.10f);if(hasBlast_) impulse(sideAge,0.24f);
        if(hasRecontact_ && frame_.shatterTime<0) impulse(t-RecontactTime(),0.36f);
        if(frame_.shattered) impulse(t-frame_.shatterTime,0.55f);
        frame_.hudOpacity=1-Ramp(t,CameraMoveBegin,0.95f);
        frame_.impactFlash=0.16f*(1-Ramp(t,0.025f,0.18f));
        if(hasImpact_ && impactAge>=0) frame_.impactFlash=(std::max)(frame_.impactFlash,0.30f*(1-Ramp(impactAge,0.008f,0.07f)));
        if(hasBlast_ && sideAge>=0) frame_.impactFlash=(std::max)(frame_.impactFlash,0.24f*(1-Ramp(sideAge,0.008f,0.06f)));
        if(frame_.shattered) frame_.impactFlash=(std::max)(frame_.impactFlash,0.95f*(1-Ramp(t-frame_.shatterTime,0.012f,0.09f)));
        if(t>=FadeEndTime()) return;
        // Thin soot from a single damaged housing. Samples stay in world
        // space after emission and stay clear of both wheel pairs.
        for(uint32_t i=0;i<64;++i) {
            const float emitted=0.05f+float(i)*0.055f,age=t-emitted;
            if(age<0 || age>1.10f || emitted>(hasImpact_?impactTime+0.15f:DetachBegin+0.30f)) continue;
            const auto emission=Pose(emitted,path);const float phase=float(i)*2.399963f;
            const auto sample=path.EvaluateExtended(distance_+Travel((std::min)(emitted,DetachBegin)));
            const auto root=Point({-2.0f,0.65f,-0.35f},emission.world);
            const auto drift=Add(Mul(sample.right,std::sin(phase)*0.16f*age),
                Add(Mul(Vector3{0,1,0},age*1.65f),Mul(sample.tangent,-age*0.35f)));
            frame_.smoke[frame_.smokeCount++]={Add(root,drift),0.16f+age*0.65f,
                Ramp(age,0,0.06f)*(1-Ramp(age,0.65f,1.10f))*0.52f,i%12};
        }
        // Contact sparks stop on the lifted side; the loaded rail keeps
        // scraping until release. This makes the pivot visible through motion.
        for(uint32_t i=0;i<284;++i) {
            const float emitted=0.035f+float(i/4)*0.031f,age=t-emitted;
            if(age<0 || age>0.30f || emitted>DetachBegin) continue;
            const uint32_t wheel=i%4;
            if(wheel%2==0 && emitted>WheelLiftBegin) continue;
            const auto root=RailAnchorAt(emitted,wheel,path).point;
            const auto sample=path.EvaluateExtended(distance_+Travel(emitted));
            const float phase=float(i)*2.399963f;
            const auto velocity=Add(Mul(sample.tangent,-6.0f-1.5f*float(i%3)),
                Add(Mul(sample.right,std::sin(phase)*2.2f),Mul(sample.up,0.75f+0.20f*float(i%3))));
            const auto at=[&](float a){return Add(root,Add(Mul(velocity,a),Mul(sample.up,-5*a*a)));};
            frame_.sparks[frame_.sparkCount++]={at(age),at((std::max)(0.0f,age-0.11f)),1-Ramp(age,0.10f,0.30f)};
        }
        FailureFx(t,path);
        const auto emitSpark=[&](float emitted,uint32_t seed,bool burst,bool mainHit) {
            const float age=t-emitted;if(age<0 || age>0.28f || frame_.sparkCount>=frame_.sparks.size()) return;
            const auto emission=Pose(emitted,path);
            if(!burst && !emission.grounded) return;
            const auto root=mainHit && burst?Pose(sideTime,path).contact:emission.contact;
            const float phase=float(seed)*2.399963f;
            const auto velocity=Add(Mul(releaseForward_,-(mainHit?7.0f:3.0f)-float(seed%4)),
                Add(Mul(releaseRight_,std::sin(phase)*(mainHit?5.0f:2.0f)),Mul(impactNormal_,0.8f+0.3f*float(seed%5))));
            const auto at=[&](float a){return Add(root,Add(Mul(velocity,a),Vector3{0,-8*a*a,0}));};
            frame_.sparks[frame_.sparkCount++]={at(age),at((std::max)(0.0f,age-0.095f)),1-Ramp(age,0.08f,0.28f)};
        };
        if(hasImpact_) {
            ContactDebris(t,impactTime,impactContact_,false,true);
            for(uint32_t i=0;i<48;++i) emitSpark(impactTime+float(i/24)*0.008f,i,true,false);
            if(hasBlast_) {
                for(uint32_t i=0;i<100;++i) emitSpark(sideTime+float(i/25)*0.008f,100+i,true,true);
                ContactDebris(t,sideTime,blastContact_);
                BurstFx(t-sideTime,blastOrigin_,blastContact_,false);
            }
            if(hasRecontact_) {
                for(uint32_t i=0;i<32;++i) emitSpark(RecontactTime()+float(i/16)*0.008f,500+i,true,false);
                for(uint32_t i=0;i<48;++i) emitSpark(RecontactTime()+0.02f+float(i/2)*0.025f,300+i,false,false);
                ContactDebris(t,RecontactTime(),landingContact_,true);
            }
            // The ground wake follows the actual skid after the small hop.
            // Its emission points stay on the mesh instead of floating with the cart.
            for(uint32_t i=0;hasRecontact_ && i<8 && frame_.dustCount<frame_.dust.size();++i) {
                const float emitted=RecontactTime()+0.02f+float(i)*0.08f,age=t-emitted;
                if(age<0 || age>0.65f || emitted>StopTime()) continue;
                const auto pose=Pose(emitted,path);if(!pose.grounded) continue;
                const auto root=Add(pose.contact,Mul(impactNormal_,0.10f));
                const auto drift=Add(Mul(Unit(skidVelocity_,releaseForward_),-0.65f*age),Mul(impactNormal_,age*0.55f));
                const float strength=(std::clamp)((1-(emitted-RecontactTime())/(std::max)(0.01f,slideDuration_))*1.2f,0.1f,1.0f);
                frame_.dust[frame_.dustCount++]={Add(root,drift),0.14f+age*0.8f,
                    Ramp(age,0,0.025f)*(1-Ramp(age,0.20f,0.65f))*0.45f*strength,i%12};
            }
        }
        if(frame_.shattered) BurstFx(t-frame_.shatterTime,finalOrigin_,finalContact_,true);
    }
    Vector3 FaultRoot(float time,float side,const RailPath& path) const {
        using namespace rail_game_over_detail;
        const size_t wheel=side<0?0:1;
        const auto anchor=RailAnchorAt(time,wheel,path);
        return Add(AttachedWheel(time,wheel,path).axleCenter,
            Add(Mul(anchor.right,side*0.30f),Mul(anchor.up,0.12f)));
    }
    void FailureFx(float t,const RailPath& path) {
        using namespace rail_game_over_detail;
        for(size_t index=0;index<FaultBursts.size();++index) {
            const auto& burst=FaultBursts[index];const float age=t-burst.time;
            if(age<0 || age>0.48f) continue;
            const auto anchor=RailAnchorAt(burst.time,burst.side<0?0:1,path);
            const auto outward=Mul(anchor.right,burst.side);
            // The brief flame follows the failing wheel mount. Ejected sparks
            // and soot keep their world-space emission positions behind it.
            if(age<0.13f) {
                const auto root=FaultRoot(t,burst.side,path);
                for(uint32_t i=0;i<2 && frame_.fireCount<frame_.fire.size();++i) {
                    const float a=age-float(i)*0.012f,life=i?0.105f:0.13f;
                    if(a<0 || a>=life) continue;
                    const auto offset=Add(Mul(outward,float(i)*0.13f),Mul(anchor.up,float(i)*0.09f));
                    frame_.fire[frame_.fireCount++]={Add(root,offset),0.14f+(i?0.36f*burst.strength:0.38f+0.24f*burst.strength)*Ramp(a,0,0.025f),
                        (i?0.62f:0.88f)*(1-Ramp(a,0.035f,life)),a/life,float(index)*2.399963f,0.72f+0.12f*float(i),uint32_t(index+i)%4,true};
                }
                frame_.blastOrigin=root;
                frame_.blastLight=(std::max)(frame_.blastLight,0.22f*burst.strength*(1-Ramp(age,0.02f,0.13f)));
            }
            const std::array<Vector3,3> sparkRoots{FaultRoot(burst.time,burst.side,path),
                FaultRoot(burst.time+0.006f,burst.side,path),FaultRoot(burst.time+0.012f,burst.side,path)};
            for(uint32_t i=0;i<24+uint32_t(index)*2 && frame_.sparkCount<frame_.sparks.size();++i) {
                const uint32_t group=i%3;const float a=age-float(group)*0.006f;
                if(a<0 || a>=0.22f) continue;
                const float phase=float(i+uint32_t(index)*37)*2.399963f;
                const auto velocity=Add(Mul(outward,3.0f+float(i%4)*0.8f),
                    Add(Mul(anchor.forward,-2.5f-float(i%3)),Mul(anchor.up,1.0f+float(i%4)*0.5f)));
                const auto at=[&](float v){return Add(sparkRoots[group],Add(Mul(velocity,v),Vector3{0,-12*v*v,0}));};
                frame_.sparks[frame_.sparkCount++]={at(a),at((std::max)(0.0f,a-0.055f)),burst.strength*(1-Ramp(a,0.06f,0.22f))};
            }
            for(uint32_t i=0;i<3 && frame_.smokeCount<frame_.smoke.size();++i) {
                const float birth=0.02f+float(i)*0.018f,a=age-birth;
                if(a<0 || a>=0.40f) continue;
                const auto root=FaultRoot(burst.time+birth,burst.side,path);
                const auto drift=Add(Mul(outward,a*0.40f),Add(Mul(anchor.forward,-a*0.30f),Vector3{0,a*1.20f,0}));
                frame_.smoke[frame_.smokeCount++]={Add(root,drift),0.10f+a*0.50f,
                    0.34f*Ramp(a,0,0.03f)*(1-Ramp(a,0.16f,0.40f)),uint32_t(index*3+i)%12};
            }
        }
    }
    void BurstFx(float age,Vector3 origin,Vector3 contact,bool finale) {
        using namespace rail_game_over_detail;
        if(age<0) return;
        if(finale) frame_.blastOrigin=origin;
        frame_.blastLight=(finale?0.75f:0.18f)*(1-Ramp(age,0.02f,finale?0.38f:0.18f));
        if(finale && age<0.26f) frame_.shock={Add(contact,Mul(blastNormal_,0.08f)),blastRight_,blastForward_,finale?8.0f:6.0f,
            (std::clamp)(age/0.26f,0.0f,1.0f),0.65f*(1-Ramp(age,0.10f,0.26f))};
        // The first impact has a brief housing rupture. Only recontact gets
        // the broad fireball, ground wave and full explosion lighting.
        for(uint32_t i=0;i<(finale?8u:3u) && frame_.fireCount<frame_.fire.size();++i) {
            const float birth=i==0?0.0f:(finale?0.008f+0.006f*float(i-1):0.012f+0.008f*float(i-1));
            const float a=age-birth,life=finale?(i==0?0.40f:0.50f):(i==0?0.14f:0.18f);
            if(a<0 || a>life) continue;
            const float phase=float(i)*2.399963f;
            const auto offset=Add(Mul(blastRight_,std::sin(phase)*(i?0.35f:0.0f)),
                Add(Mul(blastForward_,std::cos(phase)*(i?0.35f:0.0f)),Vector3{0,a*(i?2.0f:1.2f),0}));
            frame_.fire[frame_.fireCount++]={Add(origin,offset),(finale?0.80f:0.12f)+(finale?(i?1.78f:2.58f):(i?0.55f:0.78f))*Ramp(a,0,finale?0.05f:0.025f),
                (i?0.90f:1.0f)*(1-Ramp(a,life*0.5f,life)),a/life,phase,0.80f+0.12f*float(i%3),i%4};
        }
        for(uint32_t i=0;i<(finale?22u:6u) && frame_.smokeCount<frame_.smoke.size();++i) {
            const float a=age-(0.045f+float(i)*0.025f),life=finale?1.35f:0.65f;if(a<0 || a>life) continue;
            const float phase=float(i)*2.399963f;
            const auto drift=Add(Mul(blastRight_,std::sin(phase)*a*0.35f),
                Add(Mul(blastForward_,-a*0.5f),Vector3{0,a*2.0f,0}));
            frame_.smoke[frame_.smokeCount++]={Add(origin,drift),(finale?0.28f:0.16f)+a*(finale?1.5f:0.75f),
                Ramp(a,0,0.04f)*(1-Ramp(a,finale?0.75f:0.24f,life))*(finale?0.78f:0.46f),i%12,true};
        }
        for(uint32_t i=0;i<(finale?96u:48u) && frame_.sparkCount<frame_.sparks.size();++i) {
            const float a=age-float(i/24)*0.009f;if(a<0 || a>0.44f) continue;
            const float phase=float(i)*2.399963f;
            const auto v=Add(Mul(blastRight_,std::sin(phase)*(7+1.5f*float(i%3))),
                Add(Mul(blastForward_,std::cos(phase)*8-3),Vector3{0,4.0f+1.3f*float(i%4),0}));
            const auto at=[&](float t){return Add(origin,Add(Mul(v,t),Vector3{0,-14*t*t,0}));};
            frame_.sparks[frame_.sparkCount++]={at(a),at((std::max)(0.0f,a-0.12f)),1-Ramp(a,0.14f,0.44f)};
        }
        for(uint32_t i=0;i<(finale?24u:8u) && age<0.70f && frame_.debrisCount<frame_.debris.size();++i) {
            const float phase=float(i)*2.399963f;
            const auto v=Add(Mul(blastRight_,std::sin(phase)*8),Add(Mul(blastForward_,std::cos(phase)*6),Vector3{0,3.5f+0.5f*float(i%4),0}));
            const auto at=[&](float t){return Add(origin,Add(Mul(v,t),Vector3{0,-14*t*t,0}));};
            const auto centre=at(age);TerrainGroundSurfaceHit ground;
            if(!groundQuery_(centre,centre.y+0.3f,ground) || centre.y<ground.position.y) continue;
            frame_.debris[frame_.debrisCount++]={centre,at((std::max)(0.0f,age-0.025f)),0.060f+0.012f*float(i%3),1-Ramp(age,0.28f,0.70f)};
        }
    }
    void ContactDebris(float t,float impactTime,Vector3 contact,bool landing=false,bool edge=false) {
        using namespace rail_game_over_detail;
        if(!hasImpact_ || t<impactTime) return;
        const float age=t-impactTime;
        for(uint32_t i=0;i<(edge?4u:(landing?8u:16u)) && frame_.dustCount<frame_.dust.size();++i) {
            const float delay=float(i/7)*0.025f,a=age-delay;
            const float life=edge?0.55f:0.90f;if(a<0 || a>life) continue;
            const float phase=float(i)*2.399963f;
            const auto radial=Add(Mul(releaseRight_,std::sin(phase)),Mul(releaseForward_,std::cos(phase)));
            const auto root=Add(contact,Mul(Vector3{0,1,0},0.12f));
            const auto centre=Add(root,Add(Mul(radial,a*(2.5f+0.3f*float(i%4))),Vector3{0,a*(0.9f+0.14f*float(i%3)),0}));
            frame_.dust[frame_.dustCount++]={centre,0.18f+a*(1.4f+0.16f*float(i%3)),
                Ramp(a,0,0.035f)*(1-Ramp(a,edge?0.15f:0.30f,life))*(edge?0.40f:0.58f),i%12};
        }
        for(uint32_t i=0;i<(edge?6u:(landing?12u:20u)) && age<0.65f && frame_.debrisCount<frame_.debris.size();++i) {
            const float phase=float(i)*2.399963f;
            const auto v=Add(Mul(releaseRight_,std::sin(phase)*(3+float(i%3))),
                Add(Mul(releaseForward_,std::cos(phase)*3),Vector3{0,2.2f+0.3f*float(i%4),0}));
            const auto at=[&](float a){return Add(contact,Add(Mul(v,a),Vector3{0,0.12f-6*a*a,0}));};
            const auto centre=at(age);TerrainGroundSurfaceHit floor;
            if(!groundQuery_(centre,centre.y+0.3f,floor) || centre.y<floor.position.y) continue;
            frame_.debris[frame_.debrisCount++]={centre,at((std::max)(0.0f,age-0.025f)),
                0.035f+0.008f*float(i%3),1-Ramp(age,0.25f,0.65f)};
        }
    }
    RailGameOverFrame frame_{};
    PartMotion wheelPart_{},panelPart_{};
    std::array<PartMotion,RailVehicleBreakaway::FinalPieceCount> finalParts_{};
    Vector3 finalOrigin_{},finalContact_{};
    bool panelSupported_=false;
    RailVehicleActorFrame initialActor_{};
    RailVehicleRenderFrame initialVehicle_{};
    RailVehicleWheelContactPresentationFrame initialWheels_{};
    RailVehicleOccupantActorFrame initialOccupant_{};
    Matrix4x4 initialSupportInverse_=MakeIdentity4x4(),initialBodyLocal_=MakeIdentity4x4(),initialBodyInverse_=MakeIdentity4x4();
    const CourseRailTrackMeshBakeResult* track_=nullptr;
    std::array<std::vector<const CourseRailTrackBakedInstance*>,2> rails_{};
    std::array<float,4> contactOffsets_{},contactLanes_{},contactHeights_{};
    std::array<Vector3,4> contactResidual_{};
    float bodyHeight_=1.0f,rearCameraBack_=18.0f,rearCameraHeight_=6.0f;
    std::array<Vector3,4> contacts_{};
    Matrix4x4 releaseDelta_=MakeIdentity4x4(),impactDelta_=MakeIdentity4x4();
    Vector3 releaseCentre_{},releaseAxis_{0,0,1},releaseUp_{0,1,0},releaseForward_{0,0,1},releaseRight_{1,0,0};
    GroundQuery groundQuery_{};
    bool hasImpact_=false,hasBlast_=false,hasRecontact_=false;
    Matrix4x4 blastWorld_=MakeIdentity4x4(),landingWorld_=MakeIdentity4x4();
    Vector3 blastCentre_{},blastContact_{},blastNormal_{0,1,0},blastForward_{0,0,1},blastRight_{1,0,0},blastOrigin_{},blastVelocity_{};
    Vector3 landingContact_{},landingLocal_{},landingNormal_{0,1,0},slideDirection_{};
    float hopContactAge_=0,slideSpeed_=0,slideDuration_=0,slideDeceleration_=1;
    Vector3 releaseVelocity_{},impactCentre_{},impactContact_{},skidVelocity_{},impactNormal_{0,1,0},impactLocalContact_{};
    float angularSpeed_=0,impactFlight_=0;
    Vector3 initialEye_{},initialTarget_{},initialUp_{0,1,0};
    float initialFovY_=0.82f,distance_=0,speed_=0,pointerX_=0,pointerY_=0;
    bool pointerKnown_=false;
};
