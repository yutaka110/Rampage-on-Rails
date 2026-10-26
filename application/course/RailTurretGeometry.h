#pragma once
#include <string_view>
namespace RailTurret {
inline bool IsMesh(std::string_view mesh) { return mesh=="combat_turret" || mesh=="siege_turret"; }
struct Geometry { const char* baseMesh; const char* headMesh; float groundFootY,muzzleForward,muzzleSide,muzzleHeight,halfFootprint; };
inline Geometry ForMesh(std::string_view mesh) {
    return mesh=="siege_turret" ? Geometry{"siege_turret_base","siege_turret_head",-2.55f,1.95f,0.44f,0.32f,1.10f} :
        Geometry{"combat_turret_base","combat_turret_head",-0.47f,0.8272f,0.2538f,0.1598f,0.86f};
}
}
