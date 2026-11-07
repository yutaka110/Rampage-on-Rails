#pragma once
#include <algorithm>
#include <cmath>

// A bounded, distance-parametrized stunt path. Vehicle, wheels and mounts
// sample this same curve; no render-only teleport or accumulated integration.
struct RailVehicleRideArc {
    bool enabled=false;
    float start=0,rampLength=40,flightLength=100,rise=6,apex=8;
    float Launch()const{return start+rampLength;}
    float Landing()const{return Launch()+flightLength;}
    bool Airborne(float d)const{return enabled && d>Launch() && d<Landing();}
    float Height(float d)const {
        if(!enabled || d<=start || d>=Landing())return 0;
        if(d<Launch()){const float t=(d-start)/rampLength;return rise*t*t;}
        const float u=(d-Launch())/flightLength,v=1-u;
        return rise*(1-u*u*(3-2*u))+16*apex*u*u*v*v+(2*rise/rampLength)*flightLength*u*v*v;
    }
    float Slope(float d)const{return (Height(d+.025f)-Height(d-.025f))/.05f;}
};
