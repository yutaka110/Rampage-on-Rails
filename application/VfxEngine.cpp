#include "VfxEngine.h"
#include "AppLogFile.h"

#include "AppRuntimeState.h"
#include "AppSceneResources.h"
#include "AppVfxRenderPipeline.h"
#include "AppPipelines.h"
#include "terrain/TerrainChunkManager.h"
#include "vfx/VfxRenderInputs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {
constexpr float kElectricOrbStrikeMinimumDuration = 4.10f;
constexpr float kEffectAssetReloadPollInterval = 1.0f;
constexpr DWORD kVfxPrepareWatchdogStallMs = 3000;
constexpr double kVfxPrepareSpikeLogThresholdMs = 3.0;

std::atomic<bool> gVfxPrepareWatchdogStarted{false};
std::atomic<DWORD> gVfxPrepareStageTick{0};
std::atomic<const char*> gVfxPrepareStageName{nullptr};

using VfxPreparePerfClock = std::chrono::steady_clock;

double VfxPrepareElapsedMs(
    VfxPreparePerfClock::time_point begin,
    VfxPreparePerfClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void WriteVfxPrepareWatchdogLine(const char* message) {
    OutputDebugStringA(message);
    std::ofstream log = app::OpenRotatingLog("logs/vfx_prepare_watchdog.log");
    if (log) {
        log << message;
    }
}

void WriteVfxPrepareSpikeLine(const std::string& message) {
    OutputDebugStringA(message.c_str());
    std::filesystem::create_directories("logs");
    std::ofstream log = app::OpenRotatingLog("logs/vfx_prepare_spikes.log");
    if (log) {
        log << message;
    }
}

void StartVfxPrepareWatchdogOnce() {
    bool expected = false;
    if (!gVfxPrepareWatchdogStarted.compare_exchange_strong(expected, true)) {
        return;
    }
    std::thread([]() {
        const char* lastLoggedStage = nullptr;
        for (;;) {
            Sleep(1000);
            const DWORD tick = gVfxPrepareStageTick.load(std::memory_order_relaxed);
            if (tick == 0u) {
                continue;
            }
            const DWORD idleMs = GetTickCount() - tick;
            if (idleMs < kVfxPrepareWatchdogStallMs) {
                continue;
            }
            const char* stage = gVfxPrepareStageName.load(std::memory_order_relaxed);
            if (stage == lastLoggedStage) {
                continue;
            }
            lastLoggedStage = stage;
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "[VfxPrepareWatchdog] stalled idleMs=%lu stage=%s\n",
                static_cast<unsigned long>(idleMs),
                stage != nullptr ? stage : "unknown");
            WriteVfxPrepareWatchdogLine(message);
        }
    }).detach();
}

void RecordVfxPrepareStage(const char* stage) {
    StartVfxPrepareWatchdogOnce();
    gVfxPrepareStageName.store(stage != nullptr ? stage : "unknown", std::memory_order_relaxed);
    gVfxPrepareStageTick.store(GetTickCount(), std::memory_order_relaxed);
}

size_t ShowcaseIndex(AppVfxRuntimeState::ShowcaseEffect effect) {
    return static_cast<size_t>(effect);
}

bool IsSameAuthoredComponent(
    const EffectComponentCommon* source,
    const EffectComponentCommon* destination) {
    if (source == nullptr || destination == nullptr) {
        return false;
    }
    if (source->id != 0 && source->id == destination->id) {
        return true;
    }
    return source->name == destination->name && source->type == destination->type;
}

bool HasTerrainDustEmitters(const AppFrameGraphBuildContext& graphContext) {
    if (graphContext.runtimeState == nullptr ||
        graphContext.terrainChunkManager == nullptr ||
        !graphContext.runtimeState->terrain.enabled ||
        graphContext.runtimeState->terrain.settings.dustZoneDensity <= 0.0f) {
        return false;
    }

    for (const TerrainChunkDebugInfo& chunk : graphContext.terrainChunkManager->Chunks()) {
        if (!chunk.vfxZones.empty()) {
            return true;
        }
    }
    return false;
}

bool HasVfxParticleWork(
    const AppFrameGraphBuildContext& graphContext,
    const EffectRuntimeFrame& runtimeFrame) {
    return graphContext.runtimeState != nullptr &&
        graphContext.runtimeState->vfx.enableParticles &&
        (!runtimeFrame.particleQueue.empty() || HasTerrainDustEmitters(graphContext));
}

bool HasAnyVfxWork(
    const AppFrameGraphBuildContext& graphContext,
    const EffectRuntimeFrame& runtimeFrame) {
    if (graphContext.runtimeState == nullptr) {
        return false;
    }
    const AppVfxRuntimeState& runtimeState = graphContext.runtimeState->vfx;
    if (HasVfxParticleWork(graphContext, runtimeFrame)) {
        return true;
    }
    return (runtimeState.enableTrails && !runtimeFrame.trailQueue.empty()) ||
        (runtimeState.enableBeams && !runtimeFrame.beamQueue.empty()) ||
        (runtimeState.enableRings && !runtimeFrame.ringQueue.empty()) ||
        (runtimeState.enableCylinders && !runtimeFrame.cylinderQueue.empty()) ||
        (runtimeState.enableElectricOrbStrike && runtimeState.electricOrbStrikeActive) ||
        (runtimeState.enableDistortions && !runtimeFrame.distortionQueue.empty());
}

bool ReadEnvFlag(const wchar_t* name) {
    wchar_t value[32]{};
    const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(_countof(value)));
    if (length == 0) {
        return false;
    }

    for (DWORD i = 0; i < length && i < _countof(value); ++i) {
        value[i] = static_cast<wchar_t>(std::towlower(value[i]));
    }

    return value[0] == L'1' ||
        std::wcscmp(value, L"true") == 0 ||
        std::wcscmp(value, L"yes") == 0 ||
        std::wcscmp(value, L"on") == 0;
}

