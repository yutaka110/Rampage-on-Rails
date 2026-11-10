#pragma once

namespace RailRearPanelMaterial {
inline constexpr int Mode = 19;
// Material::padding2 maps to pad_, three float4s, then railLightingAxis in HLSL.
inline constexpr int PanelIndex = 0;
inline constexpr int WorldToPanel = 1;
inline constexpr int Time = 13;
inline constexpr int LockFlash = 14;
inline constexpr int VeilShape = 15;

inline constexpr int EncodeIndex(int index, bool broken) { return broken ? -1 - index : index; }
}
