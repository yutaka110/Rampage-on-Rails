#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "graphics/MaterialBinding.h"
#include "utils/math/MathUtils.h"

enum class EffectLayer {
    OpaqueFx,
    AdditiveFx,
    DistortionFx,
    TrailFx,
    VolumetricFx,
};

enum class EffectComponentType {
    Particle,
    Trail,
    Beam,
    Distortion,
    Ring,
    Cylinder,
};

enum class EffectTechnique {
    ParticleAdditive,
    TrailRibbon,
    BeamLightning,
    DistortionSprite,
    RingMesh,
    CylinderMesh,
};

enum class EffectRendererType {
    ParticleRenderer,
    TrailRenderer,
    BeamRenderer,
    DistortionRenderer,
    RingRenderer,
    CylinderRenderer,
};

enum class EffectSimulationType {
    CpuSpawnGpuSim,
    CpuTimeline,
    GpuSimulation,
    None,
};

enum class EffectTechniqueMetadataSource {
    Registry,
    Asset,
    Component,
};

struct EffectComponentCommon {
    uint32_t id = 0;
    EffectComponentType type = EffectComponentType::Particle;
    // Primary authoring/runtime technique key. Keep technique only as the
    // compatibility mirror while older paths are migrated to TechniqueRegistry.
    std::string techniqueId = "ParticleAdditive";
    std::string techniqueDisplayName;
    std::string techniqueCategory;
    std::string techniqueDescription;
    EffectTechniqueMetadataSource techniqueMetadataSource = EffectTechniqueMetadataSource::Registry;
    EffectTechniqueMetadataSource techniqueDisplayNameSource = EffectTechniqueMetadataSource::Registry;
    EffectTechniqueMetadataSource techniqueCategorySource = EffectTechniqueMetadataSource::Registry;
    EffectTechniqueMetadataSource techniqueDescriptionSource = EffectTechniqueMetadataSource::Registry;
    // Primary authoring/runtime routing key. Keep rendererType only as the
    // compatibility mirror while older paths are migrated to RendererRegistry.
    std::string rendererId = "ParticleRenderer";
    // Primary authoring/runtime routing key. Keep simulationType only as the
    // compatibility mirror while older paths are migrated to SimulationRegistry.
    std::string simulationId = "CpuSpawnGpuSim";
    // Compatibility mirror resolved from techniqueId for older enum-based paths.
    EffectTechnique technique = EffectTechnique::ParticleAdditive;
    // Compatibility mirror resolved from rendererId for older enum-based paths.
    EffectRendererType rendererType = EffectRendererType::ParticleRenderer;
    // Compatibility mirror resolved from simulationId for older enum-based paths.
    EffectSimulationType simulationType = EffectSimulationType::CpuSpawnGpuSim;
    std::string name;
    std::string shader;
    std::string texture;
    ge3::graphics::PassState passState{};
    EffectLayer layer = EffectLayer::AdditiveFx;
    float startTime = 0.0f;
    float duration = 1.0f;
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector3 size = {1.0f, 1.0f, 1.0f};
    Vector4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
};

struct EffectTechniqueDescriptor {
    std::string id;
    std::string displayName;
    std::string category;
    std::string description;
    // Compatibility mirror resolved from id for older enum-based paths.
    EffectTechnique technique = EffectTechnique::ParticleAdditive;
    EffectComponentType componentType = EffectComponentType::Particle;
    std::string rendererId = "ParticleRenderer";
    // Compatibility mirror resolved from rendererId for older enum-based paths.
    EffectRendererType rendererType = EffectRendererType::ParticleRenderer;
    std::string simulationId = "CpuSpawnGpuSim";
    // Compatibility mirror resolved from simulationId for older enum-based paths.
    EffectSimulationType simulationType = EffectSimulationType::CpuSpawnGpuSim;
    EffectLayer layer = EffectLayer::AdditiveFx;
    bool overrideBlend = false;
    ge3::graphics::BlendMode blend = ge3::graphics::BlendMode::Additive;
    uint32_t renderQueueOffset = 0;
};

