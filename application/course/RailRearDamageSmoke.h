#pragma once
#include <algorithm>
#include <array>
#include "utils/math/Vector.h"

namespace RailRearDamageSmoke {
struct Vent {
    float threshold, interval, lifetime, radius, opacity, rise;
    Vector3 soot;
};
inline constexpr std::array<Vent,3> Vents{{
    {.75f,.138f,.85f,1.03f,.69f,5.4f,{.23f,.24f,.27f}},
    {.50f,.111f,1.00f,1.23f,.78f,6.8f,{.12f,.13f,.15f}},
    {.25f,.093f,1.15f,1.60f,.88f,8.4f,{.047f,.052f,.066f}},
}};

struct State {
    std::array<float,Vents.size()> since{{-1,-1,-1}};

    void Observe(float actorAge,float healthFraction) {
        for(size_t i=0;i<Vents.size();++i)
            if(since[i]<0 && healthFraction<=Vents[i].threshold)since[i]=actorAge;
    }
    size_t Stage() const {
        return std::count_if(since.begin(),since.end(),[](float time){return time>=0;});
    }
};
}
