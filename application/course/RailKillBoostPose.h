#pragma once
#include "utils/math/MathUtils.h"
#include <cmath>

// Render-only rigid turn about rail up. Camera bounds, occupant mounts, player
// hitboxes and weapon rays continue to use their stable authoritative frames.
namespace RailKillBoostPose {
inline Vector3 Turn(Vector3 v,Vector3 axis,float radians) {
    const float length=std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
    if(length<.0001f)return v;
    axis={axis.x/length,axis.y/length,axis.z/length};
    const float c=std::cos(radians),s=std::sin(radians),dot=axis.x*v.x+axis.y*v.y+axis.z*v.z;
    return {v.x*c+(axis.y*v.z-axis.z*v.y)*s+axis.x*dot*(1-c),
        v.y*c+(axis.z*v.x-axis.x*v.z)*s+axis.y*dot*(1-c),
        v.z*c+(axis.x*v.y-axis.y*v.x)*s+axis.z*dot*(1-c)};
}
inline Matrix4x4 Apply(Matrix4x4 world,Vector3 pivot,Vector3 up,float degrees,float lift,float pitchDegrees=0) {
    if(!std::isfinite(degrees) || !std::isfinite(lift) || !std::isfinite(pitchDegrees))return world;
    const float radians=degrees*.01745329251994329577f;
    for(int row=0;row<3;++row) {
        const auto v=Turn({world.m[row][0],world.m[row][1],world.m[row][2]},up,radians);
        world.m[row][0]=v.x;world.m[row][1]=v.y;world.m[row][2]=v.z;
    }
    const auto p=Turn({world.m[3][0]-pivot.x,world.m[3][1]-pivot.y,world.m[3][2]-pivot.z},up,radians);
    world.m[3][0]=pivot.x+p.x+up.x*lift;world.m[3][1]=pivot.y+p.y+up.y*lift;world.m[3][2]=pivot.z+p.z+up.z*lift;
    const Vector3 right{world.m[0][0],world.m[0][1],world.m[0][2]};
    for(int row=0;row<3;++row) {
        const auto v=Turn({world.m[row][0],world.m[row][1],world.m[row][2]},right,pitchDegrees*.01745329251994329577f);
        world.m[row][0]=v.x;world.m[row][1]=v.y;world.m[row][2]=v.z;
    }
    return world;
}
} // namespace RailKillBoostPose