struct EffectRendererDescriptor {
    std::string id;
    // Compatibility mirror for older enum-based APIs; runtime routing should
    // resolve by id/componentType through RendererRegistry descriptors.
    EffectRendererType rendererType = EffectRendererType::ParticleRenderer;
    EffectComponentType componentType = EffectComponentType::Particle;
};

struct EffectSimulationDescriptor {
    std::string id;
    // Compatibility mirror for older enum-based APIs; runtime routing should
    // resolve by id through SimulationRegistry descriptors.
    EffectSimulationType simulationType = EffectSimulationType::CpuSpawnGpuSim;
};

class RendererRegistry {
public:
    static const RendererRegistry& Default();

    void Register(EffectRendererDescriptor descriptor);

    const EffectRendererDescriptor* Find(std::string_view id) const;
    const EffectRendererDescriptor* FindDefaultForComponentType(EffectComponentType type) const;
    // Compatibility lookup for assets/code paths that only have the enum
    // compatibility mirror. New runtime dispatch should use id/descriptor routing.
    const EffectRendererDescriptor* FindByLegacyRendererType(EffectRendererType rendererType) const;
    void ResolveTechniqueRenderer(EffectTechniqueDescriptor& descriptor) const;
    const std::vector<EffectRendererDescriptor>& Descriptors() const;

private:
    std::vector<EffectRendererDescriptor> descriptors_;
};

class SimulationRegistry {
public:
    static const SimulationRegistry& Default();

    void Register(EffectSimulationDescriptor descriptor);

    const EffectSimulationDescriptor* Find(std::string_view id) const;
    // Compatibility lookup for assets/code paths that only have the enum
    // compatibility mirror. New runtime dispatch should use id/descriptor routing.
    const EffectSimulationDescriptor* FindByLegacySimulationType(EffectSimulationType simulationType) const;
    void ResolveTechniqueSimulation(EffectTechniqueDescriptor& descriptor) const;
    const std::vector<EffectSimulationDescriptor>& Descriptors() const;

private:
    std::vector<EffectSimulationDescriptor> descriptors_;
};

class TechniqueRegistry {
public:
    static const TechniqueRegistry& Default();

    void Register(EffectTechniqueDescriptor descriptor);

    const EffectTechniqueDescriptor* Find(std::string_view id) const;
    // Compatibility lookup for assets/code paths that only have the enum
    // compatibility mirror. New authoring/runtime routing should use techniqueId.
    const EffectTechniqueDescriptor* FindByLegacyTechnique(EffectTechnique technique) const;
    const EffectTechniqueDescriptor* FindDefaultForComponentType(EffectComponentType type) const;
    const std::vector<EffectTechniqueDescriptor>& Descriptors() const;

    void ApplyDescriptor(
        EffectComponentCommon& common,
        const EffectTechniqueDescriptor& descriptor) const;
    // Updates enum compatibility mirrors from a descriptor. Runtime/authoring paths
    // should prefer techniqueId/rendererId/simulationId.
    void UpdateCompatibilityMirrors(
        EffectComponentCommon& common,
        const EffectTechniqueDescriptor& descriptor) const;
    void ApplyRouting(EffectComponentCommon& common) const;

private:
    std::vector<EffectTechniqueDescriptor> descriptors_;
};

struct EffectParticleSettings {
    // Individual GPU particle lifetime. A value <= 0 preserves the legacy
    // behavior (component duration, then the shader's bounded fallback).
    float lifetime = 0.0f;
    float emissive = 1.0f;
    float distortionStrength = 0.0f;
    float noiseStrength = 0.0f;
    float uvScrollSpeed = 0.0f;
    float pulseSpeed = 5.0f;
    float spawnRadius = 4.0f;
    float spawnCount = 0.0f;
    float spawnFrequency = 0.0f;
    float randomRotation = 0.0f;
    float scaleYMin = 1.0f;
    float scaleYMax = 1.0f;
    float depthFadeSoftness = 0.02f;
    float edgeSoftness = 0.5f;
};

