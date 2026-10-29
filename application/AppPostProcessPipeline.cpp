#include "AppPostProcessPipeline.h"

#include <string>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AppFrameGraphBuilder.h"
#include "AppImGuiLayer.h"
#include "AppPipelines.h"
#include "AppVfxRenderTargets.h"
#include "AppSceneResources.h"
#include "AppRuntimeState.h"
#include "terrain/TerrainChunkManager.h"
#include "PostProcessStack.h"
#include "graphics/RenderGraph.h"

namespace {
ID3D12PipelineState* ResolvePostProcessPso(const AppPipelines& pipelines, const std::string& name) {
    if (name == "BloomExtract") {
        return pipelines.GetBloomExtractPSO();
    }
    if (name == "BloomDownsample") {
        return pipelines.GetBloomDownsamplePSO();
    }
    if (name == "BloomUpsample") {
        return pipelines.GetBloomUpsamplePSO();
    }
    if (name == "BlurHorizontal") {
        return pipelines.GetBlurHorizontalPSO();
    }
    if (name == "BlurVertical") {
        return pipelines.GetBlurVerticalPSO();
    }
    if (name == "BoxBlurHorizontal") {
        return pipelines.GetBoxBlurHorizontalPSO();
    }
    if (name == "BoxBlurVertical") {
        return pipelines.GetBoxBlurVerticalPSO();
    }
    if (name == "GaussianBlurHorizontal") {
        return pipelines.GetGaussianBlurHorizontalPSO();
    }
    if (name == "GaussianBlurVertical") {
        return pipelines.GetGaussianBlurVerticalPSO();
    }
    if (name == "DistortionComposite") {
        return pipelines.GetDistortionCompositePSO();
    }
    if (name == "AccretionComposite") {
        return pipelines.GetAccretionCompositePSO();
    }
    if (name == "DistanceFog") {
        return pipelines.GetDistanceFogPSO();
    }
    if (name == "ContactAO") {
        return pipelines.GetContactAOPSO();
    }
    if (name == "ToneMapping") {
        return pipelines.GetToneMappingPSO();
    }
    if (name == "GlowComposite") {
        return pipelines.GetGlowCompositePSO();
    }
    if (name == "WarpTunnelGenerate") {
        return pipelines.GetWarpTunnelGeneratePSO();
    }
    if (name == "WarpTunnelComposite") {
        return pipelines.GetWarpTunnelCompositePSO();
    }
    if (name == "DissolveMask") {
        return pipelines.GetDissolveMaskPSO();
    }
    if (name == "Dissolve") {
        return pipelines.GetDissolvePSO();
    }
    if (name == "Random") {
        return pipelines.GetRandomPSO();
    }
    if (name == "PrewittOutline") {
        return pipelines.GetPrewittOutlinePSO();
    }
    if (name == "Grayscale") {
        return pipelines.GetGrayscalePSO();
    }
    if (name == "Vignette") {
        return pipelines.GetVignettePSO();
    }
    return nullptr;
}

constexpr uint32_t kPostProcessParamCount = 16;

void BuildPassParams(const PostProcessPass& postPass, float passParams[kPostProcessParamCount]) {
    for (uint32_t i = 0; i < kPostProcessParamCount; ++i) {
        passParams[i] = 0.0f;
    }
    passParams[0] = postPass.intensity;

    if (postPass.pipeline == "BloomExtract") {
        passParams[1] = postPass.parameters.bloomThresholdMin;
        passParams[2] = postPass.parameters.bloomThresholdMax;
        passParams[3] = postPass.parameters.bloomSoftKnee;
        return;
    }
    if (postPass.pipeline == "BloomDownsample") {
        return;
    }
    if (postPass.pipeline == "BloomUpsample") {
        passParams[1] = postPass.parameters.bloomUpsampleBlend;
        passParams[2] = postPass.parameters.bloomUpsampleSoftKnee;
        return;
    }
    if (postPass.pipeline == "BlurHorizontal" || postPass.pipeline == "BlurVertical") {
        passParams[1] = postPass.parameters.blurRadius;
        return;
    }
    if (postPass.pipeline == "BoxBlurHorizontal" || postPass.pipeline == "BoxBlurVertical") {
        passParams[1] = postPass.parameters.boxBlurKernelRadius;
        return;
    }
    if (postPass.pipeline == "GaussianBlurHorizontal" || postPass.pipeline == "GaussianBlurVertical") {
        passParams[1] = postPass.parameters.gaussianBlurKernelRadius;
        passParams[2] = postPass.parameters.gaussianBlurSigma;
        return;
    }
    if (postPass.pipeline == "DistortionComposite") {
        passParams[1] = postPass.parameters.distortionScale;
        return;
    }
    if (postPass.pipeline == "AccretionComposite") {
        passParams[1] = postPass.parameters.accretionRadius;
        passParams[4] = postPass.parameters.accretionDiskStretch;
        passParams[5] = postPass.parameters.accretionTurbulence;
        passParams[6] = postPass.parameters.accretionChromaticAberration;
        passParams[7] = postPass.parameters.accretionCoreSize;
        passParams[8] = postPass.parameters.accretionCenterX;
        passParams[9] = postPass.parameters.accretionCenterY;
        passParams[10] = postPass.parameters.accretionFlowSpeed;
        passParams[11] = postPass.parameters.accretionRoadDepthFade;
        passParams[12] = postPass.parameters.accretionCoreDarkness;
        passParams[13] = postPass.parameters.accretionGuideOpacity;
        passParams[14] = postPass.parameters.accretionLensStrength;
        passParams[15] = postPass.parameters.accretionGuideWidth;
        return;
    }
    if (postPass.pipeline == "DistanceFog") {
        passParams[1] = postPass.parameters.fogStart;
        passParams[2] = postPass.parameters.fogEnd;
        passParams[3] = postPass.parameters.fogDensity;
        passParams[4] = postPass.parameters.fogColorR;
        passParams[5] = postPass.parameters.fogColorG;
        passParams[6] = postPass.parameters.fogColorB;
        passParams[7] = postPass.parameters.fogNearPlane;
        passParams[8] = postPass.parameters.fogFarPlane;
        passParams[9] = postPass.parameters.fogDepthBoost;
        passParams[10] = postPass.parameters.fogDepthBoostStart;
        passParams[11] = postPass.parameters.backlitFogLift;
        passParams[12] = postPass.parameters.openingGlowStrength;
        passParams[13] = postPass.parameters.foregroundSilhouetteStrength;
        passParams[14] = postPass.parameters.lowFogLayerStrength;
        passParams[15] = postPass.parameters.coolFloorHazeStrength;
        return;
    }
    if (postPass.pipeline == "ContactAO") {
        passParams[1] = postPass.parameters.contactAoRadiusPixels;
        passParams[2] = postPass.parameters.contactAoBias;
        passParams[3] = postPass.parameters.contactAoFalloff;
        passParams[4] = postPass.parameters.contactAoNearPlane;
        passParams[5] = postPass.parameters.contactAoFarPlane;
        return;
    }
    if (postPass.pipeline == "ToneMapping") {
        passParams[1] = postPass.parameters.toneExposure;
        return;
    }
    if (postPass.pipeline == "GlowComposite") {
        passParams[1] = postPass.parameters.glowWeight;
        passParams[2] = postPass.parameters.glowTintR;
        passParams[3] = postPass.parameters.glowTintG;
        passParams[4] = postPass.parameters.glowTintB;
        return;
    }
    if (postPass.pipeline == "WarpTunnelGenerate" || postPass.pipeline == "WarpTunnelComposite") {
        passParams[1] = postPass.parameters.warpTime;
        passParams[2] = postPass.parameters.warpTransition;
        passParams[3] = postPass.parameters.warpCenterX;
        passParams[4] = postPass.parameters.warpCenterY;
        passParams[5] = postPass.parameters.warpRefractionStrength;
        passParams[6] = postPass.parameters.warpSceneSwirl;
        passParams[7] = postPass.parameters.warpRotationSpeed;
        passParams[8] = postPass.parameters.warpFlowSpeed;
        passParams[9] = postPass.parameters.warpArms;
        passParams[10] = postPass.parameters.warpRings;
        passParams[11] = postPass.parameters.warpTwistX;
        passParams[12] = postPass.parameters.warpTwistY;
        passParams[13] = postPass.parameters.warpTunnelExposure;
        passParams[14] = postPass.parameters.warpFlash;
        passParams[15] = postPass.parameters.warpAspectRatio;
        return;
    }
    if (postPass.pipeline == "DissolveMask" || postPass.pipeline == "Dissolve") {
        passParams[1] = postPass.parameters.dissolveTime;
        passParams[2] = postPass.parameters.dissolveThreshold;
        passParams[3] = postPass.parameters.dissolveEdgeWidth;
        passParams[4] = postPass.parameters.dissolveNoiseScale;
        passParams[5] = postPass.parameters.dissolveNoiseSpeed;
        passParams[6] = postPass.parameters.dissolveEdgeColorR;
        passParams[7] = postPass.parameters.dissolveEdgeColorG;
        passParams[8] = postPass.parameters.dissolveEdgeColorB;
        passParams[9] = postPass.parameters.dissolveBurnStrength;
        passParams[10] = postPass.parameters.dissolveCenterX;
        passParams[11] = postPass.parameters.dissolveCenterY;
        passParams[12] = postPass.parameters.dissolveAspectRatio;
        passParams[13] = postPass.parameters.dissolveDirectionBlend;
        passParams[14] = postPass.parameters.dissolveSoftness;
        passParams[15] = postPass.parameters.dissolveSeed;
        return;
    }
    if (postPass.pipeline == "Random") {
        passParams[1] = postPass.parameters.randomTime;
        passParams[2] = postPass.parameters.randomSeed;
        passParams[3] = postPass.parameters.randomScale;
        passParams[4] = postPass.parameters.randomSpeed;
        passParams[5] = postPass.parameters.randomFrameRate;
        passParams[6] = postPass.parameters.randomContrast;
        passParams[7] = postPass.parameters.randomBrightness;
        passParams[8] = postPass.parameters.randomColorAmount;
        return;
    }
    if (postPass.pipeline == "PrewittOutline") {
        passParams[1] = postPass.parameters.outlineThreshold;
        passParams[2] = postPass.parameters.outlineThickness;
        passParams[3] = postPass.parameters.outlineSoftness;
        passParams[4] = postPass.parameters.outlineColorR;
        passParams[5] = postPass.parameters.outlineColorG;
        passParams[6] = postPass.parameters.outlineColorB;
        passParams[7] = postPass.parameters.outlineDepthWeight;
        return;
    }
    if (postPass.pipeline == "Grayscale") {
        passParams[1] = postPass.parameters.grayscaleMode;
        return;
    }
    if (postPass.pipeline == "Vignette") {
        passParams[1] = postPass.parameters.vignetteRadius;
        passParams[2] = postPass.parameters.vignetteSoftness;
        passParams[3] = postPass.parameters.vignettePower;
        return;
    }
}

void RegisterSunfall(const AppFrameGraphBuildContext& ctx,const std::string& sceneInput) {
    ctx.renderGraph->DeclareTransientRenderTarget("SunfallVolume",0.5f,DXGI_FORMAT_R16G16B16A16_FLOAT);
    ctx.renderGraph->DeclareTransientRenderTarget("SunfallColor",1.0f,DXGI_FORMAT_R16G16B16A16_FLOAT);
    // All opaque depth is ready in Lighting. Complete the HDR sky here so
    // read-only-depth VFX can blend over it without being classified as sky.
    ctx.renderGraph->AddPass({"Atmosphere.SunfallVolume",ge3::graphics::RenderPassLayer::Lighting,
        {{"SceneDepth",ge3::graphics::RenderResourceAccessType::ReadDepth},
         {"SunfallShadow",ge3::graphics::RenderResourceAccessType::ReadSrv},
         {"SunfallVolume",ge3::graphics::RenderResourceAccessType::WriteRtv}},"",
        [ctx](ge3::graphics::RenderPassContext& pass) {
            const auto& settings=ctx.runtimeState->sunfall;
            if(!ctx.scene->mappedCascadeShadow || !ctx.appPipelines->GetSunfallVolumePSO() ||
                ctx.depthTextureHandle.ptr==0 || ctx.scene->cascadeShadowSrvGpuHandles[0].ptr==0) return;
            SunfallFrameConstants constants;
            auto vp=ctx.frameState->viewProjectionMatrix;
            constants.inverseViewProjection=Inverse(vp);
            constants.lightViewProjection=ctx.scene->mappedCascadeShadow->lightViewProjection[0];
            const auto eye=ctx.frameState->cameraWorldPosition;
            constants.cameraAndTime={eye.x,eye.y,eye.z,ctx.beamTime};
            const auto& sun=ctx.runtimeState->directionalLightData;
            constants.sunDirectionAndStrength={sun.direction.x,sun.direction.y,sun.direction.z,
                (std::clamp)(sun.intensity,0.0f,6.0f)};
            constants.sunColorAndDensity={sun.color.x,sun.color.y,sun.color.z,
                (std::clamp)(settings.density,0.0f,0.008f)};
            constants.rangeHeightAndGround={(std::clamp)(settings.maxDistance,40.0f,240.0f),
                (std::clamp)(settings.heightFalloff,4.0f,100.0f),ctx.sunfallGroundHeight,ctx.sunfallOutdoorBlend};
            constants.windAndShadow={settings.windSpeed*0.8f,settings.windSpeed*0.35f,
                ctx.scene->mappedCascadeShadow->parameters.x,
                ctx.terrainChunkManager && !ctx.terrainChunkManager->RenderChunks().empty() ? 1.0f : 0.0f};
            ID3D12DescriptorHeap* heaps[]={ctx.srvDescriptorHeap};
            pass.commandList->SetDescriptorHeaps(1,heaps);
            pass.commandList->SetGraphicsRootSignature(ctx.appPipelines->GetSunfallRootSignature());
            pass.commandList->SetPipelineState(ctx.appPipelines->GetSunfallVolumePSO());
            pass.commandList->SetGraphicsRootDescriptorTable(0,ctx.depthTextureHandle);
            pass.commandList->SetGraphicsRootDescriptorTable(1,ctx.scene->cascadeShadowSrvGpuHandles[0]);
            pass.commandList->SetGraphicsRoot32BitConstants(2,52,&constants,0);
            pass.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pass.commandList->DrawInstanced(3,1,0,0);
        }});
    ctx.renderGraph->AddPass({"Atmosphere.SunfallComposite",ge3::graphics::RenderPassLayer::Lighting,
        {{sceneInput,ge3::graphics::RenderResourceAccessType::ReadSrv},
         {"SunfallVolume",ge3::graphics::RenderResourceAccessType::ReadSrv},
         {"SceneDepth",ge3::graphics::RenderResourceAccessType::ReadDepth},
         {"SunfallColor",ge3::graphics::RenderResourceAccessType::WriteRtv}},"",
        [ctx,sceneInput](ge3::graphics::RenderPassContext& pass) {
            // Real projection planes, not a second hard-coded depth convention.
            const auto& p=ctx.frameState->projMatrix.m;
            const float nearPlane=-p[3][2]/p[2][2];
            const float farPlane=-p[3][2]/(p[2][2]-1.0f);
            const auto& sun=ctx.runtimeState->directionalLightData;
            const float params[16]={(std::clamp)(ctx.runtimeState->sunfall.strength,0.0f,1.0f),nearPlane,farPlane,
                ctx.sunfallOutdoorBlend,sun.color.x,sun.color.y,sun.color.z,sun.intensity};
            ctx.vfxRenderTargets->ExecuteDebugPreviewPass(pass.commandList,"SunfallColor",
                ctx.appPipelines->GetCompositeRootSignature(),ctx.appPipelines->GetSunfallCompositePSO(),
                ctx.vfxRenderTargets->GetSrvHandle(sceneInput),ctx.vfxRenderTargets->GetSrvHandle("SunfallVolume"),
                ctx.depthTextureHandle,params);
        }});
}

} // namespace