bool IsAuthoringSampleEffectPath(const std::filesystem::path& path) {
    const std::string stem = path.stem().string();
    return stem.rfind("Authoring", 0) == 0;
}

bool IsAuthoringSampleEffectAsset(const LoadedEffectAsset& loaded) {
    return loaded.asset.name.rfind("authoring_", 0) == 0 ||
        IsAuthoringSampleEffectPath(loaded.path);
}

bool ShouldLoadAuthoringSamples() {
    static const bool enabled = ReadEnvFlag(L"GE3_LOAD_AUTHORING_SAMPLES");
    return enabled;
}

void ApplyLiveTuningToComponent(
    const ParticleComponentAssetView& source,
    EffectParticleSettings& destination) {
    destination.lifetime = source.settings->lifetime;
    destination.depthFadeSoftness = source.settings->depthFadeSoftness;
    destination.edgeSoftness = source.settings->edgeSoftness;
    destination.spawnCount = source.settings->spawnCount;
    destination.spawnFrequency = source.settings->spawnFrequency;
    destination.randomRotation = source.settings->randomRotation;
    destination.scaleYMin = source.settings->scaleYMin;
    destination.scaleYMax = source.settings->scaleYMax;
}

void ApplyLiveTuningToComponent(
    const TrailComponentAssetView& source,
    EffectTrailSettings& destination) {
    destination.depthFadeSoftness = source.settings->depthFadeSoftness;
    destination.trailTailFade = source.settings->trailTailFade;
    destination.length = source.settings->length;
    destination.width = source.settings->width;
    destination.sampleDistance = source.settings->sampleDistance;
    destination.smoothing = source.settings->smoothing;
    destination.widthHead = source.settings->widthHead;
    destination.widthTail = source.settings->widthTail;
    destination.alphaTail = source.settings->alphaTail;
    destination.miterLimit = source.settings->miterLimit;
    destination.colorTail = source.settings->colorTail;
    destination.followMode = source.settings->followMode;
    destination.segmentBudget = source.settings->segmentBudget;
}

void ApplyLiveTuningToComponent(
    const DistortionComponentAssetView& source,
    EffectDistortionSettings& destination) {
    destination.depthFadeSoftness = source.settings->depthFadeSoftness;
    destination.depthAttenuation = source.settings->depthAttenuation;
}

void ApplyLiveTuningToComponent(
    const RingComponentAssetView& source,
    EffectRingSettings& destination) {
    destination.divide = source.settings->divide;
    destination.outerRadius = source.settings->outerRadius;
    destination.innerRadius = source.settings->innerRadius;
    destination.emissive = source.settings->emissive;
    destination.uvScrollSpeed = source.settings->uvScrollSpeed;
    destination.expansion = source.settings->expansion;
    destination.fadeOut = source.settings->fadeOut;
    destination.depthFadeSoftness = source.settings->depthFadeSoftness;
}

void ApplyLiveTuningToComponent(
    const CylinderComponentAssetView& source,
    EffectCylinderSettings& destination) {
    destination.divide = source.settings->divide;
    destination.topRadius = source.settings->topRadius;
    destination.bottomRadius = source.settings->bottomRadius;
    destination.height = source.settings->height;
    destination.emissive = source.settings->emissive;
    destination.uvScrollSpeed = source.settings->uvScrollSpeed;
    destination.alphaReference = source.settings->alphaReference;
    destination.fadeOut = source.settings->fadeOut;
    destination.depthFadeSoftness = source.settings->depthFadeSoftness;
}

Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

float SmoothStep(float t) {
    t = (std::clamp)(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::filesystem::path NormalizeEffectPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        normalized = std::filesystem::absolute(path, error);
        if (error) {
            normalized = path;
        }
    }
    return normalized.lexically_normal();
}

bool IsLoadedEffectPath(
    const std::vector<LoadedEffectAsset>& loadedAssets,
    const std::filesystem::path& path) {
    const std::filesystem::path normalizedPath = NormalizeEffectPath(path);
    for (const LoadedEffectAsset& loaded : loadedAssets) {
        if (NormalizeEffectPath(loaded.path) == normalizedPath) {
            return true;
        }
    }
    return false;
}

AppVfxRuntimeState::IceProjectileShotState* FindReusableIceProjectileShot(
    EffectRuntime& effectRuntime,
    AppVfxRuntimeState& runtimeState) {
    for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState.iceProjectileShots) {
        if (!shot.active) {
            return &shot;
        }
    }

    AppVfxRuntimeState::IceProjectileShotState* oldest = &runtimeState.iceProjectileShots.front();
    for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState.iceProjectileShots) {
        if (shot.timer > oldest->timer) {
            oldest = &shot;
        }
    }
    if (oldest->instanceId != 0) {
        effectRuntime.StopEffect(oldest->instanceId);
    }
    *oldest = {};
    return oldest;
}

void EnqueueIceProjectileShot(
    EffectRuntime& effectRuntime,
    AppVfxRuntimeState& runtimeState,
    const Vector3& start,
    const Vector3& target) {
    AppVfxRuntimeState::IceProjectileShotState* shot =
        FindReusableIceProjectileShot(effectRuntime, runtimeState);
    if (shot == nullptr) {
        return;
    }

    *shot = {};
    shot->active = true;
    shot->start = start;
    shot->target = target;
}

