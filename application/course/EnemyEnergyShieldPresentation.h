#pragma once
#include "EnemyEnergyShield.h"

namespace EnemyEnergyShield {
inline constexpr float GuardCueSeconds=.46f;
inline Vector3 SurfacePoint(Vector2 p) {
    const float edge=(std::max)(std::abs(p.y),std::abs(p.x)+.28f*std::abs(p.y));
    return {p.x*HalfWidth,p.y*HalfHeight,CenterZ+(EdgeZ-CenterZ)*edge-.008f};
}
// Alpha-blended inner rim anchors the luminous contour on pale hull/sky.
// Uses the same facets and read-only scene depth as the additive membrane.
template<class Emit> void DrawBacking(const TwinShieldDronePose& pose,const EnemyEnergyShieldState& state,
    const EnemyEnergyShieldDefinition& definition,float alpha,Emit emit) {
    if(!pose.visible || !state.Active(definition) || alpha<=.001f)return;
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    const auto vertex=[&](Vector2 p,bool center) {
        emit(Point(world,Local(p,center)),Vector2{3000+(p.x+1)*.5f,(p.y+1)*.5f},Vector4{.008f,.035f,.055f,alpha});
    };
    for(size_t i=0;i<Corners.size();++i) {vertex({},true);vertex(Corners[i],false);vertex(Corners[(i+1)%Corners.size()],false);}
}
// Constant bounded geometry: 6 facets, 4 recent impacts and 2 emitter windows.
// Independent of screen size, wall-clock time and bloom settings.
template<class Emit> void Draw(const TwinShieldDronePose& pose,const EnemyEnergyShieldState& state,
    const EnemyEnergyShieldDefinition& definition,float age,float alpha,Emit emit) {
    if(!definition.enabled || !pose.visible || alpha<=.001f ||
        (!state.Active(definition) && state.breakAge>=.48f))return;
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    const auto vertex=[&](Vector3 p,Vector2 uv,Vector4 color){emit(Point(world,p),uv,color);};
    const float health=(std::clamp)(state.hitPoints/definition.maximumHitPoints,0.0f,1.0f);
    const bool active=state.Active(definition);
    const float clock=std::floor(std::fmod((std::max)(0.0f,age)*30,64.0f));
    const float fade=active?alpha:alpha*std::pow(1-state.breakAge/.48f,2.0f);
    for(size_t i=0;i<Corners.size();++i) {
        const auto a=Corners[i],b=Corners[(i+1)%Corners.size()];
        const float t=active?0:state.breakAge/.48f;
        const Vector3 drift{(a.x+b.x)*t*.32f,(a.y+b.y)*t*.32f,-t*.28f};
        const float base=active?64.0f:70.0f;
        const float packed=active?clock:std::floor(t*31);
        const Vector4 color=active?Vector4{.035f,.59f+health*.16f,1.05f,fade}:
            Vector4{.25f+std::exp(-t*25)*1.8f,1.05f,1.30f,fade};
        vertex(Add(Local({},true),drift),{base+.5f,packed*2+.5f},color);
        vertex(Add(Local(a),drift),{base+(a.x+1)*.5f,packed*2+(a.y+1)*.5f},color);
        vertex(Add(Local(b),drift),{base+(b.x+1)*.5f,packed*2+(b.y+1)*.5f},color);
    }
    // Short local rings persist on the moving surface; repeated primary hits
    // cannot replace each other's impact clock or turn into a full-body flash.
    const EnemyEnergyShieldImpact* guard=nullptr;
    for(const auto& hit:state.impacts)if(hit.lock && hit.age<GuardCueSeconds && (!guard || hit.age<guard->age))guard=&hit;
    for(const auto& hit:state.impacts) {
        if(hit.lock && &hit!=guard)continue;
        const float lifetime=hit.lock?.28f:.22f;
        if(hit.age>=lifetime)continue;
        const float t=hit.age/lifetime,r=hit.lock?.28f+t*.62f:.15f+t*.34f;
        const auto center=SurfacePoint(hit.uv);
        const float phase=std::floor(t*31)*2;
        const Vector4 c=hit.lock?Vector4{.45f,1.2f,1.6f,(1-t)*alpha}:
            Vector4{.20f,.75f,1.0f,(1-t)*alpha*.55f};
        const Vector3 points[]={{center.x-r,center.y-r,center.z-.01f},{center.x+r,center.y-r,center.z-.01f},
            {center.x-r,center.y+r,center.z-.01f},{center.x+r,center.y+r,center.z-.01f}};
        const Vector2 uv[]={{68,phase},{69,phase},{68,phase+1},{69,phase+1}};
        for(int j:{0,1,2,2,1,3})vertex(points[j],uv[j],c);
    }
    if(guard && active) {
        const float t=guard->age/GuardCueSeconds,packed=std::floor(t*31)*2;
        const Vector4 color{(guard->uv.x+1)*.5f,(guard->uv.y+1)*.5f,0,alpha*(1-t)};
        const auto facet=[&](Vector2 p,bool center) {
            auto local=Local(p,center);local.z-=.012f;
            vertex(local,{72+(p.x+1)*.5f,packed+(p.y+1)*.5f},color);
        };
        for(size_t i=0;i<Corners.size();++i){facet({},true);facet(Corners[i],false);facet(Corners[(i+1)%Corners.size()],false);}
        // Short tangential splinters disperse the absorbed energy. They have
        // no projectile head, collision, damage or reflected shot ownership.
        if(guard->age<.26f) {
            const float phase=guard->age/.26f;
            const auto center=SurfacePoint(guard->uv);
            for(int i=0;i<6;++i) {
                const float angle=float(i)*1.04719755f+guard->uv.x*.3f;
                const float dx=std::cos(angle),dy=std::sin(angle),travel=.13f+phase*.62f;
                const Vector3 a{center.x+dx*travel,center.y+dy*travel,center.z-.08f-phase*.28f};
                const Vector3 b{a.x+dx*(.12f+.22f*(1-phase)),a.y+dy*(.12f+.22f*(1-phase)),a.z-.10f};
                const Vector3 width{-dy*.045f,dx*.045f,0};
                const Vector3 points[]={Sub(a,width),Add(a,width),Sub(b,width),Add(b,width)};
                const Vector2 uv[]={{76,0},{76,1},{77,0},{77,1}};
                const Vector4 ink=i%3==0?Vector4{1.8f,1.05f,.28f,alpha*(1-phase)}:
                    Vector4{.50f,1.15f,1.55f,alpha*(1-phase)};
                for(int j:{0,1,2,2,1,3})vertex(points[j],uv[j],ink);
            }
        }
    }
    if(!active)return;
    for(float side:{-1.0f,1.0f}) {
        const float x=side*1.34f;
        const Vector3 points[]={{x-.055f,-.22f,-1.055f},{x+.055f,-.22f,-1.055f},
            {x-.055f,.22f,-1.055f},{x+.055f,.22f,-1.055f}};
        const Vector2 uv[]={{66,0},{67,0},{66,1},{67,1}};
        for(int j:{0,1,2,2,1,3})vertex(points[j],uv[j],{.20f,.95f,1.3f,alpha*.8f});
    }
}
}