const char* AppPostProcessPipeline::WorldVfxTarget(const AppFrameGraphBuildContext& ctx) {
    const bool atmosphere=ctx.sunfallActive && ctx.runtimeState &&
        ctx.runtimeState->sunfall.enabled && ctx.postProcessStack &&
        !BuildExecutionPlan(*ctx.postProcessStack,true).passes.empty();
    return atmosphere ? "SunfallColor" : "SceneColor";
}

void AppPostProcessPipeline::RegisterWorldVfxPass(const AppFrameGraphBuildContext& ctx,
    const std::string& name,
    std::function<void(ge3::graphics::RenderPassContext&)> draw) {
    ctx.renderGraph->AddPass({name,ge3::graphics::RenderPassLayer::Vfx,
        {{WorldVfxTarget(ctx),ge3::graphics::RenderResourceAccessType::WriteRtv},
         {"SceneDepth",ge3::graphics::RenderResourceAccessType::ReadDepth}},
        "SceneDepth",std::move(draw)});
}

PostProcessExecutionPlan AppPostProcessPipeline::BuildExecutionPlan(const PostProcessStack& stack,bool sunfall) {
    if(!sunfall) return stack.BuildExecutionPlan();
    auto passes=stack.Passes();
    auto tone=std::find_if(passes.begin(),passes.end(),[](const auto& p){return p.pipeline=="ToneMapping";});
    auto glow=std::find_if(passes.begin(),passes.end(),[](const auto& p){return p.pipeline=="GlowComposite";});
    if(tone!=passes.end() && glow!=passes.end() && tone<glow &&
        tone->outputResource=="PostProcessSwapOutput" && glow->outputResource=="PostProcessSwapOutput" &&
        tone->inputResource=="PostProcessOutput" && glow->tertiaryInputResource=="PostProcessOutput") {
        auto tonePass=*tone;
        const auto insertion=static_cast<size_t>(glow-passes.begin());
        passes.erase(tone);
        passes.insert(passes.begin()+insertion,std::move(tonePass));
    }
    PostProcessStack adapted=stack;
    std::string error;
    if(!adapted.ReplacePasses(std::move(passes),&error)) return stack.BuildExecutionPlan();
    return adapted.BuildExecutionPlan();
}