void UpdateIceProjectilePreview(
    EffectRuntime& effectRuntime,
    AppVfxRuntimeState& runtimeState,
    float deltaTime) {
    bool hasActiveShot = runtimeState.iceProjectilePreviewActive;
    for (const AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState.iceProjectileShots) {
        hasActiveShot = hasActiveShot || shot.active;
    }
    if (!hasActiveShot) {
        return;
    }

    runtimeState.enableParticles = true;
    runtimeState.enableRings = true;
    runtimeState.enableCylinders = true;
    runtimeState.enableTrails = true;

    if (runtimeState.iceProjectilePreviewActive) {
        EnqueueIceProjectileShot(
            effectRuntime,
            runtimeState,
            runtimeState.iceProjectileStart,
            runtimeState.iceProjectileTarget);
        runtimeState.iceProjectilePreviewActive = false;
        runtimeState.iceProjectileInstanceId = 0;
        runtimeState.iceProjectileTimer = 0.0f;
        runtimeState.iceProjectileImpactSpawned = false;
    }

    for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState.iceProjectileShots) {
        if (!shot.active) {
            continue;
        }

        Vector3 start = shot.start;
        Vector3 end = shot.target;
        if (!shot.useWorldSpace && (std::abs(start.z - end.z) < 0.05f || start.z > -1.4f)) {
            start.z = -3.05f;
            end.z = 0.42f;
        }
        const float travelDuration = (std::max)(0.05f, shot.travelDuration);
        const float cleanupDelay = (std::max)(travelDuration, shot.cleanupDelay);

        shot.timer += (std::max)(0.0f, deltaTime);
        const float localTimer = shot.timer - (std::max)(0.0f, shot.launchDelay);
        if (localTimer < 0.0f) {
            continue;
        }

        const EffectInstance* projectile = effectRuntime.FindInstance(shot.instanceId);
        if (projectile == nullptr && localTimer <= (std::max)(0.0f, deltaTime)) {
            shot.instanceId = effectRuntime.PlayEffectWithParams(
                "ice_projectile",
                start,
                {0.82f, 0.95f, 1.0f, 1.0f},
                {1.0f, 1.0f, 1.0f});
            projectile = effectRuntime.FindInstance(shot.instanceId);
        }

        const float travelT = (std::clamp)(localTimer / travelDuration, 0.0f, 1.0f);
        const float easedT = travelT * travelT * (2.15f - 1.15f * travelT);
        Vector3 position = LerpVector3(start, end, easedT);
        position.y += std::sin(travelT * 3.14159265f) * 0.05f;

        if (projectile != nullptr) {
            Transform transform = projectile->transform;
            transform.translate = position;
            transform.rotate.z = shot.hasExplicitRotationZ
                ? shot.rotationZ
                : std::atan2(end.y - start.y, end.x - start.x);
            transform.rotate.y = -0.34f;
            const float depthScale = shot.useWorldSpace
                ? (std::max)(0.1f, shot.visualScale)
                : 2.05f + (0.58f - 2.05f) * easedT;
            const Vector3 assetScale = projectile->asset != nullptr
                ? projectile->asset->size
                : Vector3{1.0f, 1.0f, 1.0f};
            transform.scale = {
                assetScale.x * depthScale,
                assetScale.y * depthScale,
                assetScale.z * depthScale,
            };
            (void)effectRuntime.SetInstanceAppearance(shot.instanceId, transform, projectile->color, projectile->attached);
        }

        if (travelT >= 1.0f && !shot.impactSpawned) {
            Vector3 impactPosition = end;
            impactPosition.z -= 0.28f;
            effectRuntime.PlayEffectWithParams(
                "ice_impact",
                impactPosition,
                {0.72f, 0.92f, 1.0f, 1.0f},
                {
                    (std::max)(0.1f, shot.impactScale),
                    (std::max)(0.1f, shot.impactScale),
                    (std::max)(0.1f, shot.impactScale),
                });
            if (shot.instanceId != 0) {
                effectRuntime.StopEffect(shot.instanceId);
                shot.instanceId = 0;
            }
            shot.impactSpawned = true;
        }

        if (localTimer >= cleanupDelay) {
            if (shot.instanceId != 0) {
                effectRuntime.StopEffect(shot.instanceId);
            }
            shot = {};
        }
    }
}

void PreserveParticleLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    std::vector<ParticleComponentAsset> replacements;
    ForEachParticleComponent(reloadedAsset.Components().ParticleStorageView(), [&currentAsset, &replacements](const ParticleComponentAssetView& reloadedParticle) {
        bool applied = false;
        ForEachParticleComponent(currentAsset.Components().ParticleStorageView(), [&applied, &reloadedParticle, &replacements](const ParticleComponentAssetView& currentParticle) {
            if (applied) {
                return;
            }
            if (IsSameAuthoredComponent(currentParticle.common, reloadedParticle.common)) {
                ParticleComponentAsset replacement{*reloadedParticle.common, *reloadedParticle.settings};
                ApplyLiveTuningToComponent(currentParticle, replacement.settings);
                replacements.push_back(replacement);
                applied = true;
            }
        });
    });

    const MutableParticleComponentStorageView storage = reloadedAsset.MutableComponents().MutableParticleStorageView();
    for (const ParticleComponentAsset& replacement : replacements) {
        ReplaceParticleComponentAndSyncPacked(storage, replacement);
    }
}

void PreserveTrailLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    std::vector<TrailComponentAsset> replacements;
    ForEachTrailComponent(reloadedAsset.Components().TrailStorageView(), [&currentAsset, &replacements](const TrailComponentAssetView& reloadedTrail) {
        bool applied = false;
        ForEachTrailComponent(currentAsset.Components().TrailStorageView(), [&applied, &reloadedTrail, &replacements](const TrailComponentAssetView& currentTrail) {
            if (applied) {
                return;
            }
            if (IsSameAuthoredComponent(currentTrail.common, reloadedTrail.common)) {
                TrailComponentAsset replacement{*reloadedTrail.common, *reloadedTrail.settings};
                ApplyLiveTuningToComponent(currentTrail, replacement.settings);
                replacements.push_back(replacement);
                applied = true;
            }
        });
    });

    const MutableTrailComponentStorageView storage = reloadedAsset.MutableComponents().MutableTrailStorageView();
    for (const TrailComponentAsset& replacement : replacements) {
        ReplaceTrailComponentAndSyncPacked(storage, replacement);
    }
}

void PreserveDistortionLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    std::vector<DistortionComponentAsset> replacements;
    ForEachDistortionComponent(reloadedAsset.Components().DistortionStorageView(), [&currentAsset, &replacements](const DistortionComponentAssetView& reloadedDistortion) {
        bool applied = false;
        ForEachDistortionComponent(currentAsset.Components().DistortionStorageView(), [&applied, &reloadedDistortion, &replacements](const DistortionComponentAssetView& currentDistortion) {
            if (applied) {
                return;
            }
            if (IsSameAuthoredComponent(currentDistortion.common, reloadedDistortion.common)) {
                DistortionComponentAsset replacement{*reloadedDistortion.common, *reloadedDistortion.settings};
                ApplyLiveTuningToComponent(currentDistortion, replacement.settings);
                replacements.push_back(replacement);
                applied = true;
            }
        });
    });

    const MutableDistortionComponentStorageView storage = reloadedAsset.MutableComponents().MutableDistortionStorageView();
    for (const DistortionComponentAsset& replacement : replacements) {
        ReplaceDistortionComponentAndSyncPacked(storage, replacement);
    }
}

void PreserveRingLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    std::vector<RingComponentAsset> replacements;
    ForEachRingComponent(reloadedAsset.Components().RingStorageView(), [&currentAsset, &replacements](const RingComponentAssetView& reloadedRing) {
        bool applied = false;
        ForEachRingComponent(currentAsset.Components().RingStorageView(), [&applied, &reloadedRing, &replacements](const RingComponentAssetView& currentRing) {
            if (applied) {
                return;
            }
            if (IsSameAuthoredComponent(currentRing.common, reloadedRing.common)) {
                RingComponentAsset replacement{*reloadedRing.common, *reloadedRing.settings};
                ApplyLiveTuningToComponent(currentRing, replacement.settings);
                replacements.push_back(replacement);
                applied = true;
            }
        });
    });

    const MutableRingComponentStorageView storage = reloadedAsset.MutableComponents().MutableRingStorageView();
    for (const RingComponentAsset& replacement : replacements) {
        ReplaceRingComponentAndSyncPacked(storage, replacement);
    }
}

void PreserveCylinderLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    std::vector<CylinderComponentAsset> replacements;
    ForEachCylinderComponent(reloadedAsset.Components().CylinderStorageView(), [&currentAsset, &replacements](const CylinderComponentAssetView& reloadedCylinder) {
        bool applied = false;
        ForEachCylinderComponent(currentAsset.Components().CylinderStorageView(), [&applied, &reloadedCylinder, &replacements](const CylinderComponentAssetView& currentCylinder) {
            if (applied) {
                return;
            }
            if (IsSameAuthoredComponent(currentCylinder.common, reloadedCylinder.common)) {
                CylinderComponentAsset replacement{*reloadedCylinder.common, *reloadedCylinder.settings};
                ApplyLiveTuningToComponent(currentCylinder, replacement.settings);
                replacements.push_back(replacement);
                applied = true;
            }
        });
    });

    const MutableCylinderComponentStorageView storage = reloadedAsset.MutableComponents().MutableCylinderStorageView();
    for (const CylinderComponentAsset& replacement : replacements) {
        ReplaceCylinderComponentAndSyncPacked(storage, replacement);
    }
}

void PreserveLiveTuning(
    const EffectAsset& currentAsset,
    EffectAsset& reloadedAsset) {
    reloadedAsset.defaultParticle = currentAsset.defaultParticle;
    reloadedAsset.defaultTrail = currentAsset.defaultTrail;
    reloadedAsset.defaultBeam = currentAsset.defaultBeam;
    reloadedAsset.defaultDistortion = currentAsset.defaultDistortion;
    reloadedAsset.defaultRing = currentAsset.defaultRing;
    reloadedAsset.defaultCylinder = currentAsset.defaultCylinder;

    PreserveParticleLiveTuning(currentAsset, reloadedAsset);
    PreserveTrailLiveTuning(currentAsset, reloadedAsset);
    PreserveDistortionLiveTuning(currentAsset, reloadedAsset);
    PreserveRingLiveTuning(currentAsset, reloadedAsset);
    PreserveCylinderLiveTuning(currentAsset, reloadedAsset);
}

Vector4 ResolveParticleTint(const ParticleRenderInput& input) {
    if (input.primary.instance != nullptr && input.primary.componentCommon != nullptr) {
        const EffectInstance& instance = *input.primary.instance;
        const EffectComponentCommon& component = *input.primary.componentCommon;
        return {
            instance.color.x * component.color.x,
            instance.color.y * component.color.y,
            instance.color.z * component.color.z,
            instance.color.w * component.color.w,
        };
    }

    return input.fallbackCommon != nullptr ? input.fallbackCommon->color : Vector4{1.0f, 1.0f, 1.0f, 1.0f};
}

Vector3 ResolveParticleEmitterPosition(const ParticleRenderInput& input) {
    return input.primary.instance != nullptr ? input.primary.instance->transform.translate : Vector3{};
}

float ResolveParticleLifetime(const ParticleRenderInput& input) {
    if (input.primary.componentCommon != nullptr) {
        return input.primary.componentCommon->duration;
    }
    return input.fallbackCommon != nullptr ? input.fallbackCommon->duration : 0.0f;
}

const EffectParticleSettings* ResolveParticleSettings(const ParticleRenderInput& input) {
    return input.settings != nullptr ? input.settings : input.fallbackSettings;
}
} // namespace

VfxEngine::VfxEngine() {
    postProcessStack_.ResetToVfxDefaults();
    RegisterBuiltInAssets();
    effectRuntime_.AttachSystem(&effectSystem_);
    effectRuntime_.AttachAuthoringRegistry(&effectAuthoringRegistry_);
    LoadEffectDirectory("Resources/effects");
}

