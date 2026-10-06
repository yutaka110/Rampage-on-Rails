#pragma once

#include <string>
#include <string_view>
#include <vector>

struct PostProcessPass {
    struct Parameters {
        float bloomThresholdMin = 0.45f;
        float bloomThresholdMax = 1.1f;
        float bloomSoftKnee = 0.2f;
        float bloomUpsampleBlend = 1.0f;
        float bloomUpsampleSoftKnee = 0.35f;
        float blurRadius = 4.0f;
        float distortionScale = 0.02f;
        float toneExposure = 1.0f;
        float glowWeight = 1.0f;
        float glowTintR = 1.0f;
        float glowTintG = 1.0f;
        float glowTintB = 1.0f;
        float grayscaleMode = 1.0f;
        float vignetteRadius = 0.75f;
        float vignetteSoftness = 0.35f;
        float vignettePower = 1.0f;
        float boxBlurKernelRadius = 1.0f;
        float gaussianBlurKernelRadius = 2.0f;
        float gaussianBlurSigma = 1.5f;
        float outlineThreshold = 0.08f;
        float outlineThickness = 1.0f;
        float outlineSoftness = 0.04f;
        float outlineColorR = 0.0f;
        float outlineColorG = 0.0f;
        float outlineColorB = 0.0f;
        float outlineDepthWeight = 8.0f;
        float accretionRadius = 0.42f;
        float accretionDiskStretch = 2.35f;
        float accretionTurbulence = 0.85f;
        float accretionChromaticAberration = 0.95f;
        float accretionCoreSize = 0.18f;
        float accretionCenterX = 0.5f;
        float accretionCenterY = 0.5f;
        float accretionFlowSpeed = 1.0f;
        float accretionRoadDepthFade = 0.35f;
        float accretionCoreDarkness = 0.96f;
        float accretionGuideOpacity = 1.0f;
        float accretionLensStrength = 1.0f;
        float accretionGuideWidth = 0.18f;
        float fogStart = 180.0f;
        float fogEnd = 1200.0f;
        float fogDensity = 0.32f;
        float fogColorR = 0.46f;
        float fogColorG = 0.40f;
        float fogColorB = 0.34f;
        float fogNearPlane = 0.1f;
        float fogFarPlane = 5000.0f;
        float fogDepthBoost = 0.35f;
        float fogDepthBoostStart = 0.58f;
        float backlitFogLift = 0.18f;
        float openingGlowStrength = 0.42f;
        float foregroundSilhouetteStrength = 0.34f;
        float lowFogLayerStrength = 0.36f;
        float coolFloorHazeStrength = 0.28f;
        float contactAoRadiusPixels = 3.0f;
        float contactAoBias = 0.25f;
        float contactAoFalloff = 9.0f;
        float contactAoNearPlane = 0.1f;
        float contactAoFarPlane = 5000.0f;
        // WarpTunnel parameter layout is kept in the same order as the
        // 16-float root constants consumed by both warp tunnel shaders.
        float warpTime = 0.0f;
        float warpTransition = 0.0f;
        float warpCenterX = 0.5f;
        float warpCenterY = 0.5f;
        float warpRefractionStrength = 0.015f;
        float warpSceneSwirl = 0.25f;
        float warpRotationSpeed = 0.5f;
        float warpFlowSpeed = 0.6f;
        float warpArms = 14.0f;
        float warpRings = -6.0f;
        float warpTwistX = -5.0f;
        float warpTwistY = 12.0f;
        float warpTunnelExposure = 1.0f;
        float warpFlash = 0.0f;
        float warpAspectRatio = 16.0f / 9.0f;
        // Dissolve root-constant layout (intensity is stored on PostProcessPass).
        // The mask generator and composite share this layout so their noise,
        // threshold, and edge controls remain synchronized.
        float dissolveTime = 0.0f;
        float dissolveThreshold = 0.5f;
        float dissolveEdgeWidth = 0.08f;
        float dissolveNoiseScale = 7.0f;
        float dissolveNoiseSpeed = 0.12f;
        float dissolveEdgeColorR = 0.1f;
        float dissolveEdgeColorG = 0.7f;
        float dissolveEdgeColorB = 1.0f;
        float dissolveBurnStrength = 1.4f;
        float dissolveCenterX = 0.5f;
        float dissolveCenterY = 0.5f;
        float dissolveAspectRatio = 16.0f / 9.0f;
        float dissolveDirectionBlend = 0.25f;
        float dissolveSoftness = 0.025f;
        float dissolveSeed = 13.0f;
        // GPU pseudo-random noise parameters. Time changes the seed at a
        // controlled frame rate so the same frame remains deterministic.
        float randomTime = 0.0f;
        float randomSeed = 1.0f;
        float randomScale = 420.0f;
        float randomSpeed = 1.0f;
        float randomFrameRate = 24.0f;
        float randomContrast = 1.15f;
        float randomBrightness = 0.0f;
        float randomColorAmount = 0.0f;
        bool operator==(const Parameters&) const = default;
    };

