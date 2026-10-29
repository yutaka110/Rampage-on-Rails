#pragma once
#include <string_view>
#include <cmath>
#include "utils/math/Vector.h"
namespace RailTurret {
inline bool IsMesh(std::string_view mesh) { return mesh=="combat_turret" || mesh=="siege_turret" || mesh=="gatling_turret"; }
struct Geometry { const char* baseMesh; const char* headMesh; float groundFootY,muzzleForward,muzzleSide,muzzleHeight,halfFootprint; };
inline Geometry ForMesh(std::string_view mesh) {
    return mesh=="gatling_turret" ? Geometry{"siege_turret_base","gatling_turret_head",-2.55f,2.25f,0,0.32f,1.10f} :
        mesh=="siege_turret" ? Geometry{"siege_turret_base","siege_turret_head",-2.55f,1.95f,0.44f,0.32f,1.10f} :
        Geometry{"combat_turret_base","combat_turret_head",-0.47f,0.8272f,0.2538f,0.1598f,0.86f};
}
inline float GatlingRotation(float seconds) { return std::fmod(seconds*18.0f,6.28318530718f); }
inline Vector3 MuzzleLocal(std::string_view mesh,int barrel,float seconds) {
    const auto g=ForMesh(mesh);
    if(mesh=="gatling_turret") {
        const float angle=GatlingRotation(seconds)+float(barrel%6)*6.28318530718f/6;
        return {.40f*std::cos(angle),g.muzzleHeight+.40f*std::sin(angle),-g.muzzleForward};
    }
    return {(barrel&1?1.0f:-1.0f)*g.muzzleSide,g.muzzleHeight,-g.muzzleForward};
}
}