void VfxEngine::RegisterBuiltInAssets() {
    EffectAsset additiveParticle{};
    additiveParticle.name = "particle_additive";
    additiveParticle.shader = "Particle";
    additiveParticle.texture = "default";
    additiveParticle.passState.blend = ge3::graphics::BlendMode::Additive;
    additiveParticle.passState.depth = ge3::graphics::DepthMode::ReadOnly;
    additiveParticle.layer = EffectLayer::AdditiveFx;
    additiveParticle.lifetime = 2.0f;
    additiveParticle.defaultParticle.emissive = 1.5f;
    additiveParticle.defaultBeam.emissive = additiveParticle.defaultParticle.emissive;
    effectSystem_.RegisterAsset(std::move(additiveParticle), effectAuthoringRegistry_);
}

void VfxEngine::LoadEffectDirectory(const char* directory) {
    loadedEffectAssets_.clear();

    const std::filesystem::path effectDirectory{directory};
    if (!std::filesystem::exists(effectDirectory)) {
        return;
    }

    const bool loadAuthoringSamples = ShouldLoadAuthoringSamples();
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(effectDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".effect") {
            continue;
        }
        if (!loadAuthoringSamples && IsAuthoringSampleEffectPath(entry.path())) {
            continue;
        }

        LoadedEffectAsset loaded{};
        if (effectAssetLoader_.LoadFile(
                entry.path(),
                loaded,
                effectAuthoringRegistry_)) {
            if (!loadAuthoringSamples && IsAuthoringSampleEffectAsset(loaded)) {
                continue;
            }
            effectSystem_.RegisterAsset(loaded.asset, effectAuthoringRegistry_);
            loadedEffectAssets_.push_back(std::move(loaded));
        }
    }
}

void VfxEngine::InitializeBeam(
    ID3D12Device* device,
    ID3D12DescriptorHeap* srvDescriptorHeap,
    uint32_t descriptorSizeSRV,
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_CPU_DESCRIPTOR_HANDLE secondaryTextureSrvHandle,
    DXGI_FORMAT rtvFormat,
    DXGI_FORMAT dsvFormat) {
    beam_.Initialize(
        device,
        srvDescriptorHeap,
        descriptorSizeSRV,
        textureSrvHandle,
        secondaryTextureSrvHandle,
        rtvFormat,
        dsvFormat);
    electricOrbStrikeRenderer_.Initialize(device, rtvFormat, dsvFormat);
}

void VfxEngine::InitializeGpuParticles(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ge3::core::DescriptorHeapSet& heaps,
    AppPipelines& pipelines) {
    gpuParticleSystem_.Initialize(
        device,
        commandList,
        heaps,
        AppGpuParticleSystem::kDefaultMaxParticles,
        pipelines.GetGpuParticleComputeRootSignature(),
        pipelines.GetGpuParticleResetComputePSO());
}

void VfxEngine::Shutdown() {
    electricOrbStrikeRenderer_.Shutdown();
    beam_.Shutdown();
}

void VfxEngine::BeginFrame() {
    effectResourceCache_.BeginFrame();
}

void VfxEngine::Update(AppVfxRuntimeState& runtimeState, float deltaTime) {
    if (runtimeState.autoPlayVfxDemo) {
        runtimeState.enableParticles = true;
        runtimeState.autoPlayVfxTimer -= deltaTime;
        runtimeState.autoPlayVfxAngle += 0.9f * deltaTime;
        if (runtimeState.autoPlayVfxTimer <= 0.0f) {
            const float radius = (std::max)(0.0f, runtimeState.autoPlayVfxRadius);
            const float angle = runtimeState.autoPlayVfxAngle;
            const Vector3 effectPosition = {
                std::cos(angle) * radius,
                std::sin(angle * 1.7f) * 0.65f,
                std::sin(angle) * radius
            };
            effectRuntime_.PlayEffectWithParams(
                "warp_core",
                effectPosition,
                {1.0f, 0.8f, 0.45f, 1.0f},
                {1.15f, 1.15f, 1.15f});
            runtimeState.autoPlayVfxTimer = (std::max)(0.1f, runtimeState.autoPlayVfxInterval);
        }
    }

    UpdateIceProjectilePreview(effectRuntime_, runtimeState, deltaTime);
    if (runtimeState.electricOrbStrikeActive) {
        const bool showcaseElectric =
            runtimeState.showcaseMode &&
            runtimeState.showcaseEffect == AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike;
        const float electricSpeed = showcaseElectric
            ? (std::max)(
                0.25f,
                runtimeState.showcaseTuning[
                    ShowcaseIndex(AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike)].param2)
            : 1.0f;
        runtimeState.electricOrbStrikeTimer += (std::max)(0.0f, deltaTime) * electricSpeed;
        const float duration = (std::max)(
            kElectricOrbStrikeMinimumDuration,
            runtimeState.electricOrbStrikeDuration);
        if (runtimeState.electricOrbStrikeTimer >= duration) {
            if (runtimeState.electricOrbStrikeLoop) {
                runtimeState.electricOrbStrikeTimer = std::fmod(runtimeState.electricOrbStrikeTimer, duration);
            } else {
                runtimeState.electricOrbStrikeActive = false;
                runtimeState.electricOrbStrikeTimer = duration;
            }
        }
    }

    beamTime_ += deltaTime;
    beam_.SetTime(beamTime_);
    postProcessStack_.UpdateWarpTunnel(deltaTime);
    postProcessStack_.UpdateDissolve(deltaTime);
    effectRuntime_.Update(deltaTime);

    if (!effectHotReloadConfigured_) {
        effectHotReloadConfigured_ = true;
        effectHotReloadEnabled_ = ReadEnvFlag(L"GE3_EFFECT_HOT_RELOAD");
    }
    if (!effectHotReloadEnabled_) {
        return;
    }

    effectAssetReloadPollTimer_ += (std::max)(0.0f, deltaTime);
    if (effectAssetReloadPollTimer_ >= kEffectAssetReloadPollInterval) {
        effectAssetReloadPollTimer_ = 0.0f;
        ReloadChangedEffectAssets();
    }
}

