#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace RailVeilLightning {
inline constexpr char Source[]="rear_veil_rail_lightning";
inline constexpr std::array<float,3> StrikeTimes{.83f,1.045f,1.26f};
inline constexpr std::array<float,3> StrikeLanes{-.46f,.48f,0.0f};
inline constexpr float CoreStart=.045f,CoreEnd=.285f;
inline constexpr float Clearance=StrikeTimes.back()+CoreEnd;
inline constexpr float RecoveryEnd=2.55f,VisualEnd=2.80f;
inline constexpr float DeckHeight=-.38f,HalfDepth=1.25f;
inline constexpr float CruiseSpeed=36.45f;

struct Chain {
    std::array<float,StrikeTimes.size()> positions{};
    float age=-1,previousDistance=0;
    uint8_t fired=0,hits=0,flashNow=0,hitNow=0;

    bool Visible()const{return age>=0 && age<VisualEnd;}
    bool BeforeFirstStrike()const{return age>=0 && !fired;}
    bool Threat()const{return fired && age<Clearance+.16f;}
    bool CounterOpen()const{return fired==7 && hits==0 && age>=Clearance && age<RecoveryEnd;}
    float LocalAge(size_t i)const{return age-StrikeTimes[i];}

    void Begin(float distance,float speed) {
        *this={};age=0;previousDistance=distance;
        const float reference=(std::clamp)(speed,CruiseSpeed,74.0f);
        // Aim once using the current travel pace. The nearest column's core
        // occupies the unbraked crossing, after the other two have erupted.
        const float nearest=distance+reference*1.385f;
        const float spacing=(std::clamp)(reference*.085f,3.05f,5.4f);
        for(size_t i=0;i<positions.size();++i)positions[i]=nearest+spacing*float(positions.size()-1-i);
    }

    void Advance(float dt,float distance) {
        flashNow=hitNow=0;
        if(!Visible() || dt<=0)return;
        const float oldAge=age;
        age+=dt;
        for(size_t i=0;i<positions.size();++i) {
            const auto mask=uint8_t(1u<<i);
            const float strike=StrikeTimes[i];
            if(!(fired&mask) && age>=strike){fired|=mask;flashNow|=mask;}
            if(hits&mask)continue;
            const float first=(std::max)(oldAge,strike+CoreStart);
            const float last=(std::min)(age,strike+CoreEnd);
            if(first>last)continue;
            const float a=std::lerp(previousDistance,distance,(first-oldAge)/dt);
            const float b=std::lerp(previousDistance,distance,(last-oldAge)/dt);
            if((std::min)(a,b)<=positions[i]+HalfDepth && (std::max)(a,b)>=positions[i]-HalfDepth) {
                hits|=mask;hitNow|=mask;
            }
        }
        previousDistance=distance;
    }
};
}
