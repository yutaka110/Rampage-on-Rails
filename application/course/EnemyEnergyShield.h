#pragma once
#include "TwinShieldDronePose.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Shared authored contract. No automatic regeneration: breaking the generator
// permanently exposes this actor for the remainder of its lifetime/checkpoint.
struct EnemyEnergyShieldDefinition {
    bool enabled=false;
    float maximumHitPoints=48.0f;
    bool Validate()const {
        return std::isfinite(maximumHitPoints) && maximumHitPoints>0 && maximumHitPoints<=10000;
    }
};
struct EnemyEnergyShieldImpact {Vector2 uv{};float age=10;bool lock=false;};
struct EnemyEnergyShieldState {
    float hitPoints=0,breakAge=10;
    uint64_t lastShotId=0;
    std::array<EnemyEnergyShieldImpact,4> impacts{};
    uint32_t nextImpact=0;
    void Initialize(const EnemyEnergyShieldDefinition& def) {
        *this={};hitPoints=def.enabled?def.maximumHitPoints:0;
    }
    void Update(float dt) {
        if(!std::isfinite(dt) || dt<=0)return;
        breakAge=(std::min)(10.0f,breakAge+dt);
        for(auto& hit:impacts)hit.age=(std::min)(10.0f,hit.age+dt);
    }
    bool Active(const EnemyEnergyShieldDefinition& def)const{return def.enabled && hitPoints>0;}
    bool Validate(const EnemyEnergyShieldDefinition& def)const {
        if(!def.Validate() || !std::isfinite(hitPoints) || hitPoints<0 || hitPoints>def.maximumHitPoints ||
            (!def.enabled && hitPoints!=0) || !std::isfinite(breakAge) || breakAge<0 || breakAge>10 || nextImpact>=impacts.size())return false;
        for(const auto& hit:impacts)if(!std::isfinite(hit.uv.x) || !std::isfinite(hit.uv.y) ||
            std::abs(hit.uv.x)>1.001f || std::abs(hit.uv.y)>1.001f || !std::isfinite(hit.age) || hit.age<0 || hit.age>10)return false;
        return true;
    }
    void Contact(uint64_t shot,Vector2 uv,bool lock,float damage) {
        lastShotId=shot;impacts[nextImpact]={uv,0,lock};nextImpact=(nextImpact+1)%uint32_t(impacts.size());
        const float previous=hitPoints;
        if(!lock)hitPoints=(std::max)(0.0f,hitPoints-damage);
        if(previous>0 && hitPoints==0)breakAge=0;
    }
};

namespace EnemyEnergyShield {
inline constexpr float HalfWidth=1.48f,HalfHeight=1.13f,EdgeZ=-1.02f,CenterZ=-1.19f;
inline constexpr std::array<Vector2,6> Corners{{{-.72f,-1},{.72f,-1},{1,0},{.72f,1},{-.72f,1},{-1,0}}};
inline Vector3 Add(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector3 Sub(Vector3 a,Vector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 Scale(Vector3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vector3 Cross(Vector3 a,Vector3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vector3 Point(const Matrix4x4& m,Vector3 p) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
inline Vector3 Local(Vector2 uv,bool center=false){return {uv.x*HalfWidth,uv.y*HalfHeight,center?CenterZ:EdgeZ};}
struct Contact {Vector3 position{},normal{};Vector2 uv{};float distance=0;bool hit=false;};
// Exact same six shallow facets are submitted to the depth-tested world VFX
// pass. Full 3D pose is retained through roll, banking, slopes and camera cuts.
inline Contact Intersect(const TwinShieldDronePose& pose,Vector3 origin,Vector3 direction,float maximumDistance) {
    Contact result;
    const float length=std::sqrt(Dot(direction,direction));
    if(!pose.visible || !std::isfinite(length) || length<.00001f || maximumDistance<0)return result;
    direction=Scale(direction,1/length);
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    const auto front=Normalize(Vector3{-world.m[2][0],-world.m[2][1],-world.m[2][2]});
    if(Dot(direction,front)>=-.15f)return result;
    const auto center=Point(world,Local({},true));
    float nearest=maximumDistance+.05f;
    for(size_t i=0;i<Corners.size();++i) {
        const auto a=Point(world,Local(Corners[i])),b=Point(world,Local(Corners[(i+1)%Corners.size()]));
        const auto edge1=Sub(a,center),edge2=Sub(b,center),p=Cross(direction,edge2);
        const float determinant=Dot(edge1,p);if(std::abs(determinant)<.000001f)continue;
        const float inverse=1/determinant;const auto t=Sub(origin,center);
        const float u=Dot(t,p)*inverse;if(u<0 || u>1)continue;
        const auto q=Cross(t,edge1);const float v=Dot(direction,q)*inverse;
        if(v<0 || u+v>1)continue;
        const float distance=Dot(edge2,q)*inverse;if(distance<0 || distance>nearest)continue;
        nearest=distance;result.hit=true;result.distance=distance;
        result.position=Add(origin,Scale(direction,distance));result.normal=front;
        result.uv={Corners[i].x*u+Corners[(i+1)%Corners.size()].x*v,
            Corners[i].y*u+Corners[(i+1)%Corners.size()].y*v};
    }
    return result;
}
}