void VfxEngine::ReloadChangedEffectAssets() {
    for (LoadedEffectAsset& loaded : loadedEffectAssets_) {
        if (!std::filesystem::exists(loaded.path)) {
            continue;
        }

        const std::filesystem::file_time_type lastWriteTime =
            std::filesystem::last_write_time(loaded.path);
        if (lastWriteTime == loaded.lastWriteTime) {
            continue;
        }

        LoadedEffectAsset reloaded{};
        if (effectAssetLoader_.LoadFile(
                loaded.path,
                reloaded,
                effectAuthoringRegistry_)) {
            const bool shouldPreserveLiveTuning =
                reloaded.asset.name != "ice_projectile" &&
                reloaded.asset.name != "ice_impact";
            if (shouldPreserveLiveTuning) {
                if (const EffectAsset* currentAsset = effectSystem_.FindAsset(reloaded.asset.name)) {
                    PreserveLiveTuning(*currentAsset, reloaded.asset);
                }
            }
            effectSystem_.RegisterAsset(reloaded.asset, effectAuthoringRegistry_);
            loaded = std::move(reloaded);
        }
    }

    const std::filesystem::path effectDirectory{"Resources/effects"};
    if (!std::filesystem::exists(effectDirectory)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(effectDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".effect") {
            continue;
        }
        if (!ShouldLoadAuthoringSamples() && IsAuthoringSampleEffectPath(entry.path())) {
            continue;
        }
        if (IsLoadedEffectPath(loadedEffectAssets_, entry.path())) {
            continue;
        }

        LoadedEffectAsset loaded{};
        if (effectAssetLoader_.LoadFile(
                entry.path(),
                loaded,
                effectAuthoringRegistry_)) {
            if (!ShouldLoadAuthoringSamples() && IsAuthoringSampleEffectAsset(loaded)) {
                continue;
            }
            effectSystem_.RegisterAsset(loaded.asset, effectAuthoringRegistry_);
            loadedEffectAssets_.push_back(std::move(loaded));
        }
    }
}

void VfxEngine::RegisterDefaultTextures(const AppSceneResources& scene) {
    effectResourceCache_.RegisterTexture({"default", scene.textureSrvHandleCPU, scene.textureSrvHandleGPU, 1, 1, 1});
    effectResourceCache_.RegisterTexture({"monsterBall", scene.textureSrvHandleCPU2, scene.textureSrvHandleGPU2, 2, 1, 1});
    effectResourceCache_.RegisterTexture({"streakNoise", scene.textureSrvHandleCPU, scene.textureSrvHandleGPU, 1, 1, 1});
    effectResourceCache_.RegisterTexture({"circle2", scene.circle2TextureSrvHandleCPU, scene.circle2TextureSrvHandleGPU, 5, 1, 1});
    effectResourceCache_.RegisterTexture({"gradationLine", scene.gradationLineTextureSrvHandleCPU, scene.gradationLineTextureSrvHandleGPU, 6, 1, 1});
    for (const AppManagedTextureResource& texture : scene.vfxTextureLibrary) {
        effectResourceCache_.RegisterTexture({
            texture.name,
            texture.cpu,
            texture.gpu,
            texture.descriptorIndex,
            texture.width,
            texture.height
        });
        if (!texture.path.empty()) {
            effectResourceCache_.RegisterTexture({
                texture.path,
                texture.cpu,
                texture.gpu,
                texture.descriptorIndex,
                texture.width,
                texture.height
            });
        }
    }
}

