#pragma once
#include <cstddef>
#include <array>
#include "utils/math/Vector.h"

namespace RailVehicleBreakaway {
inline constexpr const char* IntactMesh="rail_vehicle.mine_cart";
inline constexpr const char* DamagedMesh="rail_vehicle.mine_cart_damaged";
inline constexpr const char* PanelMesh="rail_vehicle.mine_cart_side_panel";
inline constexpr size_t WheelIndex=2; // Front-left: never the loaded right rail.
inline constexpr Vector3 PanelMinimum{-2.25f,0.38f,-3.20f},PanelMaximum{-1.78f,1.82f,3.20f};
inline constexpr Vector3 TrimMinimum{-2.31f,1.55f,-3.26f},TrimMaximum{-1.72f,1.88f,3.26f};
inline constexpr Vector3 DriveBlastLocal{-1.20f,-0.35f,-3.30f}; // Rear running-gear housing.
inline constexpr Vector3 PanelCentre{-2.015f,1.13f,0};
struct FracturePart { const char* mesh;Vector3 minimum,maximum; };
inline constexpr std::array<FracturePart,9> FractureParts{{
    {"rail_vehicle.mine_cart_fragment_floor_rear",{-2.15f,-0.30f,-3.15f},{2.15f,0.38f,0}},
    {"rail_vehicle.mine_cart_fragment_floor_front",{-2.15f,-0.30f,0},{2.15f,0.38f,3.15f}},
    {"rail_vehicle.mine_cart_fragment_side",{1.72f,0.38f,-3.26f},{2.31f,1.88f,3.26f}},
    {"rail_vehicle.mine_cart_fragment_rear",{-1.78f,0.38f,-3.26f},{1.78f,1.48f,-2.68f}},
    {"rail_vehicle.mine_cart_fragment_front",{-1.78f,0.38f,2.75f},{1.78f,1.25f,3.20f}},
    {"rail_vehicle.mine_cart_fragment_axle_rear",{-1.45f,-0.68f,-3.48f},{1.45f,-0.20f,-3.08f}},
    {"rail_vehicle.mine_cart_fragment_axle_front",{-1.45f,-0.68f,3.08f},{1.45f,-0.20f,3.48f}},
    {"rail_vehicle.mine_cart_fragment_drive_left",{-2.95f,-0.30f,0.70f},{-2.15f,0.34f,1.85f}},
    {"rail_vehicle.mine_cart_fragment_drive_right",{2.15f,-0.30f,0.70f},{2.95f,0.34f,1.85f}}
}};
inline constexpr size_t FinalPieceCount=FractureParts.size()+3;
}
