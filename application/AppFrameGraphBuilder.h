#pragma once

#include <functional>
#include <d3d12.h>

#include "AppFrameState.h"
#include "EffectSystem.h"

class AppFrameRenderer;
class AppGpuParticleSystem;
class AppImGuiLayer;
class AppPipelines;
class AppRenderResources;
class AppSceneResources;
class AppVfxRenderTargets;
struct AppVfxRendererSet;
class PostProcessStack;
class TerrainChunkManager;
struct AppRuntimeState;
namespace ge3::resources {
class EffectResourceCache;
}

namespace ge3::graphics {
class RenderGraph;
}
namespace editor {
class EditorProductionScenePipeline;
class EditorTransientMeshRenderPath;
class EditorProductionMaterialPipeline;
class EditorProductionTexturePipeline;
class EditorProductionShaderPipeline;
class EditorProductionLightingPipeline;
class EditorProductionGpuDrivenPipeline;
class EditorWorldPartitionPipeline;
}

struct AppFrameGraphBuildContext {
    ge3::graphics::RenderGraph* renderGraph = nullptr;
    AppRuntimeState* runtimeState = nullptr;
    AppFrameRenderer* frameRenderer = nullptr;
    AppImGuiLayer* imguiLayer = nullptr;
    AppPipelines* appPipelines = nullptr;
    AppRenderResources* renderResources = nullptr;
    AppSceneResources* scene = nullptr;
    AppVfxRenderTargets* vfxRenderTargets = nullptr;
    AppGpuParticleSystem* gpuParticleSystem = nullptr;
    AppVfxRendererSet* vfxRenderers = nullptr;
    PostProcessStack* postProcessStack = nullptr;
    FrameLoopState* frameState = nullptr;
    ID3D12DescriptorHeap* srvDescriptorHeap = nullptr;
    ID3D12Resource* backBuffer = nullptr;
    ID3D12Resource* depthTextureResource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    D3D12_GPU_DESCRIPTOR_HANDLE spriteTextureHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE vfxTextureHandle{};
    uint32_t vfxTextureDescriptorIndex = 0;
    const ge3::resources::EffectResourceCache* effectResourceCache = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE depthTextureHandle{};
    const EffectRuntimeFrame* effectRuntime = nullptr;
    ParticleRenderFallback primaryParticleFx{};
    float beamTime = 0.0f;
    // Presentation-only; zero keeps the normal render path allocation-free.
    float railFilmBurnProgress = 0.0f;
    // Burn the viewport texture, leaving editor controls outside the effect.
    bool railFilmBurnInEditorViewport = false;
    TerrainChunkManager* terrainChunkManager = nullptr;
    const editor::EditorProductionScenePipeline* productionScenePipeline = nullptr;
    const editor::EditorTransientMeshRenderPath* transientMeshRenderPath = nullptr;
    const editor::EditorProductionMaterialPipeline* productionMaterialPipeline = nullptr;
    const editor::EditorProductionTexturePipeline* productionTexturePipeline = nullptr;
    const editor::EditorProductionShaderPipeline* productionShaderPipeline = nullptr;
    editor::EditorProductionLightingPipeline* productionLightingPipeline = nullptr;
    editor::EditorProductionGpuDrivenPipeline* productionGpuDrivenPipeline = nullptr;
    editor::EditorWorldPartitionPipeline* worldPartitionPipeline = nullptr;
};

class AppFrameGraphBuilder {
public:
    using RegisterVfxPasses = std::function<void(const AppFrameGraphBuildContext&)>;

    void Build(const AppFrameGraphBuildContext& context) const;
    void Build(
        const AppFrameGraphBuildContext& context,
        const RegisterVfxPasses& registerVfxPasses) const;
};