void VfxEngine::RegisterRenderPasses(
    const AppFrameGraphBuilder& frameGraphBuilder,
    AppFrameGraphBuildContext graphContext,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    const AppSceneResources& scene,
    D3D12_GPU_DESCRIPTOR_HANDLE spriteTextureHandle) {
    RegisterDefaultTextures(scene);

    frameGraphEffectRuntimeFrame_ = BuildFrame();
    const bool hasAnyVfxWork = HasAnyVfxWork(graphContext, frameGraphEffectRuntimeFrame_);
    const ParticleRenderQueue& particleQueue = frameGraphEffectRuntimeFrame_.particleQueue;
    const ParticleRenderFallback primaryParticleFx =
        !particleQueue.empty() ? frameGraphEffectRuntimeFrame_.PrimaryParticleFallback()
                               : FindPrimaryParticleFallback();
    const D3D12_GPU_DESCRIPTOR_HANDLE vfxTextureHandle =
        primaryParticleFx.common != nullptr
            ? ResolveTexture(primaryParticleFx.common->texture, spriteTextureHandle)
            : spriteTextureHandle;
    const uint32_t vfxTextureDescriptorIndex =
        primaryParticleFx.common != nullptr
            ? ResolveTextureIndex(primaryParticleFx.common->texture, 1)
            : 1;

    graphContext.vfxRenderTargets = &vfxRenderTargets_;
    graphContext.gpuParticleSystem = &gpuParticleSystem_;
    graphContext.vfxRenderers = &vfxRenderers_;
    graphContext.postProcessStack = &postProcessStack_;
    graphContext.spriteTextureHandle = spriteTextureHandle;
    graphContext.vfxTextureHandle = vfxTextureHandle;
    graphContext.vfxTextureDescriptorIndex = vfxTextureDescriptorIndex;
    graphContext.effectResourceCache = &effectResourceCache_;
    graphContext.effectRuntime = &frameGraphEffectRuntimeFrame_;
    graphContext.primaryParticleFx = primaryParticleFx;
    graphContext.beamTime = beamTime_;
    const vfx::VfxTypedResourceSet selectedResources =
        resourceResolver_.SelectPassResources(graphContext.runtimeState->vfx);

    frameGraphBuilder.Build(
        graphContext,
        [selectedResources](const AppFrameGraphBuildContext& context) {
            AppVfxRenderPipeline{}.RegisterPasses(
                context,
                selectedResources);
        });
    if (!hasAnyVfxWork) {
        return;
    }
    gpuParticleSystem_.EnsureGraphBuffers(device, *graphContext.renderGraph);
    if (!gpuParticleSystem_.IsGpuManagedParticlePoolInitialized() &&
        graphContext.appPipelines != nullptr &&
        graphContext.srvDescriptorHeap != nullptr) {
        gpuParticleSystem_.ResetGpuManagedParticlePool(
            commandList,
            graphContext.srvDescriptorHeap,
            graphContext.appPipelines->GetGpuParticleComputeRootSignature(),
            graphContext.appPipelines->GetGpuParticlePoolResetComputePSO());
    }
    gpuParticleSystem_.InitializeDedicatedParticleResources(
        commandList,
        graphContext.srvDescriptorHeap,
        graphContext.appPipelines != nullptr ? graphContext.appPipelines->GetGpuParticleComputeRootSignature() : nullptr,
        graphContext.appPipelines != nullptr ? graphContext.appPipelines->GetGpuParticleResetComputePSO() : nullptr);

    const uint64_t resetSerial = effectRuntime_.ParticlePoolResetSerial();
    if (resetSerial != consumedParticlePoolResetSerial_ &&
        graphContext.appPipelines != nullptr &&
        graphContext.srvDescriptorHeap != nullptr) {
        bool consumed = false;
        const ParticleRenderInput particleInput =
            frameGraphEffectRuntimeFrame_.ParticleInput(primaryParticleFx);
        if (selectedResources.particle.simulation.usesCompute &&
            particleInput.primary.instance != nullptr) {
            const EffectParticleSettings* settings = ResolveParticleSettings(particleInput);
            const EffectComponentCommon* common = particleInput.primary.componentCommon != nullptr
                ? particleInput.primary.componentCommon
                : particleInput.fallbackCommon;
            if (std::string_view(selectedResources.particle.simulation.stateBuffer) == "ParticleState") {
                consumed |= gpuParticleSystem_.ResetGpuManagedParticlePool(
                    commandList,
                    graphContext.srvDescriptorHeap,
                    graphContext.appPipelines->GetGpuParticleComputeRootSignature(),
                    graphContext.appPipelines->GetGpuParticlePoolResetComputePSO());
            } else {
                consumed |= gpuParticleSystem_.ResetParticlePool(
                    commandList,
                    graphContext.srvDescriptorHeap,
                    graphContext.appPipelines->GetGpuParticleComputeRootSignature(),
                    graphContext.appPipelines->GetGpuParticleResetComputePSO(),
                    selectedResources.particle.simulation.stateBuffer,
                    ResolveParticleEmitterPosition(particleInput),
                    ResolveParticleLifetime(particleInput),
                    settings != nullptr ? settings->spawnRadius : 4.0f,
                    settings != nullptr ? settings->spawnCount : 0.0f,
                    settings != nullptr ? settings->randomRotation : 0.0f,
                    settings != nullptr ? settings->scaleYMin : 1.0f,
                    settings != nullptr ? settings->scaleYMax : 1.0f,
                    common != nullptr ? common->uvRect : Vector4{0.0f, 0.0f, 1.0f, 1.0f},
                    common != nullptr ? ResolveTextureIndex(common->texture, 1) : 1,
                    ResolveParticleTint(particleInput));
            }
        }
        if (selectedResources.distortion.simulation.usesCompute &&
            !frameGraphEffectRuntimeFrame_.distortionQueue.empty()) {
            consumed |= gpuParticleSystem_.ResetParticlePool(
                commandList,
                graphContext.srvDescriptorHeap,
                graphContext.appPipelines->GetGpuParticleComputeRootSignature(),
                graphContext.appPipelines->GetGpuParticleResetComputePSO(),
                selectedResources.distortion.simulation.stateBuffer);
        }
        if (selectedResources.beam.simulation.usesCompute &&
            !frameGraphEffectRuntimeFrame_.beamQueue.empty()) {
            consumed |= gpuParticleSystem_.ResetParticlePool(
                commandList,
                graphContext.srvDescriptorHeap,
                graphContext.appPipelines->GetGpuParticleComputeRootSignature(),
                graphContext.appPipelines->GetGpuParticleResetComputePSO(),
                selectedResources.beam.simulation.stateBuffer);
        }
        if (consumed ||
            (frameGraphEffectRuntimeFrame_.particleQueue.empty() &&
             frameGraphEffectRuntimeFrame_.distortionQueue.empty() &&
             frameGraphEffectRuntimeFrame_.beamQueue.empty())) {
            consumedParticlePoolResetSerial_ = resetSerial;
        }
    }
}

