#pragma once

#include <span>
#include <unordered_map>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

#include "utils/math/MathUtils.h"

struct GpuMeshResource;

struct RailShadowCaster {
    uint32_t modelIndex;
    const GpuMeshResource* mesh;
    D3D12_GPU_VIRTUAL_ADDRESS transform;
    Matrix4x4 world;
};

struct RailNearShadowConstants {
    Matrix4x4 inverseViewProjection;
    Matrix4x4 lightViewProjection;
    Vector4 eyeAndRange;
    Vector4 sunAndStrength;
    Vector4 mapTexelAndDepth;
};
inline constexpr uint32_t kRailNearShadowDwords=sizeof(RailNearShadowConstants)/sizeof(uint32_t);
static_assert(kRailNearShadowDwords+5<=64);
inline constexpr float kRailNearShadowRange=110.0f;
inline constexpr float kRailCasterShadowDepth=320.0f;

// Immutable meshes share BLASes. The swap-chain slot owns each live TLAS,
// instance upload and build scratch until its fence completes.
class RailNearShadow {
public:
    static constexpr uint32_t MaxCasters=48;
    static bool Supports(ID3D12Device* device);
    D3D12_GPU_VIRTUAL_ADDRESS Build(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
        uint32_t slotIndex, uint32_t slotCount, std::span<const RailShadowCaster> casters);

private:
    struct Blas {
        Microsoft::WRL::ComPtr<ID3D12Resource> result, scratch;
    };
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> result, scratch, instances;
    };
    std::unordered_map<uint32_t,Blas> meshes_;
    std::vector<Slot> slots_;
};
