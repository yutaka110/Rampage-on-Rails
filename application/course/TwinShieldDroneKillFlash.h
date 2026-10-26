#pragma once

#include "TwinShieldDronePose.h"
#include <array>

enum class TwinShieldKillStrokeKind { Cut,Arc,Spark };
struct TwinShieldKillStroke final {
    Vector3 start{},end{};
    float width=0,opacity=0;
    TwinShieldKillStrokeKind kind=TwinShieldKillStrokeKind::Cut;
};
struct TwinShieldDroneKillFlash final {
    static constexpr float CutLifetime=0.17f, ArcBegin=0.15f, Lifetime=0.32f;
    std::array<TwinShieldKillStroke,19> strokes{};
    size_t count=0;
    Vector3 core{};
};

// Evaluated from the frozen lethal-contact pose and its simulation clock.
// Bounded world geometry needs no particles, collision queries or live actor.
inline TwinShieldDroneKillFlash EvaluateTwinShieldDroneKillFlash(const TwinShieldDronePose& pose,float age) {
    TwinShieldDroneKillFlash result;
    if(!pose.visible || !std::isfinite(age) || age<0 || age>=TwinShieldDroneKillFlash::Lifetime) return result;
    const auto add=[](Vector3 a,Vector3 b) {return Vector3{a.x+b.x,a.y+b.y,a.z+b.z};};
    const auto scale=[](Vector3 a,float s) {return Vector3{a.x*s,a.y*s,a.z*s};};
    const auto dot=[](Vector3 a,Vector3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto anchor=BuildTwinShieldDroneWorldMatrix(pose);
    const auto body=EvaluateTwinShieldDroneDestructionBody(pose,age);
    const auto transform=[&](Vector3 p) {
        return Vector3{p.x*body.m[0][0]+p.y*body.m[1][0]+p.z*body.m[2][0]+body.m[3][0],
            p.x*body.m[0][1]+p.y*body.m[1][1]+p.z*body.m[2][1]+body.m[3][1],
            p.x*body.m[0][2]+p.y*body.m[1][2]+p.z*body.m[2][2]+body.m[3][2]};
    };
    const auto right=Normalize(Vector3{anchor.m[0][0],anchor.m[0][1],anchor.m[0][2]});
    const auto up=Normalize(Vector3{anchor.m[1][0],anchor.m[1][1],anchor.m[1][2]});
    const float size=(std::clamp)((pose.scale.x+pose.scale.y+pose.scale.z)/3.0f,0.25f,8.0f);
    const auto append=[&](Vector3 a,Vector3 b,float width,float opacity,TwinShieldKillStrokeKind kind) {
        if(opacity<=0.001f || result.count>=result.strokes.size()) return;
        result.strokes[result.count++]={a,b,width,opacity,kind};
    };
    // Slightly ahead of the lens, so the hot core is visible with normal depth.
    result.core=transform({0,0,-1.02f});
    const float side=TwinShieldDestructionSide(pose);
    const float angle=side*0.48f+0.22f*dot(pose.destructionHitDirection,up)+
        0.12f*dot(pose.destructionHitDirection,right);
    const auto direction=Normalize(add(scale(right,std::cos(angle)),scale(up,std::sin(angle))));
    if(age<TwinShieldDroneKillFlash::CutLifetime) {
        const float t=age/TwinShieldDroneKillFlash::CutLifetime;
        const float halfLength=size*(1.15f+0.48f*(1-std::exp(-age*160)));
        // Hold the bright strike for one 30 Hz frame, then leave a readable
        // tapered tail instead of losing most of its energy immediately.
        const float intensity=1-TwinShieldDestructionSmooth(age,0.03f,TwinShieldDroneKillFlash::CutLifetime);
        append(add(result.core,scale(direction,-halfLength)),add(result.core,scale(direction,halfLength)),
            size*(0.080f+0.050f*(1-t)),intensity,TwinShieldKillStrokeKind::Cut);
    }
    // The cut leads; discharge travels out to the shield joints as its tail
    // fades. A shorter restrike bridges into the existing wing separation.
    const float arcPulse=TwinShieldDestructionSmooth(age,TwinShieldDroneKillFlash::ArcBegin,0.18f)*
        (1-TwinShieldDestructionSmooth(age,0.215f,0.245f))+
        0.65f*TwinShieldDestructionSmooth(age,0.25f,0.265f)*(1-TwinShieldDestructionSmooth(age,0.28f,0.315f));
    auto dischargePose=pose;dischargePose.destructionAge=age;
    const auto breakup=arcPulse>0.001f ? EvaluateTwinShieldDroneBreakup(dischargePose) : TwinShieldDroneBreakupFrame{};
    if(arcPulse>0.001f) for(float wing:{-1.0f,1.0f}) {
        const auto origin=transform({wing*0.34f,0,-1.02f});
        const auto& shield=wing<0?breakup.leftWorld:breakup.rightWorld;
        // Shield matrices contain their actor-space hinge offset, including
        // the detached motion. This is the same inner joint as x=+/-1.03.
        const Vector3 joint{shield.m[3][0]-wing*0.06f*shield.m[0][0]-0.96f*shield.m[2][0],
            shield.m[3][1]-wing*0.06f*shield.m[0][1]-0.96f*shield.m[2][1],
            shield.m[3][2]-wing*0.06f*shield.m[0][2]-0.96f*shield.m[2][2]};
        const float front=TwinShieldDestructionSmooth(age,TwinShieldDroneKillFlash::ArcBegin,0.205f);
        Vector3 previous=origin;
        for(int segment=1;segment<=6;++segment) {
            const float begin=float(segment-1)/6;
            if(front<=begin) break;
            const float t=(std::min)(float(segment)/6,front);
            const float jitter=t>=0.999f?0.0f:
                (segment%2?1.0f:-1.0f)*(0.025f+0.025f*std::sin(float(pose.destructionSeed%17u)+segment*2.3f));
            const auto point=add(add(origin,scale(add(joint,scale(origin,-1)),t)),
                {body.m[1][0]*jitter,body.m[1][1]*jitter,body.m[1][2]*jitter});
            append(previous,point,size*0.025f,arcPulse*0.70f,TwinShieldKillStrokeKind::Arc);
            previous=point;
        }
    }
    // A few short fragments release along the cut, then quickly lose energy.
    for(int index=0;index<6;++index) {
        const float born=0.022f+0.009f*float(index),elapsed=age-born;
        const float lifetime=0.11f+0.011f*float(index);
        if(elapsed<0 || elapsed>=lifetime) continue;
        const float seed=float((pose.destructionSeed+uint32_t(index)*11u)%37u);
        const float sign=index%2?1.0f:-1.0f;
        const auto velocity=add(scale(direction,sign*size*(4.2f+0.7f*float(index%3))),
            scale(up,size*0.70f*std::sin(seed)));
        const auto origin=transform({sign*0.30f,0,-1.02f});
        const auto head=add(origin,add(scale(velocity,elapsed),{0,-2.0f*elapsed*elapsed,0}));
        const auto tail=add(head,scale(velocity,-0.012f));
        const float fade=1-elapsed/lifetime;
        append(tail,head,size*0.018f,0.55f*fade*fade,TwinShieldKillStrokeKind::Spark);
    }
    return result;
}
