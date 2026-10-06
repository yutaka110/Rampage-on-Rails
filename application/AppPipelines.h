#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <filesystem>
#include <unordered_map>
#include <wrl/client.h>

#include "core/ShaderCompiler.h"

// AppMain.cpp から RootSignature / PSO / ShaderCompile を切り出す。
// 対象: Object3D, MotionDetect(CS), Particle。
class AppPipelines {
public:
    bool Initialize(ID3D12Device* device);
    bool HotReloadIfNeeded(ID3D12Device* device);

    ID3D12RootSignature* GetMainRootSignature() const { return mainRootSignature_.Get(); }
    ID3D12RootSignature* GetSkinnedRootSignature() const { return skinnedRootSignature_.Get(); }
    ID3D12RootSignature* GetSpriteRootSignature() const { return spriteRootSignature_.Get(); }
    ID3D12RootSignature* GetRailHudAtlasRootSignature() const { return railHudAtlasRootSignature_.Get(); }
    ID3D12RootSignature* GetSkyboxRootSignature() const { return skyboxRootSignature_.Get(); }
    ID3D12RootSignature* GetParticleRootSignature() const { return particleRootSignature_.Get(); }
    ID3D12RootSignature* GetRingRootSignature() const { return ringRootSignature_.Get(); }
    ID3D12RootSignature* GetCylinderRootSignature() const { return cylinderRootSignature_.Get(); }
    ID3D12RootSignature* GetSkeletonDebugRootSignature() const { return skeletonDebugRootSignature_.Get(); }
    ID3D12RootSignature* GetComputeRootSignature() const { return computeRootSignature_.Get(); }
    ID3D12RootSignature* GetSkinningComputeRootSignature() const { return skinningComputeRootSignature_.Get(); }
    ID3D12RootSignature* GetTerrainHiZBuildRootSignature() const { return terrainHiZBuildRootSignature_.Get(); }
    ID3D12RootSignature* GetTerrainDebrisCullRootSignature() const { return terrainDebrisCullRootSignature_.Get(); }

    ID3D12PipelineState* GetMainPSO() const { return mainPso_.Get(); }
    ID3D12PipelineState* GetTerrainPSO() const { return terrainPso_.Get(); }
    ID3D12PipelineState* GetTerrainWireframePSO() const { return terrainWireframePso_.Get(); }
    ID3D12PipelineState* GetTerrainShadowPSO() const { return terrainShadowPso_.Get(); }
    ID3D12PipelineState* GetTerrainDebrisPSO() const { return terrainDebrisPso_.Get(); }
    ID3D12PipelineState* GetTerrainDebrisShadowPSO() const { return terrainDebrisShadowPso_.Get(); }
    ID3D12PipelineState* GetTerrainDebrisCullPSO() const { return terrainDebrisCullPso_.Get(); }
    ID3D12PipelineState* GetSkinnedPSO() const { return skinnedPso_.Get(); }
    ID3D12PipelineState* GetMainOpaquePSO() const { return mainOpaquePso_.Get(); }
    ID3D12PipelineState* GetMainAlphaPSO() const { return mainAlphaPso_.Get(); }
    ID3D12PipelineState* GetSpritePSO() const { return spritePso_.Get(); }
    ID3D12PipelineState* GetRailHudAtlasPSO() const { return railHudAtlasPso_.Get(); }
    ID3D12PipelineState* GetTitleDustPSO() const { return titleDustPso_.Get(); }
    ID3D12PipelineState* GetTitleTracerPSO() const { return titleTracerPso_.Get(); }
    ID3D12PipelineState* GetSkyboxPSO() const { return skyboxPso_.Get(); }