void AppPostProcessPipeline::RegisterPasses(const AppFrameGraphBuildContext& ctx) const {
    const bool sunfall=ctx.sunfallActive && ctx.runtimeState && ctx.runtimeState->sunfall.enabled;
    const PostProcessExecutionPlan executionPlan = BuildExecutionPlan(*ctx.postProcessStack,sunfall);
    const std::string finalOutputResource =
        executionPlan.finalOutputResource.empty() ? "SceneColor" : executionPlan.finalOutputResource;
    std::unordered_map<std::string,std::string> aliases;
    if(sunfall && !executionPlan.passes.empty()) {
        RegisterSunfall(ctx,"SceneColor");
        aliases["SceneColor"]="SunfallColor";
    }
    bool beforeTone=true;
    const bool hasTone=std::any_of(executionPlan.passes.begin(),executionPlan.passes.end(),
        [](const auto& item){return item.pass.pipeline=="ToneMapping";});
    for (const PostProcessExecutionPass& executionPass : executionPlan.passes) {
        PostProcessPass postPass = executionPass.pass;
        const auto resolve=[&](std::string& name) {const auto found=aliases.find(name);if(found!=aliases.end()) name=found->second;};
        resolve(postPass.inputResource);resolve(postPass.secondaryInputResource);resolve(postPass.tertiaryInputResource);
        if(postPass.pipeline=="ToneMapping") beforeTone=false;
        const bool hdr=sunfall && beforeTone && (hasTone || &executionPass!=&executionPlan.passes.back());
        if(sunfall && postPass.pipeline=="BloomExtract") {
            postPass.inputResource="SunfallColor";
            postPass.secondaryInputResource="VfxAccumulation";
        }
        const auto logicalOutput=postPass.outputResource;
        if(hdr) postPass.outputResource="SunfallHdr."+logicalOutput;
        aliases[logicalOutput]=postPass.outputResource;

        ctx.renderGraph->DeclareTransientRenderTarget(
            postPass.outputResource,
            postPass.resolutionScale,
            hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM);
        std::vector<ge3::graphics::RenderPassResourceAccess> accesses = {
            {postPass.inputResource, ge3::graphics::RenderResourceAccessType::ReadSrv},
            {postPass.outputResource, ge3::graphics::RenderResourceAccessType::WriteRtv},
        };
        if (postPass.pipeline == "PrewittOutline" || postPass.pipeline == "DistanceFog" || postPass.pipeline == "ContactAO") {
            accesses.push_back({"SceneDepth", ge3::graphics::RenderResourceAccessType::ReadDepth});
            accesses.push_back({postPass.tertiaryInputResource, ge3::graphics::RenderResourceAccessType::ReadSrv});
        } else {
            accesses.push_back({postPass.secondaryInputResource, ge3::graphics::RenderResourceAccessType::ReadSrv});
            accesses.push_back({postPass.tertiaryInputResource, ge3::graphics::RenderResourceAccessType::ReadSrv});
        }

        ctx.renderGraph->AddPass({
            std::string("PostProcess.") + postPass.name,
            ge3::graphics::RenderPassLayer::PostProcess,
            std::move(accesses),
            "",
            [ctx, postPass, hdr](ge3::graphics::RenderPassContext& passContext) {
                ID3D12DescriptorHeap* descriptorHeaps[] = { ctx.srvDescriptorHeap };
                passContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);
                ID3D12PipelineState* pipelineState = hdr ? ctx.appPipelines->GetHdrPostProcessPSO(postPass.pipeline) :
                    ResolvePostProcessPso(*ctx.appPipelines, postPass.pipeline);
                if (pipelineState == nullptr) {
                    return;
                }
                float passParams[kPostProcessParamCount] = {};
                BuildPassParams(postPass, passParams);
                if(ctx.sunfallActive && postPass.pipeline=="BloomExtract") {
                    passParams[5]=passParams[1];passParams[6]=passParams[2];
                    passParams[1]=0.90f;passParams[2]=1.65f;passParams[4]=1.0f;
                }
                if(ctx.sunfallActive && postPass.pipeline=="GlowComposite") {
                    // Warm neutral light shares the sun's palette; weapon cores
                    // keep their own colours in the final VFX composition.
                    passParams[1]*=0.65f;
                    passParams[2]=passParams[3]=passParams[4]=1.0f;
                }
                if(ctx.sunfallActive && postPass.pipeline=="ToneMapping") {
                    passParams[1]*=ctx.sunfallExposure;
                    passParams[2]=1.0f; // Linear HDR -> display sRGB on the UNORM target.
                }
                if(ctx.sunfallActive && postPass.pipeline=="DistanceFog") {
                    // Real shadowed sunlight replaces the screen-fixed glow.
                    passParams[0]*=0.65f*(1.0f-ctx.sunfallOutdoorBlend*0.85f);
                    passParams[11]=passParams[12]=0.0f;
                    passParams[13]*=0.40f*(1.0f-ctx.sunfallOutdoorBlend);
                    passParams[14]*=0.60f*(1.0f-ctx.sunfallOutdoorBlend*0.75f);
                    passParams[15]*=1.0f-ctx.sunfallOutdoorBlend*0.85f;
                }
                if (postPass.pipeline == "AccretionComposite") {
                    passParams[2] = ctx.beamTime;
                    uint32_t targetWidth = 0;
                    uint32_t targetHeight = 0;
                    if (ctx.vfxRenderTargets->GetTargetSize(postPass.outputResource, targetWidth, targetHeight)) {
                        passParams[3] = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
                    } else {
                        passParams[3] = 16.0f / 9.0f;
                    }
                }
                if (postPass.pipeline == "WarpTunnelGenerate" || postPass.pipeline == "WarpTunnelComposite") {
                    // Treat the authored time as an offset and keep both passes
                    // synchronized to the same gameplay/render clock.
                    passParams[1] += ctx.beamTime;
                    uint32_t targetWidth = 0;
                    uint32_t targetHeight = 0;
                    if (ctx.vfxRenderTargets->GetTargetSize(postPass.outputResource, targetWidth, targetHeight) &&
                        targetHeight > 0) {
                        passParams[15] = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
                    }
                }
                if (postPass.pipeline == "DissolveMask" || postPass.pipeline == "Dissolve") {
                    passParams[1] += ctx.beamTime;
                    uint32_t targetWidth = 0;
                    uint32_t targetHeight = 0;
                    if (ctx.vfxRenderTargets->GetTargetSize(postPass.outputResource, targetWidth, targetHeight) &&
                        targetHeight > 0) {
                        passParams[12] = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
                    }
                }
                if (postPass.pipeline == "Random") {
                    passParams[1] += ctx.beamTime;
                }
                if (postPass.pipeline == "PrewittOutline" || postPass.pipeline == "DistanceFog" || postPass.pipeline == "ContactAO") {
                    ctx.vfxRenderTargets->ExecuteDebugPreviewPass(
                        passContext.commandList,
                        postPass.outputResource,
                        ctx.appPipelines->GetCompositeRootSignature(),
                        pipelineState,
                        ctx.vfxRenderTargets->GetSrvHandle(postPass.inputResource),
                        ctx.depthTextureHandle,
                        ctx.vfxRenderTargets->GetSrvHandle(postPass.tertiaryInputResource),
                        passParams);
                } else {
                    ctx.vfxRenderTargets->ExecutePostProcessPass(
                        passContext.commandList,
                        postPass.outputResource,
                        ctx.appPipelines->GetCompositeRootSignature(),
                        pipelineState,
                        postPass.inputResource,
                        postPass.secondaryInputResource,
                        postPass.tertiaryInputResource,
                        passParams);
                }
        }});
    }

    if (ctx.HasRailBoostBlur()) {
        // Resolve through a separate target, then copy to the normal logical
        // output. Both the fullscreen and editor viewport therefore see the
        // effect, with no simultaneous sampling/writing of the same texture.
        ctx.renderGraph->DeclareTransientRenderTarget("RailBoostBlurred",1.0f,DXGI_FORMAT_R8G8B8A8_UNORM);
        ctx.renderGraph->AddPass({"PostProcess.RailBoostPeripheralBlur",ge3::graphics::RenderPassLayer::PostProcess,
            {{finalOutputResource,ge3::graphics::RenderResourceAccessType::ReadSrv},
             {"RailBoostProtection",ge3::graphics::RenderResourceAccessType::ReadSrv},
             {"RailBoostBlurred",ge3::graphics::RenderResourceAccessType::WriteRtv}},"",
            [ctx,finalOutputResource](ge3::graphics::RenderPassContext& pass) {
                // Mode -1 packs its optical pulse and two exhaust projections
                // into the existing constants; no extra fullscreen pass.
                const float params[16]={0,0,0,1,ctx.railBoostBlurStrength,-1,ctx.railBoostIgnitionShock,0,
                    ctx.railBoostBlurFocus.x,ctx.railBoostBlurFocus.y,
                    ctx.railBoostLensSources[0].x,ctx.railBoostLensSources[0].y,
                    ctx.railBoostLensSources[1].x,ctx.railBoostLensSources[1].y,
                    ctx.railBoostLensVisibility[0],ctx.railBoostLensVisibility[1]};
                ctx.vfxRenderTargets->ExecutePostProcessPass(pass.commandList,"RailBoostBlurred",
                    ctx.appPipelines->GetCompositeRootSignature(),ctx.appPipelines->GetCompositePSO(),
                    finalOutputResource,"RailBoostProtection",finalOutputResource,params);
            }});
        ctx.renderGraph->AddPass({"PostProcess.RailBoostResolve",ge3::graphics::RenderPassLayer::PostProcess,
            {{"RailBoostBlurred",ge3::graphics::RenderResourceAccessType::ReadSrv},
             {finalOutputResource,ge3::graphics::RenderResourceAccessType::WriteRtv}},"",
            [ctx,finalOutputResource](ge3::graphics::RenderPassContext& pass) {
                const float params[16]={0,0,0,1,0,-2};
                ctx.vfxRenderTargets->ExecutePostProcessPass(pass.commandList,finalOutputResource,
                    ctx.appPipelines->GetCompositeRootSignature(),ctx.appPipelines->GetCompositePSO(),
                    "RailBoostBlurred","RailBoostBlurred","RailBoostBlurred",params);
            }});
    }

    const bool editorViewportCompositedByImGui =
        ctx.railFilmBurnInEditorViewport ||
        (ctx.imguiLayer != nullptr && ctx.imguiLayer->WantsDeveloperDiagnostics());
    const bool filmBurnActive = ctx.HasRailFilmBurnPresentation();
    if (editorViewportCompositedByImGui && !filmBurnActive) {
        return;
    }

    const std::string presentationTarget = filmBurnActive ? "RailFilmBurnScene" : "BackBuffer";
    if (filmBurnActive) {
        ctx.renderGraph->DeclareTransientRenderTarget(
            presentationTarget, 1.0f, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    ctx.renderGraph->AddPass({
        "PostProcess.CompositeToBackBuffer",
        ge3::graphics::RenderPassLayer::PostProcess,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::ReadSrv},
            {"VfxAccumulation", ge3::graphics::RenderResourceAccessType::ReadSrv},
            {finalOutputResource, ge3::graphics::RenderResourceAccessType::ReadSrv},
            {presentationTarget, ge3::graphics::RenderResourceAccessType::WriteRtv},
        },
        "",
        [ctx, finalOutputResource, filmBurnActive, presentationTarget](ge3::graphics::RenderPassContext& passContext) {
            ID3D12DescriptorHeap* descriptorHeaps[] = { ctx.srvDescriptorHeap };
            passContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);
            const float compositeParams[kPostProcessParamCount] = {
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };
            if (filmBurnActive) {
                ctx.vfxRenderTargets->ExecutePostProcessPass(
                    passContext.commandList, presentationTarget,
                    ctx.appPipelines->GetCompositeRootSignature(),
                    ctx.appPipelines->GetCompositePSO(),
                    "SceneColor", "VfxAccumulation", finalOutputResource, compositeParams);
                return;
            }
            ctx.vfxRenderTargets->CompositeToBackBuffer(
                passContext.commandList,
                ctx.backBuffer,
                ctx.rtv,
                ctx.appPipelines->GetCompositeRootSignature(),
                ctx.appPipelines->GetCompositePSO(),
                finalOutputResource,
                compositeParams);
        },
        true});
}