    std::string name;
    std::string inputResource = "SceneColor";
    std::string outputResource = "PostColor";
    std::string pipeline = "FullscreenComposite";
    std::string secondaryInputResource = "VfxAccumulation";
    std::string tertiaryInputResource = "SceneColor";
    bool enabled = true;
    float intensity = 1.0f;
    float resolutionScale = 1.0f;
    Parameters parameters{};
    bool operator==(const PostProcessPass&) const = default;
    bool Validate(std::string* errorMessage = nullptr) const;
};

struct PostProcessExecutionPass {
    PostProcessPass pass;
};

struct PostProcessExecutionPlan {
    std::vector<PostProcessExecutionPass> passes;
    std::string finalOutputResource = "SceneColor";
};

enum class WarpTunnelPhase {
    Idle,
    Enter,
    Cruise,
    Exit,
};

enum class DissolvePhase {
    Idle,
    DissolveOut,
    Switch,
    DissolveIn,
};

class PostProcessStack {
public:
    void ResetToVfxDefaults();
    void SetEnabled(const std::string& name, bool enabled);
    void SetIntensity(const std::string& name, float intensity);
    bool IsEnabled(const std::string& name) const;
    float Intensity(const std::string& name) const;
    void StartWarpTunnel();
    void StopWarpTunnel();
    void UpdateWarpTunnel(float deltaTime);
    void SetWarpTunnelDurations(float enterDuration, float exitDuration);
    WarpTunnelPhase GetWarpTunnelPhase() const { return warpTunnelPhase_; }
    float WarpTunnelTransition() const { return warpTunnelTransition_; }
    float WarpTunnelFlash() const { return warpTunnelFlash_; }
    float WarpTunnelEnterDuration() const { return warpTunnelEnterDuration_; }
    float WarpTunnelExitDuration() const { return warpTunnelExitDuration_; }
    void StartDissolveTransition();
    void CancelDissolveTransition();
    void UpdateDissolve(float deltaTime);
    void SetDissolveDurations(float outDuration, float switchDuration, float inDuration);
    bool ConsumeDissolveSwitchRequest();
    DissolvePhase GetDissolvePhase() const { return dissolvePhase_; }
    float DissolveThreshold() const { return dissolveThreshold_; }
    float DissolveOutDuration() const { return dissolveOutDuration_; }
    float DissolveSwitchDuration() const { return dissolveSwitchDuration_; }
    float DissolveInDuration() const { return dissolveInDuration_; }
    bool HasDissolveSwitchRequest() const { return dissolveSwitchRequested_; }
    std::string FinalOutputResource() const;
    PostProcessExecutionPlan BuildExecutionPlan() const;
    const std::vector<PostProcessPass>& Passes() const { return passes_; }
    // Live tuning preserves controller-owned enable flags and transition progress.
    // Warp/dissolve enablement uses SetEnabled or the explicit start/stop operations.
    bool ConfigurePass(const PostProcessPass& pass, std::string* errorMessage = nullptr);
    bool ConfigurePasses(const std::vector<PostProcessPass>& passes, std::string* errorMessage = nullptr);
    // Full authoring restore cancels transient progress and reconciles pass flags.
    bool ReplacePasses(std::vector<PostProcessPass> passes, std::string* errorMessage = nullptr);
    static bool ValidatePasses(const std::vector<PostProcessPass>& passes, std::string* errorMessage = nullptr);

private:
    void SyncWarpTunnelPasses_();
    void SyncDissolvePasses_();

    std::vector<PostProcessPass> passes_;
    WarpTunnelPhase warpTunnelPhase_ = WarpTunnelPhase::Idle;
    float warpTunnelTransition_ = 0.0f;
    float warpTunnelFlash_ = 0.0f;
    float warpTunnelPhaseElapsed_ = 0.0f;
    float warpTunnelEnterDuration_ = 0.65f;
    float warpTunnelExitDuration_ = 0.55f;
    DissolvePhase dissolvePhase_ = DissolvePhase::Idle;
    float dissolveThreshold_ = 0.0f;
    float dissolvePhaseElapsed_ = 0.0f;
    float dissolveOutDuration_ = 0.80f;
    float dissolveSwitchDuration_ = 0.12f;
    float dissolveInDuration_ = 0.70f;
    bool dissolveSwitchRequested_ = false;
};