enum class EffectTrailFollowMode : uint32_t {
    InstanceForward = 0,
    InstanceUp = 1,
    WorldX = 2,
    MovementHistory = 3,
};

struct EffectTrailSettings {
    float depthFadeSoftness = 0.02f;
    float trailTailFade = 1.35f;
    float length = 1.0f;
    float width = 0.08f;
    float sampleDistance = 0.03f;
    float smoothing = 0.0f;
    float widthHead = 1.0f;
    float widthTail = 0.35f;
    float alphaTail = 0.0f;
    float miterLimit = 2.0f;
    Vector4 colorTail = {0.65f, 0.65f, 0.65f, 1.0f};
    EffectTrailFollowMode followMode = EffectTrailFollowMode::InstanceForward;
    uint32_t segmentBudget = 8;
};

struct EffectBeamSettings {
    float emissive = 1.0f;
};

struct EffectDistortionSettings {
    float strength = 0.0f;
    float noiseStrength = 0.0f;
    float uvScrollSpeed = 0.0f;
    float depthFadeSoftness = 0.02f;
    float depthAttenuation = 1.0f;
};

struct EffectRingSettings {
    uint32_t divide = 64;
    float outerRadius = 1.0f;
    float innerRadius = 0.35f;
    float emissive = 2.0f;
    float uvScrollSpeed = 0.0f;
    float expansion = 0.0f;
    float fadeOut = 1.0f;
    float depthFadeSoftness = 0.02f;
};

struct EffectCylinderSettings {
    uint32_t divide = 64;
    float topRadius = 1.0f;
    float bottomRadius = 1.0f;
    float height = 2.0f;
    float emissive = 2.0f;
    float uvScrollSpeed = 0.0f;
    float alphaReference = 0.0f;
    float fadeOut = 1.0f;
    float depthFadeSoftness = 0.02f;
};

struct EffectComponentPayload {
    using Variant = std::variant<
        EffectParticleSettings,
        EffectTrailSettings,
        EffectBeamSettings,
        EffectDistortionSettings,
        EffectRingSettings,
        EffectCylinderSettings>;

    Variant data{EffectParticleSettings{}};
};

struct EffectComponentAsset {
    // Full component storage is an authoring/registry boundary. Runtime queues,
    // renderer inputs, and renderers should consume typed views or typed items.
    EffectComponentCommon common{};
    EffectComponentPayload payload{};
};

struct ParticleComponentAsset {
    EffectComponentCommon common{};
    EffectParticleSettings settings{};
};

struct TrailComponentAsset {
    EffectComponentCommon common{};
    EffectTrailSettings settings{};
};

struct BeamComponentAsset {
    EffectComponentCommon common{};
    EffectBeamSettings settings{};
};

struct DistortionComponentAsset {
    EffectComponentCommon common{};
    EffectDistortionSettings settings{};
};

struct RingComponentAsset {
    EffectComponentCommon common{};
    EffectRingSettings settings{};
};

struct CylinderComponentAsset {
    EffectComponentCommon common{};
    EffectCylinderSettings settings{};
};

inline EffectComponentAsset ToEffectComponentAsset(const ParticleComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Particle;
    asset.payload.data = component.settings;
    return asset;
}

inline EffectComponentAsset ToEffectComponentAsset(const TrailComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Trail;
    asset.payload.data = component.settings;
    return asset;
}

inline EffectComponentAsset ToEffectComponentAsset(const BeamComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Beam;
    asset.payload.data = component.settings;
    return asset;
}

inline EffectComponentAsset ToEffectComponentAsset(const DistortionComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Distortion;
    asset.payload.data = component.settings;
    return asset;
}