void AppPostProcessPipeline::RegisterFilmBurnPresentationPass(const AppFrameGraphBuildContext& ctx) const {
    if (!ctx.HasRailFilmBurnPresentation()) return;
    const bool editorViewport = ctx.railFilmBurnInEditorViewport ||
        (ctx.imguiLayer != nullptr && ctx.imguiLayer->WantsDeveloperDiagnostics());
    const auto plan = ctx.postProcessStack->BuildExecutionPlan();
    const std::string output = editorViewport
        ? (plan.finalOutputResource.empty() ? "SceneColor" : plan.finalOutputResource)
        : "BackBuffer";
    std::vector<ge3::graphics::RenderPassResourceAccess> accesses{
        {"RailFilmBurnScene", ge3::graphics::RenderResourceAccessType::ReadSrv},
        {output, ge3::graphics::RenderResourceAccessType::WriteRtv},
    };
    if (!editorViewport) {
        accesses.push_back({"SceneColor", ge3::graphics::RenderResourceAccessType::ReadSrv});
        accesses.push_back({"VfxAccumulation", ge3::graphics::RenderResourceAccessType::ReadSrv});
    }
    ctx.renderGraph->AddPass({
        "UI.RailFilmBurnPresentation",
        ge3::graphics::RenderPassLayer::Ui,
        std::move(accesses), "",
        [ctx, editorViewport, output](ge3::graphics::RenderPassContext& passContext) {
            const float params[kPostProcessParamCount] = {
                ctx.railSandTransitionAge, 0, 0, 1, ctx.railFilmBurnProgress, 1,
                ctx.railFilmBurnTreatmentStrength, ctx.railSandTransition
                    ? (ctx.railFilmBurnIncoming ? 3.0f : 2.0f)
                    : (ctx.railFilmBurnIncoming ? 1.0f : 0.0f),
                ctx.railFilmBurnFocus.x, ctx.railFilmBurnFocus.y,
                ctx.railFilmBurnLightSource.x, ctx.railFilmBurnLightSource.y,
                ctx.railFilmBurnLensStrength, ctx.railFilmBurnLensAge,
                ctx.railFilmBurnTunnelShade, ctx.railCollapseDustStrength
            };
            if (editorViewport) {
                // All SRVs point to the capture, so SceneColor can safely be
                // the destination when the authored postprocess stack is empty.
                ctx.vfxRenderTargets->ExecutePostProcessPass(
                    passContext.commandList, output,
                    ctx.appPipelines->GetCompositeRootSignature(), ctx.appPipelines->GetCompositePSO(),
                    "RailFilmBurnScene", "RailFilmBurnScene", "RailFilmBurnScene", params);
                return;
            }
            ctx.vfxRenderTargets->CompositeToBackBuffer(
                passContext.commandList, ctx.backBuffer, ctx.rtv,
                ctx.appPipelines->GetCompositeRootSignature(), ctx.appPipelines->GetCompositePSO(),
                "RailFilmBurnScene", params);
        }, true});
}
