#pragma once
#include "RailBridgeEscape.h"
#include <vector>

// Deterministic structural choreography, derived entirely from checkpointed
// escape state. Rigid deck pieces keep their thickness during sag and release.
namespace RailBridgeStructure {
inline constexpr float TearSeconds=.56f;
inline float Smooth(float t){t=(std::clamp)(t,0.0f,1.0f);return t*t*(3-2*t);}
inline float CollapseAge(const RailBridgeEscape& e){return e.Age()-RailBridgeEscape::ImpactSeconds;}
inline float BendAngle(float age) {
    const float impactFlex=.035f*Smooth(age/.055f)*(1.0f-Smooth((age-.055f)/.12f));
    return .24f*Smooth(age/TearSeconds)+impactFlex;
}
inline float JointHeight(float age){return -50*std::sin(BendAngle(age));}
inline float Sag(const RailBridgeEscape& e){return -2.4f*Smooth((CollapseAge(e)-.12f)/.70f);}
inline float RampHeight(const RailBridgeEscape& e,float u) {
    return std::lerp(Sag(e),e.Arc().rise,e.Deploy())*u*u;
}
struct SpanPose {float distance=0,height=0,pitch=0,lateral=0;bool attached=true;};
inline SpanPose Span(const RailBridgeEscape& e,int index) {
    const float t=(std::max)(0.0f,CollapseAge(e));
    const float angle=BendAngle(t);
    const float free=(std::max)(0.0f,t-TearSeconds);
    SpanPose p;
    p.distance=index==0?e.Arc().Launch()+25*std::cos(angle):e.Arc().Landing()-25*std::cos(angle);
    p.distance+=(index==0?-1.0f:1.0f)*free*3;
    p.height=-25*std::sin(angle)-6*free-12*free*free;
    p.pitch=(index==0?1.0f:-1.0f)*(angle+free*.48f);
    p.lateral=(index==0?-1.0f:1.0f)*free*.9f;
    p.attached=t<=TearSeconds;return p;
}
inline CourseTerrainPlacement Piece(const char* id,const char* mesh,float d,float x=0,float y=0) {
    CourseTerrainPlacement p;p.id=id;p.meshId=mesh;p.distance=d;p.lateralOffset=x;p.verticalOffset=y;
    p.cullAheadDistance=1000;p.cullBehindDistance=350;p.renderPriority=23;return p;
}
inline CourseTerrainPlacement TornJoint(const RailBridgeEscape& e,int index,const SpanPose& s) {
    const float sign=index==0?1.0f:-1.0f;
    auto p=Piece(index==0?"escape-near-torn-joint":"escape-far-torn-joint","bridge_escape_torn_joint",
        s.distance+sign*25*std::cos(s.pitch),s.lateral,s.height-25*std::sin(std::abs(s.pitch)));
    p.rotation.x=std::abs(s.pitch);
    p.rotation.y=index==0?0.0f:3.14159265f;
    p.rotation.z=-sign*(std::max)(0.0f,CollapseAge(e)-TearSeconds)*.12f;
    return p;
}
// Endpoints are rail-local (lateral, vertical, distance). Unit cable is along Z.
inline CourseTerrainPlacement Link(const char* id,Vector3 a,Vector3 b,float radius=.10f) {
    auto p=Piece(id,"bridge_escape_link",(a.z+b.z)*.5f,(a.x+b.x)*.5f,(a.y+b.y)*.5f);
    const float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
    const float horizontal=std::hypot(dx,dz),length=std::hypot(horizontal,dy);
    p.rotation={-std::atan2(dy,horizontal),std::atan2(dx,dz),0};p.scale={radius,radius,length};return p;
}
inline void Append(const RailBridgeEscape& e,std::vector<CourseTerrainPlacement>& out) {
    if(!e.GapOpen())return;
    const float start=e.Arc().start,t=CollapseAge(e),deploy=e.Deploy();
    for(int i=0;i<8;++i) {
        const float a=float(i)/8,b=float(i+1)/8;
        const float ya=RampHeight(e,a),yb=RampHeight(e,b);
        auto p=Piece("escape-ramp-leaf","bridge_escape_leaf",start+(a+b)*20,0,(ya+yb)*.5f);
        p.id+="-"+std::to_string(i);p.rotation.x=-std::atan2(yb-ya,5.0f);
        p.scale.z=std::hypot(5.0f,yb-ya)/5;out.push_back(p);
    }
    for(int i=0;i<2;++i) {
        const auto s=Span(e,i);if(s.height<-90)continue;
        auto p=Piece(i==0?"escape-near-span":"escape-far-span","bridge_escape_span",s.distance,s.lateral,s.height);
        p.rotation.x=s.pitch;p.rotation.z=(i==0?-1.0f:1.0f)*(std::max)(0.0f,t-TearSeconds)*.12f;out.push_back(p);
        if(!s.attached)out.push_back(TornJoint(e,i,s));
    }
    if(t<TearSeconds)for(int side:{-1,1}) {
        const auto s=Span(e,0);const float gap=(std::max)(.05f,50*(1-std::cos(s.pitch)));
        auto p=Piece(side<0?"escape-stretch-rail-l":"escape-stretch-rail-r","bridge_escape_link",e.Arc().Launch()+50,float(side)*1.9f,JointHeight(t));
        p.scale={.15f,.13f,gap+1};out.push_back(p);
    }
    const auto pin=e.TargetRail();
    auto post=Piece("escape-clamp-post","bridge_escape_pylon",pin.z,pin.x,0);out.push_back(post);
    const float released=deploy;
    const Vector3 hook{-5.25f,RampHeight(e,.90f)-.7f,start+36};
    auto tether=Link("escape-retaining-tether",pin,hook,.12f);
    if(released>0){tether.lateralOffset-=released*3;tether.verticalOffset-=released*12;tether.rotation.z+=released*1.8f;}
    if(released<.95f)out.push_back(tether);
    for(int side:{-1,1})out.push_back(Link(side<0?"escape-lifting-strut-l":"escape-lifting-strut-r",
        {float(side)*4.7f,-4.5f,start+8},{float(side)*4.7f,RampHeight(e,.85f)-.7f,start+34},.25f));
    auto weight=Piece("escape-counterweight","bridge_escape_debris",start+23,-9,-2-released*8);
    weight.scale={.55f,2,1};out.push_back(weight);
    // Small secondary debris follows the primary tear, away from the pin.
    for(int i=0;i<16;++i) {
        const float f=t-TearSeconds-float(i%4)*.045f;if(f<0 || f>2.6f)continue;
        auto p=Piece("escape-connection-debris","bridge_escape_debris",e.Arc().Launch()+50+float(i%3-1)*f*3,
            float(i%2?1:-1)*(2+f*5),JointHeight(TearSeconds)-7*f-12*f*f);
        p.id+="-"+std::to_string(i);p.scale={.30f+float(i%3)*.14f,.45f,.5f};
        p.rotation={f*.8f,float(i),f*.5f};out.push_back(p);
    }
}
}