inline EffectComponentAsset ToEffectComponentAsset(const RingComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Ring;
    asset.payload.data = component.settings;
    return asset;
}

inline EffectComponentAsset ToEffectComponentAsset(const CylinderComponentAsset& component) {
    EffectComponentAsset asset{};
    asset.common = component.common;
    asset.common.type = EffectComponentType::Cylinder;
    asset.payload.data = component.settings;
    return asset;
}

struct ParticleComponentStorageView;
struct MutableParticleComponentStorageView;
struct TrailComponentStorageView;
struct MutableTrailComponentStorageView;
struct BeamComponentStorageView;
struct MutableBeamComponentStorageView;
struct DistortionComponentStorageView;
struct MutableDistortionComponentStorageView;
struct RingComponentStorageView;
struct MutableRingComponentStorageView;
struct CylinderComponentStorageView;
struct MutableCylinderComponentStorageView;

class EffectAssetComponentStorage {
public:
    using Container = std::vector<EffectComponentAsset>;

    bool Empty() const { return ComponentCount() == 0; }
    std::size_t ComponentCount() const {
        return particleComponents_.size() +
               trailComponents_.size() +
               beamComponents_.size() +
               distortionComponents_.size() +
               ringComponents_.size() +
               cylinderComponents_.size();
    }
    void Reserve(std::size_t count) { packedComponents_.reserve(count); }

