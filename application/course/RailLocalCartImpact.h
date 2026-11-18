#pragma once

#include "RailRearCartImpact.h"

// Ordinary impacts start at the accepted contact, then energize the complete
// cart. Broad surface light preserves the silhouette at the normal camera size.
namespace RailLocalCartImpact {
inline constexpr float EnemyDuration=.40f, BatteryDuration=.48f;
inline bool IsBattery(std::string_view source) {return source.starts_with("enemy_turret");}
struct Flash {Vector3 position{},normal{};float intensity=0,radius=0;bool contact=false;};
struct Frame {
    std::array<RailCartImpactEnergy::Stroke,192> strokes{};
    std::array<RailCartImpactEnergy::Stroke,32> hotTips{};
    std::array<Flash,6> flashes{};
    size_t count=0,hotTipCount=0,flashCount=0;
    Vector3 contact{},normal{};
    float heat=0,halo=0,radius=0;
};
inline Frame Evaluate(Vector3 contact,Vector3 normal,float age,uint64_t seed,bool battery) {
    Frame result;
    const float duration=battery?BatteryDuration:EnemyDuration;
    if(!std::isfinite(age) || age<0 || age>=duration)return result;
    using RailCartImpactEnergy::Smooth;
    const auto bound=RailRearCartImpact::ResolveWingContact(contact,normal);
    result.contact=bound.point;result.normal=bound.normal;
    result.heat=2.1f*(1-Smooth(age,.045f,.135f));
    result.halo=.80f*(1-Smooth(age,.060f,.160f));
    result.radius=battery?.70f:.59f;
    result.flashes[result.flashCount++]={bound.point,bound.normal,result.heat,result.radius,true};
    const float side=bound.point.x<0?-1.0f:1.0f;
    const float variation=float(seed%127)*.713f;
    const float shape=std::sin(variation);
    const float fade=1-Smooth(age,battery?.28f:.23f,duration);
    const auto point=[](Vector3 a,Vector3 b,float t){return Vector3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};};
    unsigned branch=0;
    const auto trace=[&](std::span<const Vector3> nodes,Vector3 face,float starts,float speed,float width,float gain) {
        const float pattern=variation+float(++branch)*2.39996f;
        for(size_t i=1;i<nodes.size();++i) {
            const auto a=nodes[i-1],b=nodes[i];
            const Vector3 d{b.x-a.x,b.y-a.y,b.z-a.z};
            const float length=std::hypot(d.x,d.y,d.z);
            if(length<.01f)continue;
            const float travel=length/speed;
            const float progress=(age-starts)/travel;
            starts+=travel;
            if(progress<=0 || age-starts>.15f)continue;
            const Vector3 across{(face.y*d.z-face.z*d.y)/length,(face.z*d.x-face.x*d.z)/length,(face.x*d.y-face.y*d.x)/length};
            const int cells=(std::clamp)(int(std::ceil(length/.42f)),1,12);
            const auto knot=[&](int n) {
                auto p=point(a,b,float(n)/cells);
                if(n==0 || n==cells)return p;
                const float bend=.17f*std::sin(pattern+float(n)*3.73f+float(i)*1.91f);
                return Vector3{p.x+across.x*bend+face.x*.045f,p.y+across.y*bend+face.y*.045f,p.z+across.z*bend+face.z*.045f};
            };
            for(int cell=0;cell<cells && result.count<result.strokes.size();++cell) {
                const float from=float(cell)/cells,to=(std::min)(progress,float(cell+1)/cells);
                if(to<=from+.001f)break;
                const auto a0=knot(cell),end=point(a0,knot(cell+1),(to-from)*cells);
                const float since=travel*(progress-.5f*(from+to));
                const float tail=1-Smooth(since,.025f,.15f);
                if(tail<.008f)continue;
                result.strokes[result.count++]={a0,end,gain*fade*tail,width*(.64f+.36f*tail)};
                if(progress<1 && to>=progress && result.hotTipCount<result.hotTips.size()) {
                    const auto tip=point(end,a0,(std::min)(1.0f,.32f/((to-from)*length)));
                    result.hotTips[result.hotTipCount++]={tip,end,gain*fade,width*.78f};
                }
            }
        }
        return starts;
    };
    constexpr Vector3 top{0,1,0},back{0,0,-1},front{0,0,1};
    const float z=(std::clamp)(bound.point.z,-2.35f,2.35f);
    const Vector3 root{bound.point.x+bound.normal.x*.08f,bound.point.y+bound.normal.y*.08f,bound.point.z+bound.normal.z*.08f};
    const Vector3 cap{side*2.02f,1.96f,z};
    const float capTime=trace(std::array{root,Vector3{side*2.37f,1.26f,z+.16f*shape},cap},
        bound.normal,-.012f,95,.14f,1.0f);
    const Vector3 entry{side*1.46f,.47f,z};
    const float entryTime=trace(std::array{cap,Vector3{side*1.61f,1.94f,z+.13f},entry},top,capTime,105,.13f,.94f);
    const Vector3 fork{side*.24f,.47f,(std::clamp)(z+.65f*shape,-2.05f,2.05f)};
    const float forkTime=trace(std::array{entry,Vector3{side*.83f,.47f,z-.31f},fork},top,entryTime,86,.15f,1.0f);
    for(float direction:{-1.0f,1.0f}) {
        const Vector3 farFloor{direction*1.48f,.47f,-1.71f+.29f*shape};
        const float floorTime=trace(std::array{fork,Vector3{direction*.77f,.47f,-.63f-.19f*shape},farFloor},top,forkTime,92,.13f,.93f);
        trace(std::array{fork,Vector3{direction*.67f,.47f,1.20f+.17f*shape},Vector3{direction*1.40f,.47f,2.33f}},
            top,forkTime+.012f,94,.10f,.82f);
        const Vector3 lip{direction*2.03f,1.96f,-1.90f};
        const float lipTime=trace(std::array{farFloor,Vector3{direction*1.63f,1.13f,-1.84f},lip},
            {-direction,0,0},floorTime,88,.13f,.94f);
        trace(std::array{lip,Vector3{direction*2.05f,1.96f,-.70f+.20f*shape},Vector3{direction*1.84f,1.96f,.89f}},
            top,lipTime,105,.10f,.85f);
        const Vector3 corner{direction*1.70f,1.30f,-3.47f};
        const float endTime=trace(std::array{lip,Vector3{direction*2.35f,.93f,-2.63f},corner},
            {direction,0,0},lipTime,98,.13f,1.0f);
        const Vector3 split{direction*.55f,.88f,-3.47f};
        const float splitTime=trace(std::array{corner,Vector3{direction*1.09f,1.18f,-3.47f},split},back,endTime,94,.14f,1.0f);
        trace(std::array{split,Vector3{direction*.19f,.53f,-3.47f},Vector3{-direction*.61f,.66f,-3.47f}},back,splitTime,84,.11f,.87f);
        trace(std::array{split,Vector3{direction*.87f,.44f,-3.47f},Vector3{direction*1.49f,.32f,-3.47f}},back,splitTime+.008f,81,.085f,.75f);
        trace(std::array{Vector3{direction*1.42f,.47f,2.32f},Vector3{direction*1.64f,1.10f,3.45f},Vector3{direction*.38f,.78f,3.45f}},
            front,forkTime+.025f,108,.11f,.85f);
    }
    // Five broad, receiver-bound flashes illuminate the bed, both flanks and
    // both end plates. They cannot veil a projectile elsewhere on the screen.
    const float body=(battery?1.35f:1.10f)*Smooth(age,.012f,.055f)*(1-Smooth(age,.115f,.255f));
    result.flashes[result.flashCount++]={{0,.47f,0},top,body,2.45f,false};
    result.flashes[result.flashCount++]={{-2.27f,.93f,0},{-1,0,0},body,2.65f,false};
    result.flashes[result.flashCount++]={{2.27f,.93f,0},{1,0,0},body,2.65f,false};
    result.flashes[result.flashCount++]={{0,.91f,-3.43f},back,body,2.15f,false};
    result.flashes[result.flashCount++]={{0,.91f,3.43f},front,body,2.15f,false};
    return result;
}
}
