#include "AppPostProcessPanel.h"

#include "PostProcessPresetStore.h"
#include "PostProcessStack.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
PostProcessPass* FindPass(std::vector<PostProcessPass>& passes, const char* name) {
    for (PostProcessPass& pass : passes) {
        if (pass.name == name) {
            return &pass;
        }
    }
    return nullptr;
}

void SetPassEnabled(PostProcessPass* pass, bool enabled) {
    if (pass != nullptr) {
        pass->enabled = enabled;
    }
}

const char* WarpTunnelPhaseLabel(WarpTunnelPhase phase) {
    switch (phase) {
    case WarpTunnelPhase::Idle: return "Idle";
    case WarpTunnelPhase::Enter: return "Enter";
    case WarpTunnelPhase::Cruise: return "Cruise";
    case WarpTunnelPhase::Exit: return "Exit";
    }
    return "Unknown";
}

const char* DissolvePhaseLabel(DissolvePhase phase) {
    switch (phase) {
    case DissolvePhase::Idle: return "Idle";
    case DissolvePhase::DissolveOut: return "DissolveOut";
    case DissolvePhase::Switch: return "Switch";
    case DissolvePhase::DissolveIn: return "DissolveIn";
    }
    return "Unknown";
}

void DrawBloomGlareControls(PostProcessStack& postProcessStack, std::vector<PostProcessPass>& passes) {
    PostProcessPass* extract = FindPass(passes, "BloomExtract");
    PostProcessPass* downsampleQuarter = FindPass(passes, "BloomDownsampleQuarter");
    PostProcessPass* blurQuarterH = FindPass(passes, "BloomBlurHorizontalQuarter");
    PostProcessPass* blurQuarterV = FindPass(passes, "BloomBlurVerticalQuarter");
    PostProcessPass* downsampleEighth = FindPass(passes, "BloomDownsampleEighth");
    PostProcessPass* blurEighthH = FindPass(passes, "BloomBlurHorizontalEighth");
    PostProcessPass* blurEighthV = FindPass(passes, "BloomBlurVerticalEighth");
    PostProcessPass* upsampleQuarter = FindPass(passes, "BloomUpsampleQuarter");
    PostProcessPass* upsampleHalf = FindPass(passes, "BloomUpsampleHalf");
    PostProcessPass* glowComposite = FindPass(passes, "GlowComposite");

    if (extract == nullptr || glowComposite == nullptr) {
        return;
    }

    ImGui::SeparatorText("Bloom / Glare Controls");
    bool bloomEnabled =
        extract->enabled &&
        (downsampleQuarter == nullptr || downsampleQuarter->enabled) &&
        (blurQuarterH == nullptr || blurQuarterH->enabled) &&
        (blurQuarterV == nullptr || blurQuarterV->enabled) &&
        (downsampleEighth == nullptr || downsampleEighth->enabled) &&
        (blurEighthH == nullptr || blurEighthH->enabled) &&
        (blurEighthV == nullptr || blurEighthV->enabled) &&
        (upsampleQuarter == nullptr || upsampleQuarter->enabled) &&
        (upsampleHalf == nullptr || upsampleHalf->enabled) &&
        glowComposite->enabled;
    if (ImGui::Checkbox("Bloom Chain", &bloomEnabled)) {
        SetPassEnabled(extract, bloomEnabled);
        SetPassEnabled(downsampleQuarter, bloomEnabled);
        SetPassEnabled(blurQuarterH, bloomEnabled);
        SetPassEnabled(blurQuarterV, bloomEnabled);
        SetPassEnabled(downsampleEighth, bloomEnabled);
        SetPassEnabled(blurEighthH, bloomEnabled);
        SetPassEnabled(blurEighthV, bloomEnabled);
        SetPassEnabled(upsampleQuarter, bloomEnabled);
        SetPassEnabled(upsampleHalf, bloomEnabled);
        SetPassEnabled(glowComposite, bloomEnabled);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Glare Composite", &glowComposite->enabled);

    float thresholdMin = extract->parameters.bloomThresholdMin;
    if (ImGui::SliderFloat("Bloom Threshold Min", &thresholdMin, 0.0f, 3.0f)) {
        extract->parameters.bloomThresholdMin = thresholdMin;
        extract->parameters.bloomThresholdMax = (std::max)(extract->parameters.bloomThresholdMax, thresholdMin + 0.01f);
    }
    ImGui::SliderFloat("Bloom Threshold Max", &extract->parameters.bloomThresholdMax, 0.01f, 6.0f);
    extract->parameters.bloomThresholdMax =
        (std::max)(extract->parameters.bloomThresholdMax, extract->parameters.bloomThresholdMin + 0.01f);
    ImGui::SliderFloat("Bloom Soft Knee", &extract->parameters.bloomSoftKnee, 0.01f, 1.5f);
    ImGui::SliderFloat("Bloom Extract Gain", &extract->intensity, 0.0f, 6.0f);

    float scatter = blurQuarterH != nullptr ? blurQuarterH->parameters.blurRadius / 6.0f : 1.0f;
    if (ImGui::SliderFloat("Glare Scatter", &scatter, 0.25f, 2.5f)) {
        const float quarterRadius = 6.0f * scatter;
        const float eighthRadius = 4.0f * scatter;
        if (blurQuarterH != nullptr) blurQuarterH->parameters.blurRadius = quarterRadius;
        if (blurQuarterV != nullptr) blurQuarterV->parameters.blurRadius = quarterRadius;
        if (blurEighthH != nullptr) blurEighthH->parameters.blurRadius = eighthRadius;
        if (blurEighthV != nullptr) blurEighthV->parameters.blurRadius = eighthRadius;
        if (upsampleQuarter != nullptr) upsampleQuarter->parameters.bloomUpsampleBlend = (std::clamp)(0.85f * scatter, 0.0f, 1.5f);
        if (upsampleHalf != nullptr) upsampleHalf->parameters.bloomUpsampleBlend = (std::clamp)(0.7f * scatter, 0.0f, 1.5f);
    }

    if (upsampleQuarter != nullptr) {
        ImGui::SliderFloat("Quarter Blend", &upsampleQuarter->parameters.bloomUpsampleBlend, 0.0f, 1.5f);
    }
    if (upsampleHalf != nullptr) {
        ImGui::SliderFloat("Half Blend", &upsampleHalf->parameters.bloomUpsampleBlend, 0.0f, 1.5f);
    }

    ImGui::SliderFloat("Glare Weight", &glowComposite->parameters.glowWeight, 0.0f, 6.0f);
    ImGui::SliderFloat("Glare Composite Gain", &glowComposite->intensity, 0.0f, 6.0f);
    ImGui::ColorEdit3("Glare Tint", &glowComposite->parameters.glowTintR);

    static std::string lookPresetStatus;
    PostProcessPresetStore presetStore{};
    if (ImGui::Button("Save Look Preset")) {
        std::string error;
        if (postProcessStack.ConfigurePasses(passes, &error) && presetStore.Save(postProcessStack, &error)) {
            lookPresetStatus = "Saved: " + presetStore.Path().string();
        } else {
            lookPresetStatus = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Look Preset")) {
        std::string error;
        if (presetStore.Load(postProcessStack, &error)) {
            passes = postProcessStack.Passes();
            lookPresetStatus = "Loaded: " + presetStore.Path().string();
        } else {
            lookPresetStatus = error;
        }
    }
    if (!lookPresetStatus.empty()) {
        ImGui::TextUnformatted(lookPresetStatus.c_str());
    }
}
} // namespace

void DrawPostProcessPanel(
    PostProcessStack& postProcessStack) {
    std::vector<PostProcessPass> passes = postProcessStack.Passes();
    ImGui::SeparatorText("Assignment Shortcuts");
    ImGui::TextUnformatted("0: Reset  1: WarpTunnel  2: Grayscale  3: Vignette");
    ImGui::TextUnformatted("4: BoxBlur  5: GaussianBlur  6: Outline  7: Dissolve  8: Random");
    DrawBloomGlareControls(postProcessStack, passes);
    for (PostProcessPass& pass : passes) {
        if (pass.pipeline == "WarpTunnelComposite") {
            continue;
        }
        if (pass.pipeline == "DissolveMask") {
            continue;
        }
        if (pass.pipeline == "WarpTunnelGenerate") {
            PostProcessPass* compositePass = nullptr;
            for (PostProcessPass& candidate : passes) {
                if (candidate.pipeline == "WarpTunnelComposite") {
                    compositePass = &candidate;
                    break;
                }
            }

            ImGui::PushID(pass.name.c_str());
            const WarpTunnelPhase phase = postProcessStack.GetWarpTunnelPhase();
            bool enabled = phase == WarpTunnelPhase::Enter || phase == WarpTunnelPhase::Cruise;
            if (ImGui::Checkbox("WarpTunnel", &enabled)) {
                if (enabled) {
                    postProcessStack.StartWarpTunnel();
                } else {
                    postProcessStack.StopWarpTunnel();
                }
            }
            ImGui::SameLine();
            ImGui::SliderFloat("Intensity", &pass.intensity, 0.0f, 1.0f);
            ImGui::Text(
                "Phase: %s  Transition: %.3f  Flash: %.3f",
                WarpTunnelPhaseLabel(postProcessStack.GetWarpTunnelPhase()),
                postProcessStack.WarpTunnelTransition(),
                postProcessStack.WarpTunnelFlash());
            float enterDuration = postProcessStack.WarpTunnelEnterDuration();
            float exitDuration = postProcessStack.WarpTunnelExitDuration();
            bool durationChanged = ImGui::SliderFloat("Enter Duration", &enterDuration, 0.10f, 3.0f);
            durationChanged |= ImGui::SliderFloat("Exit Duration", &exitDuration, 0.10f, 3.0f);
            if (durationChanged) {
                postProcessStack.SetWarpTunnelDurations(enterDuration, exitDuration);
            }
            ImGui::SliderFloat("Time", &pass.parameters.warpTime, 0.0f, 30.0f);
            ImGui::SliderFloat("Center X", &pass.parameters.warpCenterX, 0.0f, 1.0f);
            ImGui::SliderFloat("Center Y", &pass.parameters.warpCenterY, 0.0f, 1.0f);
            ImGui::SliderFloat("Refraction", &pass.parameters.warpRefractionStrength, 0.0f, 0.1f);
            ImGui::SliderFloat("Scene Swirl", &pass.parameters.warpSceneSwirl, -2.0f, 2.0f);
            ImGui::SliderFloat("Rotation Speed", &pass.parameters.warpRotationSpeed, -4.0f, 4.0f);
            ImGui::SliderFloat("Flow Speed", &pass.parameters.warpFlowSpeed, -4.0f, 4.0f);
            ImGui::SliderFloat("Arms", &pass.parameters.warpArms, 1.0f, 32.0f);
            ImGui::SliderFloat("Rings", &pass.parameters.warpRings, -24.0f, 24.0f);
            ImGui::SliderFloat("Twist X", &pass.parameters.warpTwistX, -24.0f, 24.0f);
            ImGui::SliderFloat("Twist Y", &pass.parameters.warpTwistY, -24.0f, 24.0f);
            ImGui::SliderFloat("Tunnel Exposure", &pass.parameters.warpTunnelExposure, 0.0f, 4.0f);
            ImGui::SliderFloat("Aspect Ratio", &pass.parameters.warpAspectRatio, 0.5f, 4.0f);
            if (compositePass != nullptr) {
                compositePass->intensity = pass.intensity;
                compositePass->parameters = pass.parameters;
            }
            ImGui::Text("  Generate 0.50x -> refract/radial blur/transition Composite 1.00x");
            ImGui::PopID();
            continue;
        }
        if (pass.pipeline == "BoxBlurVertical") {
            continue;
        }
        if (pass.pipeline == "GaussianBlurVertical") {
            continue;
        }
        if (pass.pipeline == "BoxBlurHorizontal") {
            PostProcessPass* verticalPass = nullptr;
            for (PostProcessPass& candidate : passes) {
                if (candidate.pipeline == "BoxBlurVertical") {
                    verticalPass = &candidate;
                    break;
                }
            }

            ImGui::PushID(pass.name.c_str());
            bool enabled = pass.enabled && (verticalPass == nullptr || verticalPass->enabled);
            if (ImGui::Checkbox("BoxBlur", &enabled)) {
                pass.enabled = enabled;
                if (verticalPass != nullptr) {
                    verticalPass->enabled = enabled;
                }
            }
            ImGui::SameLine();
            float intensity = pass.intensity;
            if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f)) {
                pass.intensity = intensity;
                if (verticalPass != nullptr) {
                    verticalPass->intensity = intensity;
                }
            }
            const char* kernels[] = { "3x3", "5x5" };
            int kernel = pass.parameters.boxBlurKernelRadius >= 1.5f ? 1 : 0;
            if (ImGui::Combo("Kernel", &kernel, kernels, _countof(kernels))) {
                const float kernelRadius = kernel == 0 ? 1.0f : 2.0f;
                pass.parameters.boxBlurKernelRadius = kernelRadius;
                if (verticalPass != nullptr) {
                    verticalPass->parameters.boxBlurKernelRadius = kernelRadius;
                }
            }
            ImGui::Text("  separable box blur: horizontal -> vertical scale=%.2f", pass.resolutionScale);
            ImGui::PopID();
            continue;
        }
        if (pass.pipeline == "GaussianBlurHorizontal") {
            PostProcessPass* verticalPass = nullptr;
            for (PostProcessPass& candidate : passes) {
                if (candidate.pipeline == "GaussianBlurVertical") {
                    verticalPass = &candidate;
                    break;
                }
            }

            ImGui::PushID(pass.name.c_str());
            bool enabled = pass.enabled && (verticalPass == nullptr || verticalPass->enabled);
            if (ImGui::Checkbox("GaussianBlur", &enabled)) {
                pass.enabled = enabled;
                if (verticalPass != nullptr) {
                    verticalPass->enabled = enabled;
                }
            }
            ImGui::SameLine();
            float intensity = pass.intensity;
            if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f)) {
                pass.intensity = intensity;
                if (verticalPass != nullptr) {
                    verticalPass->intensity = intensity;
                }
            }
            const char* kernels[] = { "3x3", "5x5", "7x7", "9x9" };
            int kernel = static_cast<int>(pass.parameters.gaussianBlurKernelRadius) - 1;
            kernel = kernel < 0 ? 0 : (kernel > 3 ? 3 : kernel);
            if (ImGui::Combo("Kernel", &kernel, kernels, _countof(kernels))) {
                const float kernelRadius = static_cast<float>(kernel + 1);
                pass.parameters.gaussianBlurKernelRadius = kernelRadius;
                if (verticalPass != nullptr) {
                    verticalPass->parameters.gaussianBlurKernelRadius = kernelRadius;
                }
            }
            float sigma = pass.parameters.gaussianBlurSigma;
            if (ImGui::SliderFloat("Sigma", &sigma, 0.3f, 6.0f)) {
                pass.parameters.gaussianBlurSigma = sigma;
                if (verticalPass != nullptr) {
                    verticalPass->parameters.gaussianBlurSigma = sigma;
                }
            }
            ImGui::Text("  separable gaussian blur: horizontal -> vertical scale=%.2f", pass.resolutionScale);
            ImGui::PopID();
            continue;
        }
        if (pass.pipeline == "Dissolve") {
            PostProcessPass* maskPass = nullptr;
            for (PostProcessPass& candidate : passes) {
                if (candidate.pipeline == "DissolveMask") {
                    maskPass = &candidate;
                    break;
                }
            }

            ImGui::PushID(pass.name.c_str());
            const DissolvePhase phase = postProcessStack.GetDissolvePhase();
            if (phase == DissolvePhase::Idle) {
                if (ImGui::Button("Start Dissolve Transition")) {
                    postProcessStack.StartDissolveTransition();
                }
            } else if (ImGui::Button("Cancel Dissolve Transition")) {
                postProcessStack.CancelDissolveTransition();
            }
            ImGui::SameLine();
            ImGui::SliderFloat("Intensity", &pass.intensity, 0.0f, 1.0f);
            ImGui::Text(
                "Phase: %s  Threshold: %.3f%s",
                DissolvePhaseLabel(phase),
                postProcessStack.DissolveThreshold(),
                postProcessStack.HasDissolveSwitchRequest() ? "  Switch Pending" : "");

            float outDuration = postProcessStack.DissolveOutDuration();
            float switchDuration = postProcessStack.DissolveSwitchDuration();
            float inDuration = postProcessStack.DissolveInDuration();
            bool durationChanged = ImGui::SliderFloat("Out Duration", &outDuration, 0.05f, 3.0f);
            durationChanged |= ImGui::SliderFloat("Switch Hold", &switchDuration, 0.0f, 1.0f);
            durationChanged |= ImGui::SliderFloat("In Duration", &inDuration, 0.05f, 3.0f);
            if (durationChanged) {
                postProcessStack.SetDissolveDurations(outDuration, switchDuration, inDuration);
            }

            ImGui::SliderFloat("Edge Width", &pass.parameters.dissolveEdgeWidth, 0.0f, 0.3f);
            ImGui::SliderFloat("Edge Softness", &pass.parameters.dissolveSoftness, 0.001f, 0.15f);
            ImGui::ColorEdit3("Edge Color", &pass.parameters.dissolveEdgeColorR);
            ImGui::SliderFloat("Burn Strength", &pass.parameters.dissolveBurnStrength, 0.0f, 5.0f);
            ImGui::SliderFloat("Noise Scale", &pass.parameters.dissolveNoiseScale, 0.5f, 24.0f);
            ImGui::SliderFloat("Noise Speed", &pass.parameters.dissolveNoiseSpeed, 0.0f, 2.0f);
            ImGui::SliderFloat("Center X", &pass.parameters.dissolveCenterX, 0.0f, 1.0f);
            ImGui::SliderFloat("Center Y", &pass.parameters.dissolveCenterY, 0.0f, 1.0f);
            ImGui::SliderFloat("Direction Blend", &pass.parameters.dissolveDirectionBlend, 0.0f, 1.0f);
            ImGui::SliderFloat("Seed", &pass.parameters.dissolveSeed, 0.0f, 100.0f);
            if (maskPass != nullptr) {
                maskPass->intensity = pass.intensity;
                maskPass->parameters = pass.parameters;
            }
            ImGui::TextUnformatted("  Mask -> Threshold/Edge -> hidden Switch point -> reveal");
            ImGui::PopID();
            continue;
        }

        ImGui::PushID(pass.name.c_str());
        ImGui::Checkbox(pass.name.c_str(), &pass.enabled);
        ImGui::SameLine();
        ImGui::SliderFloat("Intensity", &pass.intensity, 0.0f, 4.0f);
        if (pass.pipeline == "BloomExtract") {
            ImGui::SliderFloat("Threshold Min", &pass.parameters.bloomThresholdMin, 0.0f, 2.0f);
            ImGui::SliderFloat("Threshold Max", &pass.parameters.bloomThresholdMax, 0.0f, 4.0f);
            ImGui::SliderFloat("Soft Knee", &pass.parameters.bloomSoftKnee, 0.01f, 1.0f);
        } else if (pass.pipeline == "BloomUpsample") {
            ImGui::SliderFloat("Blend", &pass.parameters.bloomUpsampleBlend, 0.0f, 1.5f);
            ImGui::SliderFloat("Soft Knee", &pass.parameters.bloomUpsampleSoftKnee, 0.01f, 1.0f);
        } else if (pass.pipeline == "BlurHorizontal" || pass.pipeline == "BlurVertical") {
            ImGui::SliderFloat("Blur Radius", &pass.parameters.blurRadius, 1.0f, 8.0f);
        } else if (pass.pipeline == "DistortionComposite") {
            ImGui::SliderFloat("Distortion Scale", &pass.parameters.distortionScale, 0.0f, 0.1f);
        } else if (pass.pipeline == "AccretionComposite") {
            ImGui::SliderFloat("Center X", &pass.parameters.accretionCenterX, 0.0f, 1.0f);
            ImGui::SliderFloat("Center Y", &pass.parameters.accretionCenterY, 0.0f, 1.0f);
            ImGui::SliderFloat("Radius", &pass.parameters.accretionRadius, 0.12f, 0.9f);
            ImGui::SliderFloat("Disk Stretch", &pass.parameters.accretionDiskStretch, 0.6f, 4.0f);
            ImGui::SliderFloat("Turbulence", &pass.parameters.accretionTurbulence, 0.0f, 1.5f);
            ImGui::SliderFloat("Chromatic", &pass.parameters.accretionChromaticAberration, 0.0f, 2.0f);
            ImGui::SliderFloat("Core Size", &pass.parameters.accretionCoreSize, 0.04f, 0.36f);
            ImGui::SliderFloat("Flow Speed", &pass.parameters.accretionFlowSpeed, 0.0f, 4.0f);
            ImGui::SliderFloat("Road Fade", &pass.parameters.accretionRoadDepthFade, 0.0f, 2.0f);
            ImGui::SliderFloat("Core Darkness", &pass.parameters.accretionCoreDarkness, 0.0f, 1.0f);
            ImGui::SliderFloat("Guide Opacity", &pass.parameters.accretionGuideOpacity, 0.0f, 2.0f);
            ImGui::SliderFloat("Lens Strength", &pass.parameters.accretionLensStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Guide Width", &pass.parameters.accretionGuideWidth, 0.04f, 0.5f);
        } else if (pass.pipeline == "DistanceFog") {
            ImGui::SliderFloat("Fog Start", &pass.parameters.fogStart, 0.0f, 1000.0f);
            ImGui::SliderFloat("Fog End", &pass.parameters.fogEnd, 1.0f, 5000.0f);
            ImGui::SliderFloat("Fog Density", &pass.parameters.fogDensity, 0.0f, 4.0f);
            ImGui::ColorEdit3("Fog Color", &pass.parameters.fogColorR);
            ImGui::SliderFloat("Fog Near Plane", &pass.parameters.fogNearPlane, 0.001f, 10.0f);
            ImGui::SliderFloat("Fog Far Plane", &pass.parameters.fogFarPlane, 10.0f, 10000.0f);
            ImGui::SliderFloat("Fog Depth Boost", &pass.parameters.fogDepthBoost, 0.0f, 2.0f);
            ImGui::SliderFloat("Fog Boost Start", &pass.parameters.fogDepthBoostStart, 0.0f, 1.0f);
            ImGui::SliderFloat("Backlit Fog Lift", &pass.parameters.backlitFogLift, 0.0f, 1.5f);
            ImGui::SliderFloat("Opening Glow", &pass.parameters.openingGlowStrength, 0.0f, 2.0f);
            ImGui::SliderFloat("Foreground Silhouette", &pass.parameters.foregroundSilhouetteStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Low Fog Layer", &pass.parameters.lowFogLayerStrength, 0.0f, 1.5f);
            ImGui::SliderFloat("Cool Floor Haze", &pass.parameters.coolFloorHazeStrength, 0.0f, 1.5f);
        } else if (pass.pipeline == "ContactAO") {
            ImGui::SliderFloat("AO Radius", &pass.parameters.contactAoRadiusPixels, 1.0f, 12.0f);
            ImGui::SliderFloat("AO Bias", &pass.parameters.contactAoBias, 0.01f, 3.0f);
            ImGui::SliderFloat("AO Falloff", &pass.parameters.contactAoFalloff, 1.0f, 64.0f);
            ImGui::SliderFloat("AO Near Plane", &pass.parameters.contactAoNearPlane, 0.001f, 10.0f);
            ImGui::SliderFloat("AO Far Plane", &pass.parameters.contactAoFarPlane, 10.0f, 10000.0f);
        } else if (pass.pipeline == "ToneMapping") {
            ImGui::SliderFloat("Exposure", &pass.parameters.toneExposure, 0.1f, 4.0f);
        } else if (pass.pipeline == "GlowComposite") {
            ImGui::SliderFloat("Glow Weight", &pass.parameters.glowWeight, 0.0f, 4.0f);
            ImGui::ColorEdit3("Glow Tint", &pass.parameters.glowTintR);
        } else if (pass.pipeline == "PrewittOutline") {
            ImGui::SliderFloat("Threshold", &pass.parameters.outlineThreshold, 0.001f, 0.5f);
            ImGui::SliderFloat("Thickness", &pass.parameters.outlineThickness, 1.0f, 4.0f);
            ImGui::SliderFloat("Softness", &pass.parameters.outlineSoftness, 0.001f, 0.25f);
            ImGui::SliderFloat("Depth Weight", &pass.parameters.outlineDepthWeight, 0.0f, 64.0f);
            ImGui::ColorEdit3("Outline Color", &pass.parameters.outlineColorR);
        } else if (pass.pipeline == "Grayscale") {
            const char* modes[] = { "Average", "BT.709" };
            int mode = pass.parameters.grayscaleMode >= 0.5f ? 1 : 0;
            if (ImGui::Combo("Mode", &mode, modes, _countof(modes))) {
                pass.parameters.grayscaleMode = static_cast<float>(mode);
            }
        } else if (pass.pipeline == "Vignette") {
            ImGui::SliderFloat("Radius", &pass.parameters.vignetteRadius, 0.0f, 1.5f);
            ImGui::SliderFloat("Softness", &pass.parameters.vignetteSoftness, 0.01f, 1.0f);
            ImGui::SliderFloat("Power", &pass.parameters.vignettePower, 0.1f, 4.0f);
        } else if (pass.pipeline == "Random") {
            ImGui::SliderFloat("Seed", &pass.parameters.randomSeed, 0.0f, 1000.0f);
            ImGui::SliderFloat("Noise Scale", &pass.parameters.randomScale, 32.0f, 1200.0f);
            ImGui::SliderFloat("Animation Speed", &pass.parameters.randomSpeed, 0.0f, 4.0f);
            ImGui::SliderFloat("Seed Frame Rate", &pass.parameters.randomFrameRate, 1.0f, 60.0f);
            ImGui::SliderFloat("Contrast", &pass.parameters.randomContrast, 0.0f, 4.0f);
            ImGui::SliderFloat("Brightness", &pass.parameters.randomBrightness, -1.0f, 1.0f);
            ImGui::SliderFloat("Color Amount", &pass.parameters.randomColorAmount, 0.0f, 1.0f);
            ImGui::TextUnformatted("  UV + quantized time seed -> deterministic GPU random");
        }
        ImGui::Text("  %s -> %s pipeline=%s scale=%.2f",
            pass.inputResource.c_str(),
            pass.outputResource.c_str(),
            pass.pipeline.c_str(),
            pass.resolutionScale);
        ImGui::PopID();
    }
    std::string error;
    if (!postProcessStack.ConfigurePasses(passes, &error)) ImGui::TextWrapped("Edit rejected: %s", error.c_str());

}
