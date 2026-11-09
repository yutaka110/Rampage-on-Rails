#pragma once

#include "RailEnemyEnergyPalette.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace RailCartImpactEnergy {
struct Stroke {
    Vector3 start{},end{};
    float opacity=0,width=0;
};
struct Frame {
    std::array<Stroke,14> strokes{};
    size_t count=0;
};

inline constexpr float Duration(RailEnemyEnergy::BatteryKind kind) {
    return kind==RailEnemyEnergy::BatteryKind::Heavy?.225f:.115f;
}

inline Frame Evaluate(RailEnemyEnergy::BatteryKind kind,Vector3 contact,float age) {
    Frame result;
    if(kind==RailEnemyEnergy::BatteryKind::None || age<0 || age>=Duration(kind))return result;
    const bool heavy=kind==RailEnemyEnergy::BatteryKind::Heavy;
    const float phase=age/Duration(kind);
    const float side=contact.x<0?-1.0f:1.0f;
    const float end=contact.z<-2.1f?1.0f:-1.0f;
    const float rim=2.34f,top=1.91f,wall=3.31f;
    const float endLip=end<0?(heavy?1.51f:1.08f):1.30f;
    const float z=(std::clamp)(contact.z,-3.28f,3.28f);
    const bool hitEnd=std::abs(contact.z)>2.90f && std::abs(contact.x)<1.80f;
    const Vector3 origin=hitEnd?Vector3{contact.x,(std::clamp)(contact.y,.42f,contact.z<0?1.51f:1.30f),std::copysign(wall,contact.z)}:
        Vector3{side*rim,(std::clamp)(contact.y,.42f,top),z};
    const Vector3 alongRim{side*rim,top,hitEnd?origin.z:z};
    const Vector3 corner{side*rim,top,end*wall};
    const Vector3 nearLip{side*1.74f,endLip,end*wall};
    const Vector3 farLip{-side*1.74f,endLip,end*wall};
    const std::array main{origin,alongRim,corner,nearLip,heavy?farLip:Vector3{side*.62f,endLip,end*wall}};
    const auto length=[](Vector3 a,Vector3 b){return std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);};
    const float startBranch=length(origin,alongRim);
    const float cornerBranch=startBranch+length(alongRim,corner);
    const float mainLength=cornerBranch+length(corner,nearLip)+length(nearLip,main.back());
    const float tail=heavy?3.4f:2.15f;
    // The head passes each seam once. Only its short wake remains lit.
    const float finish=mainLength+tail*.68f;
    // The short gatling pulse reaches the end seam before its fade, even
    // when the impact starts at the distant end of the cart.
    const float cornerArrival=cornerBranch+length(corner,nearLip)*.86f;
    const float head=heavy?std::lerp(.54f,finish,phase):phase<.47f?
        std::lerp(.54f,cornerArrival,phase/.47f):std::lerp(cornerArrival,finish,(phase-.47f)/.53f);
    const float fade=1.0f-(std::clamp)((phase-.66f)/.34f,0.0f,1.0f);
    const auto trace=[&](std::span<const Vector3> points,float offset,float width,float strength) {
        for(size_t i=1;i<points.size() && result.count<result.strokes.size();++i) {
            const auto a=points[i-1],b=points[i];
            const float span=length(a,b);
            if(span<.001f)continue;
            const float first=(std::clamp)((head-tail-offset)/span,0.0f,1.0f);
            const float last=(std::clamp)((head-offset)/span,0.0f,1.0f);
            if(last>first+.002f) {
                const auto at=[&](float t){return Vector3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};};
                const float wake=1.0f-(std::clamp)((head-(offset+span*last))/tail,0.0f,1.0f);
                result.strokes[result.count++]={at(first),at(last),fade*strength*(.35f+.65f*wake),width};
            }
            offset+=span;
        }
    };
    trace(main,0,heavy?.115f:.085f,1);
    if(heavy) {
        const float nextZ=(std::clamp)(z+end*1.30f,-3.18f,3.18f);
        const std::array sideBranch{alongRim,Vector3{side*rim,1.28f,nextZ},
            Vector3{side*2.30f,.46f,nextZ},Vector3{side*1.64f,.06f,nextZ}};
        const std::array chassis{corner,Vector3{side*1.57f,1.02f,end*wall},
            Vector3{side*1.57f,.42f,end*wall},Vector3{-side*.96f,.22f,end*wall}};
        trace(sideBranch,startBranch,.073f,.76f);
        trace(chassis,cornerBranch,.087f,.84f);
    }
    return result;
}
}