    EffectComponentAsset& Add(EffectComponentAsset component) {
        // Compatibility import path. New component creation should use typed Add overloads.
        MirrorTypedComponent(component);
        packedComponents_.push_back(std::move(component));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(ParticleComponentAsset component) {
        component.common.type = EffectComponentType::Particle;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        particleComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(TrailComponentAsset component) {
        component.common.type = EffectComponentType::Trail;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        trailComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(BeamComponentAsset component) {
        component.common.type = EffectComponentType::Beam;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        beamComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(DistortionComponentAsset component) {
        component.common.type = EffectComponentType::Distortion;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        distortionComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(RingComponentAsset component) {
        component.common.type = EffectComponentType::Ring;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        ringComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    EffectComponentAsset& Add(CylinderComponentAsset component) {
        component.common.type = EffectComponentType::Cylinder;
        EffectComponentAsset packed = ToEffectComponentAsset(component);
        cylinderComponents_.push_back(std::move(component));
        packedComponents_.push_back(std::move(packed));
        return packedComponents_.back();
    }

    bool ReplaceParticleComponentAndSyncPacked(ParticleComponentAsset component) {
        component.common.type = EffectComponentType::Particle;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(particleComponents_, component);
        RemoveTypedComponent(trailComponents_, component.common.id);
        RemoveTypedComponent(beamComponents_, component.common.id);
        RemoveTypedComponent(distortionComponents_, component.common.id);
        RemoveTypedComponent(ringComponents_, component.common.id);
        RemoveTypedComponent(cylinderComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceTrailComponentAndSyncPacked(TrailComponentAsset component) {
        component.common.type = EffectComponentType::Trail;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(trailComponents_, component);
        RemoveTypedComponent(particleComponents_, component.common.id);
        RemoveTypedComponent(beamComponents_, component.common.id);
        RemoveTypedComponent(distortionComponents_, component.common.id);
        RemoveTypedComponent(ringComponents_, component.common.id);
        RemoveTypedComponent(cylinderComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceBeamComponentAndSyncPacked(BeamComponentAsset component) {
        component.common.type = EffectComponentType::Beam;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(beamComponents_, component);
        RemoveTypedComponent(particleComponents_, component.common.id);
        RemoveTypedComponent(trailComponents_, component.common.id);
        RemoveTypedComponent(distortionComponents_, component.common.id);
        RemoveTypedComponent(ringComponents_, component.common.id);
        RemoveTypedComponent(cylinderComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceDistortionComponentAndSyncPacked(DistortionComponentAsset component) {
        component.common.type = EffectComponentType::Distortion;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(distortionComponents_, component);
        RemoveTypedComponent(particleComponents_, component.common.id);
        RemoveTypedComponent(trailComponents_, component.common.id);
        RemoveTypedComponent(beamComponents_, component.common.id);
        RemoveTypedComponent(ringComponents_, component.common.id);
        RemoveTypedComponent(cylinderComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceRingComponentAndSyncPacked(RingComponentAsset component) {
        component.common.type = EffectComponentType::Ring;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(ringComponents_, component);
        RemoveTypedComponent(particleComponents_, component.common.id);
        RemoveTypedComponent(trailComponents_, component.common.id);
        RemoveTypedComponent(beamComponents_, component.common.id);
        RemoveTypedComponent(distortionComponents_, component.common.id);
        RemoveTypedComponent(cylinderComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceCylinderComponentAndSyncPacked(CylinderComponentAsset component) {
        component.common.type = EffectComponentType::Cylinder;
        EffectComponentAsset* packedComponent = Find(component.common.id);
        if (packedComponent == nullptr) {
            return false;
        }
        ReplaceTypedComponent(cylinderComponents_, component);
        RemoveTypedComponent(particleComponents_, component.common.id);
        RemoveTypedComponent(trailComponents_, component.common.id);
        RemoveTypedComponent(beamComponents_, component.common.id);
        RemoveTypedComponent(distortionComponents_, component.common.id);
        RemoveTypedComponent(ringComponents_, component.common.id);
        *packedComponent = ToEffectComponentAsset(component);
        return true;
    }

private:
    friend class EffectSystem;
    friend class EffectAssetLoader;

    bool ReplaceParticleComponentAtForAuthoring(
        std::size_t index,
        ParticleComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Particle;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceTrailComponentAtForAuthoring(
        std::size_t index,
        TrailComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Trail;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceBeamComponentAtForAuthoring(
        std::size_t index,
        BeamComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Beam;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceDistortionComponentAtForAuthoring(
        std::size_t index,
        DistortionComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Distortion;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceRingComponentAtForAuthoring(
        std::size_t index,
        RingComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Ring;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

    bool ReplaceCylinderComponentAtForAuthoring(
        std::size_t index,
        CylinderComponentAsset component) {
        if (index >= packedComponents_.size()) {
            return false;
        }
        component.common.type = EffectComponentType::Cylinder;
        packedComponents_[index] = ToEffectComponentAsset(component);
        return true;
    }

public:
    ParticleComponentStorageView ParticleStorageView() const;
    MutableParticleComponentStorageView MutableParticleStorageView();
    TrailComponentStorageView TrailStorageView() const;
    MutableTrailComponentStorageView MutableTrailStorageView();
    BeamComponentStorageView BeamStorageView() const;
    MutableBeamComponentStorageView MutableBeamStorageView();
    DistortionComponentStorageView DistortionStorageView() const;
    MutableDistortionComponentStorageView MutableDistortionStorageView();
    RingComponentStorageView RingStorageView() const;
    MutableRingComponentStorageView MutableRingStorageView();
    CylinderComponentStorageView CylinderStorageView() const;
    MutableCylinderComponentStorageView MutableCylinderStorageView();

    template <typename Visitor>
    void ForEachComponentCommon(Visitor&& visitor) const {
        for (const ParticleComponentAsset& component : particleComponents_) {
            visitor(component.common);
        }
        for (const TrailComponentAsset& component : trailComponents_) {
            visitor(component.common);
        }
        for (const BeamComponentAsset& component : beamComponents_) {
            visitor(component.common);
        }
        for (const DistortionComponentAsset& component : distortionComponents_) {
            visitor(component.common);
        }
        for (const RingComponentAsset& component : ringComponents_) {
            visitor(component.common);
        }
        for (const CylinderComponentAsset& component : cylinderComponents_) {
            visitor(component.common);
        }
    }

private:
    bool HasPackedComponentsForNormalization() const { return !packedComponents_.empty(); }
    std::size_t PackedComponentCountForNormalization() const { return packedComponents_.size(); }
    const EffectComponentAsset& PackedComponentAtForNormalization(std::size_t index) const {
        return packedComponents_.at(index);
    }

    void SyncTypedStorageFromPackedForNormalization() {
        particleComponents_.clear();
        trailComponents_.clear();
        beamComponents_.clear();
        distortionComponents_.clear();
        ringComponents_.clear();
        cylinderComponents_.clear();

        for (const EffectComponentAsset& component : packedComponents_) {
            MirrorTypedComponent(component);
        }
    }

    EffectComponentAsset* Find(uint32_t componentId) {
        for (EffectComponentAsset& component : packedComponents_) {
            if (component.common.id == componentId) {
                return &component;
            }
        }
        return nullptr;
    }
    template <typename TypedComponent>
    static void ReplaceTypedComponent(
        std::vector<TypedComponent>& components,
        TypedComponent component) {
        for (TypedComponent& typedComponent : components) {
            if (typedComponent.common.id == component.common.id) {
                typedComponent = std::move(component);
                return;
            }
        }
        components.push_back(std::move(component));
    }

    template <typename TypedComponent>
    static void RemoveTypedComponent(
        std::vector<TypedComponent>& components,
        uint32_t componentId) {
        for (auto it = components.begin(); it != components.end(); ++it) {
            if (it->common.id == componentId) {
                components.erase(it);
                return;
            }
        }
    }

    void MirrorTypedComponent(const EffectComponentAsset& component) {
        switch (component.common.type) {
        case EffectComponentType::Particle:
            if (const auto* settings = std::get_if<EffectParticleSettings>(&component.payload.data)) {
                particleComponents_.push_back({component.common, *settings});
            }
            break;
        case EffectComponentType::Trail:
            if (const auto* settings = std::get_if<EffectTrailSettings>(&component.payload.data)) {
                trailComponents_.push_back({component.common, *settings});
            }
            break;
        case EffectComponentType::Beam:
            if (const auto* settings = std::get_if<EffectBeamSettings>(&component.payload.data)) {
                beamComponents_.push_back({component.common, *settings});
            }
            break;
        case EffectComponentType::Distortion:
            if (const auto* settings = std::get_if<EffectDistortionSettings>(&component.payload.data)) {
                distortionComponents_.push_back({component.common, *settings});
            }
            break;
        case EffectComponentType::Ring:
            if (const auto* settings = std::get_if<EffectRingSettings>(&component.payload.data)) {
                ringComponents_.push_back({component.common, *settings});
            }
            break;
        case EffectComponentType::Cylinder:
            if (const auto* settings = std::get_if<EffectCylinderSettings>(&component.payload.data)) {
                cylinderComponents_.push_back({component.common, *settings});
            }
            break;
        }
    }

    Container packedComponents_;
    std::vector<ParticleComponentAsset> particleComponents_;
    std::vector<TrailComponentAsset> trailComponents_;
    std::vector<BeamComponentAsset> beamComponents_;
    std::vector<DistortionComponentAsset> distortionComponents_;
    std::vector<RingComponentAsset> ringComponents_;
    std::vector<CylinderComponentAsset> cylinderComponents_;
};

struct EffectAsset {
    std::string name;
    std::string shader;
    std::string texture;
    std::string techniqueDisplayName;
    std::string techniqueCategory;
    std::string techniqueDescription;
    bool techniqueMetadataOverridden = false;
    bool techniqueDisplayNameOverridden = false;
    bool techniqueCategoryOverridden = false;
    bool techniqueDescriptionOverridden = false;
    ge3::graphics::PassState passState{};
    EffectLayer layer = EffectLayer::AdditiveFx;
    float lifetime = 1.0f;
    EffectParticleSettings defaultParticle{};
    EffectTrailSettings defaultTrail{};
    EffectBeamSettings defaultBeam{};
    EffectDistortionSettings defaultDistortion{};
    EffectRingSettings defaultRing{};
    EffectCylinderSettings defaultCylinder{};
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector3 size = {1.0f, 1.0f, 1.0f};
    Vector4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
    bool Validate(std::string* errorMessage = nullptr) const;
    const EffectAssetComponentStorage& Components() const { return components_; }
    // Mutable storage is restricted to asset construction/normalization and
    // typed replacement APIs. Runtime/render paths should use Components().
    EffectAssetComponentStorage& MutableComponents() { return components_; }

private:
    EffectAssetComponentStorage components_;
};

#include "EffectComponentViews.h"

struct EffectComponentInstance {
    uint32_t componentId = 0;
    float age = 0.0f;
    bool active = true;
};

inline constexpr uint32_t kEffectTrailHistoryCapacity = 256;

struct EffectInstance {
    uint32_t id = 0;
    std::string assetName;
    const EffectAsset* asset = nullptr;
    std::vector<EffectComponentInstance> components;
    Transform transform{};
    Vector3 previousPosition{};
    Vector3 velocity{};
    Vector3 trailHistory[kEffectTrailHistoryCapacity]{};
    uint32_t trailHistoryHead = 0;
    uint32_t trailHistoryCount = 0;
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    float age = 0.0f;
    bool attached = false;
    bool previewLoop = false;
};

struct EffectEvent {
    std::string effectName;
    Transform transform{};
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct EffectRenderItemCommon {
    const EffectAsset* asset = nullptr;
    const EffectComponentCommon* componentCommon = nullptr;
    const EffectRendererDescriptor* rendererDescriptor = nullptr;
    const EffectSimulationDescriptor* simulationDescriptor = nullptr;
    const EffectInstance* instance = nullptr;
    const EffectComponentInstance* componentInstance = nullptr;
    uint32_t renderQueue = 0;
    float normalizedAge = 0.0f;
};

struct ParticleRenderItem {
    EffectRenderItemCommon common{};
    const EffectParticleSettings* settings = nullptr;
};

struct TrailRenderItem {
    EffectRenderItemCommon common{};
    const EffectTrailSettings* settings = nullptr;
};

struct BeamRenderItem {
    EffectRenderItemCommon common{};
    const EffectBeamSettings* settings = nullptr;
};

struct DistortionRenderItem {
    EffectRenderItemCommon common{};
    const EffectDistortionSettings* settings = nullptr;
};

struct RingRenderItem {
    EffectRenderItemCommon common{};
    const EffectRingSettings* settings = nullptr;
};

struct CylinderRenderItem {
    EffectRenderItemCommon common{};
    const EffectCylinderSettings* settings = nullptr;
};

struct ParticleRenderFallback {
    const EffectComponentCommon* common = nullptr;
    const EffectParticleSettings* settings = nullptr;
};

using ParticleRenderQueue = std::vector<ParticleRenderItem>;
using TrailRenderQueue = std::vector<TrailRenderItem>;
using BeamRenderQueue = std::vector<BeamRenderItem>;
using DistortionRenderQueue = std::vector<DistortionRenderItem>;
using RingRenderQueue = std::vector<RingRenderItem>;
using CylinderRenderQueue = std::vector<CylinderRenderItem>;

struct EffectRuntimeQueueAuthoringStatus {
    bool hasPrimary = false;
    std::string rendererId;
    bool rendererResolved = false;
    EffectRendererType rendererType = EffectRendererType::ParticleRenderer;
    std::string rendererDescriptorId;
    std::string techniqueId;
    std::string simulationId;
    bool simulationResolved = false;
    EffectSimulationType simulationType = EffectSimulationType::CpuSpawnGpuSim;
    std::string simulationDescriptorId;
    uint32_t renderQueue = 0;
};

struct EffectRuntimeAuthoringFrame {
    EffectRuntimeQueueAuthoringStatus particle;
    EffectRuntimeQueueAuthoringStatus trail;
    EffectRuntimeQueueAuthoringStatus beam;
    EffectRuntimeQueueAuthoringStatus distortion;
    EffectRuntimeQueueAuthoringStatus ring;
    EffectRuntimeQueueAuthoringStatus cylinder;
};

struct ParticleRenderInput;
struct TrailRenderInput;
struct BeamRenderInput;
struct DistortionRenderInput;
struct RingRenderInput;
struct CylinderRenderInput;
class EffectAuthoringRegistry;

struct EffectRuntimeFrame {
    ParticleRenderQueue particleQueue;
    TrailRenderQueue trailQueue;
    BeamRenderQueue beamQueue;
    DistortionRenderQueue distortionQueue;
    RingRenderQueue ringQueue;
    CylinderRenderQueue cylinderQueue;
    EffectRuntimeAuthoringFrame authoring;
    uint32_t activeInstanceCount = 0;
    uint32_t activeComponentCount = 0;

    void Clear();
    ParticleRenderFallback PrimaryParticleFallback() const;
    ParticleRenderInput ParticleInput(const ParticleRenderFallback& fallback) const;
    TrailRenderInput TrailInput() const;
    BeamRenderInput BeamInput() const;
    DistortionRenderInput DistortionInput() const;
    RingRenderInput RingInput() const;
    CylinderRenderInput CylinderInput() const;
};

class EffectSystem {
public:
    // Compatibility fallback for standalone callers. VfxEngine should register
    // assets with its owned EffectAuthoringRegistry.
    bool RegisterAsset(EffectAsset asset);
    bool RegisterAsset(
        EffectAsset asset,
        const EffectAuthoringRegistry& authoringRegistry);
    const EffectAsset* FindAsset(std::string_view name) const;

    uint32_t PlayEffect(std::string_view name, const Vector3& position);
    uint32_t PlayEffectWithParams(
        std::string_view name,
        const Vector3& position,
        const Vector4& color,
        const Vector3& scale);
    void StopEffect(uint32_t id);
    void Update(float deltaTime);
    void ClearInstances();
    const EffectInstance* FindInstance(uint32_t id) const;
    void SetEffectPreviewLoop(uint32_t id, bool enabled);
    void RestartInstance(uint32_t id);
    void SetInstanceAge(uint32_t id, float age);
    bool SetInstanceAppearance(uint32_t id, const Transform& transform, const Vector4& color, bool attached = false);
    bool MoveInstance(uint32_t id, const Vector3& position, bool resetVelocity = false);
    bool ReplaceAssets(std::unordered_map<std::string, EffectAsset> assets,
        const EffectAuthoringRegistry& authoringRegistry, std::string* errorMessage = nullptr);
    static bool ValidateAssets(const std::unordered_map<std::string, EffectAsset>& assets, std::string* errorMessage = nullptr);
    uint64_t ParticlePoolResetSerial() const { return particlePoolResetSerial_; }

    const std::vector<EffectInstance>& Instances() const { return instances_; }
    const std::unordered_map<std::string, EffectAsset>& Assets() const { return assets_; }

private:
    EffectInstance* FindMutableInstance(uint32_t id);
    void RebindInstances();
    static void EnsureDefaultComponent(EffectAsset& asset);
    static void EnsureDefaultComponent(
        EffectAsset& asset,
        const EffectAuthoringRegistry& authoringRegistry);

    uint32_t nextInstanceId_ = 1;
    uint64_t particlePoolResetSerial_ = 0;
    std::unordered_map<std::string, EffectAsset> assets_;
    std::vector<EffectInstance> instances_;
};