VfxGraphResourceStats VfxEngine::PrepareGraphResources(
    ID3D12Device* device,
    ge3::core::DescriptorHeapSet& heaps,
    ge3::resources::ResourceRegistry& resourceRegistry,
    ge3::graphics::RenderGraph& renderGraph,
    uint32_t width,
    uint32_t height) {
    const auto prepareStart = VfxPreparePerfClock::now();
    RecordVfxPrepareStage("prepare.begin");
    const auto planStart = VfxPreparePerfClock::now();
    const std::vector<ge3::graphics::TransientRenderTargetDesc> transientRenderTargetPlan =
        renderGraph.BuildTransientRenderTargetPlan();
    const double targetPlanMs = VfxPrepareElapsedMs(planStart, VfxPreparePerfClock::now());
    RecordVfxPrepareStage("prepare.afterRenderTargetPlan");

    const auto storageStart = VfxPreparePerfClock::now();
    std::unordered_set<std::string> transientTargetStorages;
    for (const auto& target : transientRenderTargetPlan) {
        if (target.transient) {
            transientTargetStorages.insert(target.storageName);
        }
    }
    const double storageSetMs = VfxPrepareElapsedMs(storageStart, VfxPreparePerfClock::now());
    RecordVfxPrepareStage("prepare.afterStorageSets");

    const auto requestStart = VfxPreparePerfClock::now();
    vfxRenderTargets_.ResetRequests();
    for (const auto& renderTarget : transientRenderTargetPlan) {
        if (renderTarget.transient) {
            vfxRenderTargets_.RequestTransientTarget(
                renderTarget,
                renderTarget.clearColor,
                renderTarget.initialState,
                renderTarget.name.find("PostColor") == 0);
            continue;
        }

        vfxRenderTargets_.RequestTarget(
            renderTarget.name,
            renderTarget.resolutionScale,
            renderTarget.format,
            renderTarget.clearColor,
            renderTarget.initialState);
    }
    const double requestMs = VfxPrepareElapsedMs(requestStart, VfxPreparePerfClock::now());
    RecordVfxPrepareStage("prepare.afterTargetRequests");

    const auto initializeStart = VfxPreparePerfClock::now();
    vfxRenderTargets_.Initialize(device, heaps, width, height);
    const double initializeMs = VfxPrepareElapsedMs(initializeStart, VfxPreparePerfClock::now());
    RecordVfxPrepareStage("prepare.afterRenderTargetInitialize");
    const auto registerStart = VfxPreparePerfClock::now();
    vfxRenderTargets_.Register(resourceRegistry);
    const double registerMs = VfxPrepareElapsedMs(registerStart, VfxPreparePerfClock::now());
    RecordVfxPrepareStage("prepare.afterRegisterTargets");

    const uint32_t transientTargetCount = static_cast<uint32_t>(std::count_if(
        transientRenderTargetPlan.begin(),
        transientRenderTargetPlan.end(),
        [](const auto& target) { return target.transient; }));
    const uint32_t transientTargetStorageCount = static_cast<uint32_t>(transientTargetStorages.size());
    const double totalMs = VfxPrepareElapsedMs(prepareStart, VfxPreparePerfClock::now());
    if (totalMs >= kVfxPrepareSpikeLogThresholdMs) {
        char line[768] = {};
        std::snprintf(
            line,
            sizeof(line),
            "[VfxPrepareSpike] totalMs=%.3f targetPlanMs=%.3f storageSetMs=%.3f requestMs=%.3f initializeMs=%.3f registerMs=%.3f planTargets=%zu transientTargets=%u transientStorages=%u size=%ux%u\n",
            totalMs,
            targetPlanMs,
            storageSetMs,
            requestMs,
            initializeMs,
            registerMs,
            transientRenderTargetPlan.size(),
            transientTargetCount,
            transientTargetStorageCount,
            width,
            height);
        WriteVfxPrepareSpikeLine(line);
    }

    return {
        transientTargetCount,
        transientTargetStorageCount,
        0,
        0
    };
}

void VfxEngine::BeginScene(
    ID3D12GraphicsCommandList* commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    vfxRenderTargets_.BeginScene(commandList, dsv);
}

void VfxEngine::RegisterGraphResources(
    ge3::graphics::RenderGraph& renderGraph,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    std::function<void(std::string_view, D3D12_RESOURCE_STATES)> onResourceStateChanged) {
    vfxRenderTargets_.RegisterGraphResources(renderGraph);
    vfxRenderTargets_.RegisterDepthBinding(renderGraph, dsv);
    gpuParticleSystem_.RegisterGraphResources(renderGraph);
    renderGraph.SetResourceStateChangedCallback(
        [this, onResourceStateChanged](std::string_view name, D3D12_RESOURCE_STATES state) {
            if (onResourceStateChanged) {
                onResourceStateChanged(name, state);
            }
            vfxRenderTargets_.SetResourceState(name, state);
            gpuParticleSystem_.SetResourceState(name, state);
        });
}

void VfxEngine::CaptureFrameTelemetry(
    ID3D12GraphicsCommandList* commandList,
    const VfxFrameTelemetryOptions& options) {
    if (!options.AnyEnabled()) {
        return;
    }
    if (options.trailMeshStream) {
        gpuParticleSystem_.CaptureTrailMeshStreamTelemetry(commandList);
    }
    if (options.particlePool) {
        gpuParticleSystem_.CaptureParticlePoolTelemetry(commandList);
    }
    if (options.particleDedicatedReadback) {
        gpuParticleSystem_.CaptureParticleDedicatedReadbackTelemetry(commandList);
    }
    if (options.distortionDedicatedReadback) {
        gpuParticleSystem_.CaptureDistortionDedicatedReadbackTelemetry(commandList);
    }
    if (options.beamDedicatedReadback) {
        gpuParticleSystem_.CaptureBeamDedicatedReadbackTelemetry(commandList);
    }
}

void VfxEngine::ResolveFrameTelemetry(const VfxFrameTelemetryOptions& options) {
    if (!options.AnyEnabled()) {
        return;
    }
    if (options.trailMeshStream) {
        gpuParticleSystem_.ResolveTrailMeshStreamTelemetry();
    }
    if (options.particlePool) {
        gpuParticleSystem_.ResolveParticlePoolTelemetry();
    }
    if (options.particleDedicatedReadback) {
        gpuParticleSystem_.ResolveParticleDedicatedReadbackTelemetry();
    }
    if (options.distortionDedicatedReadback) {
        gpuParticleSystem_.ResolveDistortionDedicatedReadbackTelemetry();
    }
    if (options.beamDedicatedReadback) {
        gpuParticleSystem_.ResolveBeamDedicatedReadbackTelemetry();
    }
}

EffectRuntimeFrame VfxEngine::BuildFrame() const {
    return effectRuntime_.BuildFrame();
}

ParticleRenderFallback VfxEngine::FindPrimaryParticleFallback() const {
    return effectRuntime_.FindPrimaryParticleFallback();
}

D3D12_GPU_DESCRIPTOR_HANDLE VfxEngine::ResolveTexture(
    std::string_view textureName,
    D3D12_GPU_DESCRIPTOR_HANDLE fallback) const {
    return effectResourceCache_.ResolveTexture(textureName, fallback);
}

uint32_t VfxEngine::ResolveTextureIndex(
    std::string_view textureName,
    uint32_t fallback) const {
    return effectResourceCache_.ResolveTextureIndex(textureName, fallback);
}
