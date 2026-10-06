#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "EffectSystem.h"
#include "utils/math/MathUtils.h"

class EffectAuthoringRegistry;
struct EffectRendererDescriptor;
struct EffectSimulationDescriptor;

class EffectRuntime {
public:
    explicit EffectRuntime(EffectSystem* effectSystem = nullptr);

    void AttachSystem(EffectSystem* effectSystem);
    void AttachAuthoringRegistry(const EffectAuthoringRegistry* authoringRegistry);
    bool IsAttached() const;

    uint32_t PlayEffect(std::string_view name, const Vector3& position);
    uint32_t PlayEffectWithParams(
        std::string_view name,
        const Vector3& position,
        const Vector4& color,
        const Vector3& scale);
    void StopEffect(uint32_t id);
    void RestartInstance(uint32_t id);
    void SetInstanceAge(uint32_t id, float age);
    void SetEffectPreviewLoop(uint32_t id, bool enabled);
    const EffectInstance* FindInstance(uint32_t id) const;
    void ClearInstances();

    void Update(float deltaTime);
    EffectRuntimeFrame BuildFrame() const;
    ParticleRenderFallback FindPrimaryParticleFallback() const;

    void SetPaused(bool paused);
    bool IsPaused() const;
    void SetSpeedMultiplier(float speedMultiplier);
    float SpeedMultiplier() const;

    const std::unordered_map<std::string, EffectAsset>& Assets() const;
    bool RegisterAsset(EffectAsset asset, std::string* errorMessage = nullptr);
    bool ValidateAssetReplacement(const std::unordered_map<std::string, EffectAsset>& assets, std::string* errorMessage = nullptr) const;
    bool ReplaceAssets(std::unordered_map<std::string, EffectAsset> assets, std::string* errorMessage = nullptr);
    bool SetInstanceAppearance(uint32_t id, const Transform& transform, const Vector4& color, bool attached = false);
    bool MoveInstance(uint32_t id, const Vector3& position, bool resetVelocity = false);
    const std::vector<EffectInstance>& Instances() const;
    uint64_t ParticlePoolResetSerial() const;

private:
    struct ActiveComponentCore {
        EffectRenderItemCommon common{};
    };

    struct ParticleActiveComponent {
        EffectRenderItemCommon common{};
        const EffectParticleSettings* settings = nullptr;
    };

    struct TrailActiveComponent {
        EffectRenderItemCommon common{};
        const EffectTrailSettings* settings = nullptr;
    };

    struct BeamActiveComponent {
        EffectRenderItemCommon common{};
        const EffectBeamSettings* settings = nullptr;
    };

    struct DistortionActiveComponent {
        EffectRenderItemCommon common{};
        const EffectDistortionSettings* settings = nullptr;
    };

    struct RingActiveComponent {
        EffectRenderItemCommon common{};
        const EffectRingSettings* settings = nullptr;
    };

    struct CylinderActiveComponent {
        EffectRenderItemCommon common{};
        const EffectCylinderSettings* settings = nullptr;
    };

    void ForEachActiveInstanceComponent(
        const std::function<void(const EffectInstance&, const EffectComponentInstance&)>& visitor) const;
    static bool BuildActiveComponentCore(
        const EffectInstance& instance,
        const EffectComponentInstance& componentInstance,
        const EffectComponentCommon& componentCommon,
        const EffectRendererDescriptor* rendererDescriptor,
        const EffectSimulationDescriptor* simulationDescriptor,
        ActiveComponentCore& outCore);
    void ForEachParticleAssetComponent(
        const std::function<void(const ActiveComponentCore&, const ParticleComponentAssetView&)>& visitor) const;
    void ForEachTrailAssetComponent(
        const std::function<void(const ActiveComponentCore&, const TrailComponentAssetView&)>& visitor) const;
    void ForEachBeamAssetComponent(
        const std::function<void(const ActiveComponentCore&, const BeamComponentAssetView&)>& visitor) const;
    void ForEachDistortionAssetComponent(
        const std::function<void(const ActiveComponentCore&, const DistortionComponentAssetView&)>& visitor) const;
    void ForEachRingAssetComponent(
        const std::function<void(const ActiveComponentCore&, const RingComponentAssetView&)>& visitor) const;
    void ForEachCylinderAssetComponent(
        const std::function<void(const ActiveComponentCore&, const CylinderComponentAssetView&)>& visitor) const;
    void ForEachParticleComponent(const std::function<void(const ParticleActiveComponent&)>& visitor) const;
    void ForEachTrailComponent(const std::function<void(const TrailActiveComponent&)>& visitor) const;
    void ForEachBeamComponent(const std::function<void(const BeamActiveComponent&)>& visitor) const;
    void ForEachDistortionComponent(const std::function<void(const DistortionActiveComponent&)>& visitor) const;
    void ForEachRingComponent(const std::function<void(const RingActiveComponent&)>& visitor) const;
    void ForEachCylinderComponent(const std::function<void(const CylinderActiveComponent&)>& visitor) const;
    ParticleRenderQueue BuildParticleQueue() const;
    TrailRenderQueue BuildTrailQueue() const;
    BeamRenderQueue BuildBeamQueue() const;
    DistortionRenderQueue BuildDistortionQueue() const;
    RingRenderQueue BuildRingQueue() const;
    CylinderRenderQueue BuildCylinderQueue() const;
    const EffectAuthoringRegistry& AuthoringRegistry() const;

    EffectSystem* effectSystem_ = nullptr;
    const EffectAuthoringRegistry* authoringRegistry_ = nullptr;
    bool paused_ = false;
    float speedMultiplier_ = 1.0f;
};