    ID3D12PipelineState* GetComputePSO() const { return computePso_.Get(); }
    ID3D12PipelineState* GetSkinningComputePSO() const { return skinningComputePso_.Get(); }
    ID3D12PipelineState* GetTerrainHiZBuildPSO() const { return terrainHiZBuildPso_.Get(); }
    ID3D12RootSignature* GetGpuParticleComputeRootSignature() const { return gpuParticleComputeRootSignature_.Get(); }
    ID3D12RootSignature* GetTrailMeshStreamComputeRootSignature() const { return trailMeshStreamComputeRootSignature_.Get(); }
    ID3D12RootSignature* GetTrailMeshBuildComputeRootSignature() const { return trailMeshBuildComputeRootSignature_.Get(); }
    ID3D12PipelineState* GetGpuParticleComputePSO() const { return gpuParticleComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticleResetComputePSO() const { return gpuParticleResetComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolResetComputePSO() const { return gpuParticlePoolResetComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolBeginComputePSO() const { return gpuParticlePoolBeginComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolUpdateComputePSO() const { return gpuParticlePoolUpdateComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticleEmitterUpdateComputePSO() const { return gpuParticleEmitterUpdateComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticleEmitterResetComputePSO() const { return gpuParticleEmitterResetComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolSpawnPrepareComputePSO() const { return gpuParticlePoolSpawnPrepareComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolSpawnComputePSO() const { return gpuParticlePoolSpawnComputePso_.Get(); }
    ID3D12PipelineState* GetGpuParticlePoolArgsComputePSO() const { return gpuParticlePoolArgsComputePso_.Get(); }
    ID3D12PipelineState* GetTrailMeshStreamComputePSO() const { return trailMeshStreamComputePso_.Get(); }
    ID3D12PipelineState* GetTrailMeshBuildComputePSO() const { return trailMeshBuildComputePso_.Get(); }
    ID3D12RootSignature* GetCompositeRootSignature() const { return compositeRootSignature_.Get(); }
    ID3D12PipelineState* GetCompositePSO() const { return compositePso_.Get(); }
    ID3D12PipelineState* GetBloomExtractPSO() const { return bloomExtractPso_.Get(); }
    ID3D12PipelineState* GetBloomDownsamplePSO() const { return bloomDownsamplePso_.Get(); }
    ID3D12PipelineState* GetBloomUpsamplePSO() const { return bloomUpsamplePso_.Get(); }
    ID3D12PipelineState* GetBlurHorizontalPSO() const { return blurHorizontalPso_.Get(); }
    ID3D12PipelineState* GetBlurVerticalPSO() const { return blurVerticalPso_.Get(); }
    ID3D12PipelineState* GetBoxBlurHorizontalPSO() const { return boxBlurHorizontalPso_.Get(); }
    ID3D12PipelineState* GetBoxBlurVerticalPSO() const { return boxBlurVerticalPso_.Get(); }
    ID3D12PipelineState* GetGaussianBlurHorizontalPSO() const { return gaussianBlurHorizontalPso_.Get(); }
    ID3D12PipelineState* GetGaussianBlurVerticalPSO() const { return gaussianBlurVerticalPso_.Get(); }
    ID3D12PipelineState* GetDistortionCompositePSO() const { return distortionCompositePso_.Get(); }
    ID3D12PipelineState* GetAccretionCompositePSO() const { return accretionCompositePso_.Get(); }
    ID3D12PipelineState* GetDistanceFogPSO() const { return distanceFogPso_.Get(); }
    ID3D12PipelineState* GetContactAOPSO() const { return contactAoPso_.Get(); }
    ID3D12PipelineState* GetToneMappingPSO() const { return toneMappingPso_.Get(); }
    ID3D12PipelineState* GetGlowCompositePSO() const { return glowCompositePso_.Get(); }
    ID3D12PipelineState* GetWarpTunnelGeneratePSO() const { return warpTunnelGeneratePso_.Get(); }
    ID3D12PipelineState* GetWarpTunnelCompositePSO() const { return warpTunnelCompositePso_.Get(); }
    ID3D12PipelineState* GetDissolveMaskPSO() const { return dissolveMaskPso_.Get(); }
    ID3D12PipelineState* GetDissolvePSO() const { return dissolvePso_.Get(); }
    ID3D12PipelineState* GetRandomPSO() const { return randomPso_.Get(); }
    ID3D12PipelineState* GetPrewittOutlinePSO() const { return prewittOutlinePso_.Get(); }
    ID3D12PipelineState* GetGrayscalePSO() const { return grayscalePso_.Get(); }
    ID3D12PipelineState* GetVignettePSO() const { return vignettePso_.Get(); }
    ID3D12PipelineState* GetDebugDepthPreviewPSO() const { return debugDepthPreviewPso_.Get(); }
    ID3D12PipelineState* GetDebugEmissivePreviewPSO() const { return debugEmissivePreviewPso_.Get(); }

    ID3D12PipelineState* GetParticlePSO() const { return particlePso_.Get(); }
    ID3D12PipelineState* GetParticleOpaquePSO() const { return particleOpaquePso_.Get(); }
    ID3D12PipelineState* GetParticleAlphaPSO() const { return particleAlphaPso_.Get(); }
    ID3D12PipelineState* GetParticleAdditivePSO() const { return particleAdditivePso_.Get(); }
    ID3D12PipelineState* GetTrailMeshPSO() const { return trailMeshPso_.Get(); }
    ID3D12PipelineState* GetTrailMeshStreamPSO() const { return trailMeshStreamPso_.Get(); }
    ID3D12PipelineState* GetDistortionSpritePSO() const { return distortionSpritePso_.Get(); }
    ID3D12PipelineState* GetRingPSO() const { return ringPso_.Get(); }
    ID3D12PipelineState* GetSpearPSO() const { return spearPso_.Get(); }
    ID3D12PipelineState* GetOrbitRibbonPSO() const { return orbitRibbonPso_.Get(); }
    ID3D12PipelineState* GetCylinderPSO() const { return cylinderPso_.Get(); }
    ID3D12PipelineState* GetSkeletonDebugPSO() const { return skeletonDebugPso_.Get(); }
    ID3D12PipelineState* GetSkeletonDebugDepthTestPSO() const { return skeletonDebugDepthTestPso_.Get(); }

private:
    // RootSignature
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mainRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinnedRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> spriteRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> railHudAtlasRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ringRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> cylinderRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skeletonDebugRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningComputeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> terrainHiZBuildRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> terrainDebrisCullRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleComputeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> trailMeshStreamComputeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> trailMeshBuildComputeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSignature_;

    // PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mainPso_;      // 元の graphicsPipelineState
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainWireframePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainShadowPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainDebrisPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainDebrisShadowPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainDebrisCullPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinnedPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mainOpaquePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mainAlphaPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> railHudAtlasPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> titleDustPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> titleTracerPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPso_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> terrainHiZBuildPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleResetComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolResetComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolBeginComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolUpdateComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleEmitterUpdateComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticleEmitterResetComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolSpawnPrepareComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolSpawnComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gpuParticlePoolArgsComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trailMeshStreamComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trailMeshBuildComputePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomExtractPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomDownsamplePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomUpsamplePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurHorizontalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurVerticalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> boxBlurHorizontalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> boxBlurVerticalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gaussianBlurHorizontalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gaussianBlurVerticalPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distortionCompositePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> accretionCompositePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distanceFogPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> contactAoPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> toneMappingPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> glowCompositePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> warpTunnelGeneratePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> warpTunnelCompositePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> dissolveMaskPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> dissolvePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> randomPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> prewittOutlinePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> grayscalePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> vignettePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugDepthPreviewPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugEmissivePreviewPso_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> particlePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particleOpaquePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particleAlphaPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particleAdditivePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trailMeshPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> trailMeshStreamPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distortionSpritePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ringPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spearPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> orbitRibbonPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> cylinderPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skeletonDebugPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skeletonDebugDepthTestPso_;

    // Shader blobs are kept alive while PSO uses them
    ge3::core::ShaderCompiler shaderCompiler_;
    Microsoft::WRL::ComPtr<IDxcBlob> vs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skinnedVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> ps_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainShadowVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainDebrisVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainDebrisShadowVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainHiZBuildCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> terrainDebrisCullCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> spriteVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> spritePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> railHudAtlasVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> railHudAtlasPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skyboxVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skyboxPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> cs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skinningCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> particleVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> particlePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> trailMeshVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> trailMeshStreamVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> trailMeshPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> distortionSpriteVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> distortionSpritePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> ringVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> ringPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> spearVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> spearPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> orbitRibbonVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> orbitRibbonPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> cylinderVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> cylinderPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skeletonDebugVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> skeletonDebugPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleResetCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolResetCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolBeginCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolUpdateCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleEmitterUpdateCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticleEmitterResetCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolSpawnPrepareCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolSpawnCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gpuParticlePoolArgsCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> trailMeshStreamCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> trailMeshBuildCs_;
    Microsoft::WRL::ComPtr<IDxcBlob> compositeVs_;
    Microsoft::WRL::ComPtr<IDxcBlob> compositePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> bloomExtractPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> bloomDownsamplePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> bloomUpsamplePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> blurHorizontalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> blurVerticalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> boxBlurHorizontalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> boxBlurVerticalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gaussianBlurHorizontalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> gaussianBlurVerticalPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> distortionCompositePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> accretionCompositePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> distanceFogPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> contactAoPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> toneMappingPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> glowCompositePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> warpTunnelGeneratePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> warpTunnelCompositePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> dissolveMaskPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> dissolvePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> randomPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> prewittOutlinePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> grayscalePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> vignettePs_;
    Microsoft::WRL::ComPtr<IDxcBlob> debugDepthPreviewPs_;
    Microsoft::WRL::ComPtr<IDxcBlob> debugEmissivePreviewPs_;
    std::unordered_map<std::wstring, std::filesystem::path> shaderResolvedPaths_;
    std::unordered_map<std::wstring, std::filesystem::file_time_type> shaderWriteTimes_;
    uint32_t hotReloadPollFrame_ = 0;
    bool shaderHotReloadConfigured_ = false;
    bool shaderHotReloadEnabled_ = false;

    Microsoft::WRL::ComPtr<IDxcBlob> Compile_(const std::wstring& filePath, const wchar_t* profile);
    void TrackShader_(const std::wstring& filePath);
    bool ShaderChanged_(const std::wstring& filePath) const;
};
