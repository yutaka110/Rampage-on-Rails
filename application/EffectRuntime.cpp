#include "EffectRuntime.h"
#include "EffectAuthoringRegistry.h"
#include "RendererRegistry.h"
#include "SimulationRegistry.h"

#include <algorithm>

namespace {
struct RuntimeComponentRoute {
    const EffectRendererDescriptor* renderer = nullptr;
    const EffectSimulationDescriptor* simulation = nullptr;
    EffectComponentType componentType = EffectComponentType::Particle;
    bool rendererResolved = false;
    bool simulationResolved = false;
};

const std::unordered_map<std::string, EffectAsset>& EmptyAssets() {
    static const std::unordered_map<std::string, EffectAsset> assets;
    return assets;
}


const std::vector<EffectInstance>& EmptyInstances() {
    static const std::vector<EffectInstance> instances;
    return instances;
}


template <typename Queue>
void SortRenderQueue(Queue& queue) {
    std::sort(
        queue.begin(),
        queue.end(),
        [](const auto& left, const auto& right) {
            if (left.common.renderQueue != right.common.renderQueue) {
                return left.common.renderQueue < right.common.renderQueue;
            }
            const uint32_t leftId = left.common.instance != nullptr ? left.common.instance->id : 0;
            const uint32_t rightId = right.common.instance != nullptr ? right.common.instance->id : 0;
            return leftId < rightId;
        });
}

const EffectRendererDescriptor* ResolveRendererDescriptor(
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    const RendererRegistry& registry = authoringRegistry.Renderers();
    if (!common.rendererId.empty()) {
        if (const EffectRendererDescriptor* descriptor = registry.Find(common.rendererId)) {
            return descriptor;
        }
    }
    if (!common.techniqueId.empty()) {
        if (const EffectTechniqueDescriptor* technique = authoringRegistry.Techniques().Find(common.techniqueId)) {
            if (const EffectRendererDescriptor* descriptor = registry.Find(technique->rendererId)) {
                return descriptor;
            }
        }
    }
    if (const EffectTechniqueDescriptor* technique =
            authoringRegistry.Techniques().FindDefaultForComponentType(common.type)) {
        if (const EffectRendererDescriptor* descriptor = registry.Find(technique->rendererId)) {
            return descriptor;
        }
    }
    return registry.FindDefaultForComponentType(common.type);
}

const EffectSimulationDescriptor* ResolveSimulationDescriptor(
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    const SimulationRegistry& registry = authoringRegistry.Simulations();
    if (!common.simulationId.empty()) {
        if (const EffectSimulationDescriptor* descriptor = registry.Find(common.simulationId)) {
            return descriptor;
        }
    }
    if (!common.techniqueId.empty()) {
        if (const EffectTechniqueDescriptor* technique = authoringRegistry.Techniques().Find(common.techniqueId)) {
            if (const EffectSimulationDescriptor* descriptor = registry.Find(technique->simulationId)) {
                return descriptor;
            }
        }
    }
    if (const EffectTechniqueDescriptor* technique =
            authoringRegistry.Techniques().FindDefaultForComponentType(common.type)) {
        if (const EffectSimulationDescriptor* descriptor = registry.Find(technique->simulationId)) {
            return descriptor;
        }
    }
    return registry.Find("None");
}

RuntimeComponentRoute ResolveRuntimeComponentRoute(
    const EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    RuntimeComponentRoute route{};
    route.renderer = ResolveRendererDescriptor(common, authoringRegistry);
    route.simulation = ResolveSimulationDescriptor(common, authoringRegistry);
    route.rendererResolved = route.renderer != nullptr;
    route.simulationResolved = route.simulation != nullptr;
    route.componentType = route.renderer != nullptr
        ? route.renderer->componentType
        : common.type;
    return route;
}

bool ComponentRoutesToRenderer(
    const RuntimeComponentRoute& route,
    EffectComponentType expectedComponentType) {
    if (route.rendererResolved) {
        return route.componentType == expectedComponentType;
    }
    return false;
}

EffectRuntimeQueueAuthoringStatus BuildRuntimeQueueAuthoringStatus(
    const EffectComponentCommon* common,
    const EffectAuthoringRegistry& authoringRegistry) {
    EffectRuntimeQueueAuthoringStatus status{};
    if (common == nullptr) {
        return status;
    }

    status.hasPrimary = true;
    status.rendererId = common->rendererId;
    status.techniqueId = common->techniqueId;
    status.simulationId = common->simulationId;
    status.renderQueue = common->passState.renderQueue;

    const RuntimeComponentRoute route =
        ResolveRuntimeComponentRoute(*common, authoringRegistry);
    status.rendererResolved = route.rendererResolved;
    status.simulationResolved = route.simulationResolved;

    if (route.renderer != nullptr) {
        status.rendererDescriptorId = route.renderer->id;
        status.rendererType = route.renderer->rendererType;
    }

    if (route.simulation != nullptr) {
        status.simulationDescriptorId = route.simulation->id;
        status.simulationType = route.simulation->simulationType;
    }

    return status;
}

const EffectComponentCommon* PrimaryComponentCommon(const ParticleRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}

const EffectComponentCommon* PrimaryComponentCommon(const TrailRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}

const EffectComponentCommon* PrimaryComponentCommon(const BeamRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}

const EffectComponentCommon* PrimaryComponentCommon(const DistortionRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}

const EffectComponentCommon* PrimaryComponentCommon(const RingRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}

const EffectComponentCommon* PrimaryComponentCommon(const CylinderRenderQueue& queue) {
    return queue.empty() ? nullptr : queue.front().common.componentCommon;
}
} // namespace

EffectRuntime::EffectRuntime(EffectSystem* effectSystem)
    : effectSystem_(effectSystem) {
}

void EffectRuntime::AttachSystem(EffectSystem* effectSystem) {
    effectSystem_ = effectSystem;
}

void EffectRuntime::AttachAuthoringRegistry(const EffectAuthoringRegistry* authoringRegistry) {
    authoringRegistry_ = authoringRegistry;
}

bool EffectRuntime::IsAttached() const {
    return effectSystem_ != nullptr;
}

uint32_t EffectRuntime::PlayEffect(std::string_view name, const Vector3& position) {
    if (effectSystem_ == nullptr) {
        return 0;
    }
    return effectSystem_->PlayEffect(name, position);
}

uint32_t EffectRuntime::PlayEffectWithParams(
    std::string_view name,
    const Vector3& position,
    const Vector4& color,
    const Vector3& scale) {
    if (effectSystem_ == nullptr) {
        return 0;
    }
    return effectSystem_->PlayEffectWithParams(name, position, color, scale);
}

void EffectRuntime::StopEffect(uint32_t id) {
    if (effectSystem_ != nullptr) {
        effectSystem_->StopEffect(id);
    }
}

void EffectRuntime::RestartInstance(uint32_t id) {
    if (effectSystem_ != nullptr) {
        effectSystem_->RestartInstance(id);
    }
}

void EffectRuntime::SetInstanceAge(uint32_t id, float age) {
    if (effectSystem_ != nullptr) {
        effectSystem_->SetInstanceAge(id, age);
    }
}

void EffectRuntime::SetEffectPreviewLoop(uint32_t id, bool enabled) {
    if (effectSystem_ != nullptr) {
        effectSystem_->SetEffectPreviewLoop(id, enabled);
    }
}


const EffectInstance* EffectRuntime::FindInstance(uint32_t id) const {
    return effectSystem_ != nullptr ? effectSystem_->FindInstance(id) : nullptr;
}

void EffectRuntime::ClearInstances() {
    if (effectSystem_ != nullptr) {
        effectSystem_->ClearInstances();
    }
}

void EffectRuntime::Update(float deltaTime) {
    if (effectSystem_ == nullptr || paused_) {
        return;
    }
    effectSystem_->Update(deltaTime * speedMultiplier_);
}

EffectRuntimeFrame EffectRuntime::BuildFrame() const {
    if (effectSystem_ == nullptr) {
        return {};
    }

    EffectRuntimeFrame frame{};
    frame.activeInstanceCount = static_cast<uint32_t>(effectSystem_->Instances().size());
    frame.particleQueue = BuildParticleQueue();
    frame.trailQueue = BuildTrailQueue();
    frame.beamQueue = BuildBeamQueue();
    frame.distortionQueue = BuildDistortionQueue();
    frame.ringQueue = BuildRingQueue();
    frame.cylinderQueue = BuildCylinderQueue();
    const EffectAuthoringRegistry& authoringRegistry = AuthoringRegistry();
    frame.authoring.particle =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.particleQueue), authoringRegistry);
    frame.authoring.trail =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.trailQueue), authoringRegistry);
    frame.authoring.beam =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.beamQueue), authoringRegistry);
    frame.authoring.distortion =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.distortionQueue), authoringRegistry);
    frame.authoring.ring =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.ringQueue), authoringRegistry);
    frame.authoring.cylinder =
        BuildRuntimeQueueAuthoringStatus(PrimaryComponentCommon(frame.cylinderQueue), authoringRegistry);
    frame.activeComponentCount =
        static_cast<uint32_t>(
            frame.particleQueue.size() +
            frame.trailQueue.size() +
            frame.beamQueue.size() +
            frame.distortionQueue.size() +
            frame.ringQueue.size() +
            frame.cylinderQueue.size());
    return frame;
}

ParticleRenderFallback EffectRuntime::FindPrimaryParticleFallback() const {
    if (effectSystem_ == nullptr) {
        return {};
    }

    const ParticleRenderQueue queue = BuildParticleQueue();
    if (!queue.empty()) {
        return {
            queue.front().common.componentCommon,
            queue.front().settings
        };
    }

    ParticleComponentAssetView selected{};
    for (const auto& [assetName, asset] : effectSystem_->Assets()) {
        (void)assetName;
        ::ForEachParticleComponent(asset.Components().ParticleStorageView(), [&selected](const ParticleComponentAssetView& particle) {
            if (!selected ||
                particle.common->passState.renderQueue < selected.common->passState.renderQueue ||
                (particle.common->passState.renderQueue == selected.common->passState.renderQueue &&
                    particle.common->name < selected.common->name)) {
                selected = particle;
            }
        });
    }

    if (!selected) {
        return {};
    }

    return {
        selected.common,
        selected.settings
    };
}

void EffectRuntime::ForEachActiveInstanceComponent(
    const std::function<void(const EffectInstance&, const EffectComponentInstance&)>& visitor) const {
    if (effectSystem_ == nullptr) {
        return;
    }

    for (const EffectInstance& instance : effectSystem_->Instances()) {
        if (instance.asset == nullptr) {
            continue;
        }

        for (const EffectComponentInstance& componentInstance : instance.components) {
            if (!componentInstance.active) {
                continue;
            }

            visitor(instance, componentInstance);
        }
    }
}

bool EffectRuntime::BuildActiveComponentCore(
    const EffectInstance& instance,
    const EffectComponentInstance& componentInstance,
    const EffectComponentCommon& componentCommon,
    const EffectRendererDescriptor* rendererDescriptor,
    const EffectSimulationDescriptor* simulationDescriptor,
    ActiveComponentCore& outCore) {
    const float localAge = instance.age - componentCommon.startTime;
    if (localAge < 0.0f ||
        (componentCommon.duration > 0.0f && localAge >= componentCommon.duration)) {
        return false;
    }

    float normalizedAge = 0.0f;
    if (componentCommon.duration > 0.0f) {
        normalizedAge = localAge / componentCommon.duration;
        if (normalizedAge > 1.0f) {
            normalizedAge = 1.0f;
        }
    }

    outCore = {
        {
            instance.asset,
            &componentCommon,
            rendererDescriptor,
            simulationDescriptor,
            &instance,
            &componentInstance,
            componentCommon.passState.renderQueue,
            normalizedAge
        }
    };
    return true;
}

void EffectRuntime::ForEachParticleAssetComponent(
    const std::function<void(const ActiveComponentCore&, const ParticleComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const ParticleComponentAssetView particle =
                    FindParticleComponent(instance.asset->Components().ParticleStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*particle.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Particle)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *particle.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, particle);
                }
            }
        });
}

void EffectRuntime::ForEachTrailAssetComponent(
    const std::function<void(const ActiveComponentCore&, const TrailComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const TrailComponentAssetView trail =
                    FindTrailComponent(instance.asset->Components().TrailStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*trail.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Trail)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *trail.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, trail);
                }
            }
        });
}

void EffectRuntime::ForEachBeamAssetComponent(
    const std::function<void(const ActiveComponentCore&, const BeamComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const BeamComponentAssetView beam =
                    FindBeamComponent(instance.asset->Components().BeamStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*beam.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Beam)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *beam.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, beam);
                }
            }
        });
}

void EffectRuntime::ForEachDistortionAssetComponent(
    const std::function<void(const ActiveComponentCore&, const DistortionComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const DistortionComponentAssetView distortion =
                    FindDistortionComponent(instance.asset->Components().DistortionStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*distortion.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Distortion)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *distortion.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, distortion);
                }
            }
        });
}

void EffectRuntime::ForEachRingAssetComponent(
    const std::function<void(const ActiveComponentCore&, const RingComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const RingComponentAssetView ring =
                    FindRingComponent(instance.asset->Components().RingStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*ring.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Ring)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *ring.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, ring);
                }
            }
        });
}

void EffectRuntime::ForEachCylinderAssetComponent(
    const std::function<void(const ActiveComponentCore&, const CylinderComponentAssetView&)>& visitor) const {
    ForEachActiveInstanceComponent(
        [&visitor, this](const EffectInstance& instance, const EffectComponentInstance& componentInstance) {
            if (instance.asset == nullptr) {
                return;
            }
            if (const CylinderComponentAssetView cylinder =
                    FindCylinderComponent(instance.asset->Components().CylinderStorageView(), componentInstance.componentId)) {
                const RuntimeComponentRoute route =
                    ResolveRuntimeComponentRoute(*cylinder.common, AuthoringRegistry());
                if (!ComponentRoutesToRenderer(route, EffectComponentType::Cylinder)) {
                    return;
                }
                ActiveComponentCore core{};
                if (BuildActiveComponentCore(
                        instance,
                        componentInstance,
                        *cylinder.common,
                        route.renderer,
                        route.simulation,
                        core)) {
                    visitor(core, cylinder);
                }
            }
        });
}

void EffectRuntime::ForEachParticleComponent(
    const std::function<void(const ParticleActiveComponent&)>& visitor) const {
    ForEachParticleAssetComponent(
        [&visitor](const ActiveComponentCore& core, const ParticleComponentAssetView& particle) {
            visitor({core.common, particle.settings});
        });
}

void EffectRuntime::ForEachTrailComponent(
    const std::function<void(const TrailActiveComponent&)>& visitor) const {
    ForEachTrailAssetComponent(
        [&visitor](const ActiveComponentCore& core, const TrailComponentAssetView& trail) {
            visitor({core.common, trail.settings});
        });
}

void EffectRuntime::ForEachBeamComponent(
    const std::function<void(const BeamActiveComponent&)>& visitor) const {
    ForEachBeamAssetComponent(
        [&visitor](const ActiveComponentCore& core, const BeamComponentAssetView& beam) {
            visitor({core.common, beam.settings});
        });
}

void EffectRuntime::ForEachDistortionComponent(
    const std::function<void(const DistortionActiveComponent&)>& visitor) const {
    ForEachDistortionAssetComponent(
        [&visitor](const ActiveComponentCore& core, const DistortionComponentAssetView& distortion) {
            visitor({core.common, distortion.settings});
        });
}

void EffectRuntime::ForEachRingComponent(
    const std::function<void(const RingActiveComponent&)>& visitor) const {
    ForEachRingAssetComponent(
        [&visitor](const ActiveComponentCore& core, const RingComponentAssetView& ring) {
            visitor({core.common, ring.settings});
        });
}

void EffectRuntime::ForEachCylinderComponent(
    const std::function<void(const CylinderActiveComponent&)>& visitor) const {
    ForEachCylinderAssetComponent(
        [&visitor](const ActiveComponentCore& core, const CylinderComponentAssetView& cylinder) {
            visitor({core.common, cylinder.settings});
        });
}

ParticleRenderQueue EffectRuntime::BuildParticleQueue() const {
    ParticleRenderQueue queue;
    ForEachParticleComponent(
        [&queue](const ParticleActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

TrailRenderQueue EffectRuntime::BuildTrailQueue() const {
    TrailRenderQueue queue;
    ForEachTrailComponent(
        [&queue](const TrailActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

BeamRenderQueue EffectRuntime::BuildBeamQueue() const {
    BeamRenderQueue queue;
    ForEachBeamComponent(
        [&queue](const BeamActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

DistortionRenderQueue EffectRuntime::BuildDistortionQueue() const {
    DistortionRenderQueue queue;
    ForEachDistortionComponent(
        [&queue](const DistortionActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

RingRenderQueue EffectRuntime::BuildRingQueue() const {
    RingRenderQueue queue;
    ForEachRingComponent(
        [&queue](const RingActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

CylinderRenderQueue EffectRuntime::BuildCylinderQueue() const {
    CylinderRenderQueue queue;
    ForEachCylinderComponent(
        [&queue](const CylinderActiveComponent& component) {
            queue.push_back({component.common, component.settings});
        });
    SortRenderQueue(queue);
    return queue;
}

const EffectAuthoringRegistry& EffectRuntime::AuthoringRegistry() const {
    // Compatibility fallback for standalone EffectRuntime usage. VfxEngine
    // attaches its owned registry in the normal application path.
    static const EffectAuthoringRegistry defaultRegistry = EffectAuthoringRegistry::Default();
    return authoringRegistry_ != nullptr ? *authoringRegistry_ : defaultRegistry;
}

void EffectRuntime::SetPaused(bool paused) {
    paused_ = paused;
}

bool EffectRuntime::IsPaused() const {
    return paused_;
}

void EffectRuntime::SetSpeedMultiplier(float speedMultiplier) {
    speedMultiplier_ = (std::max)(0.0f, speedMultiplier);
}

float EffectRuntime::SpeedMultiplier() const {
    return speedMultiplier_;
}

const std::unordered_map<std::string, EffectAsset>& EffectRuntime::Assets() const {
    if (effectSystem_ == nullptr) {
        return EmptyAssets();
    }
    return effectSystem_->Assets();
}


const std::vector<EffectInstance>& EffectRuntime::Instances() const {
    if (effectSystem_ == nullptr) {
        return EmptyInstances();
    }
    return effectSystem_->Instances();
}


uint64_t EffectRuntime::ParticlePoolResetSerial() const {
    return effectSystem_ != nullptr ? effectSystem_->ParticlePoolResetSerial() : 0;
}

bool EffectRuntime::RegisterAsset(EffectAsset asset, std::string* errorMessage) {
    if (effectSystem_ == nullptr || !asset.Validate(errorMessage)) {
        if (effectSystem_ == nullptr && errorMessage != nullptr) *errorMessage = "Effect system is unavailable.";
        return false;
    }
    if (!effectSystem_->RegisterAsset(std::move(asset), AuthoringRegistry())) {
        if (errorMessage != nullptr) *errorMessage = "Normalized effect asset failed validation.";
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool EffectRuntime::ReplaceAssets(std::unordered_map<std::string, EffectAsset> assets, std::string* errorMessage) {
    if (effectSystem_ == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "Effect system is unavailable.";
        return false;
    }
    return effectSystem_->ReplaceAssets(std::move(assets), AuthoringRegistry(), errorMessage);
}

bool EffectRuntime::SetInstanceAppearance(uint32_t id, const Transform& transform, const Vector4& color, bool attached) {
    return effectSystem_ != nullptr && effectSystem_->SetInstanceAppearance(id, transform, color, attached);
}

bool EffectRuntime::MoveInstance(uint32_t id, const Vector3& position, bool resetVelocity) {
    return effectSystem_ != nullptr && effectSystem_->MoveInstance(id, position, resetVelocity);
}

bool EffectRuntime::ValidateAssetReplacement(const std::unordered_map<std::string, EffectAsset>& assets, std::string* errorMessage) const {
    if (effectSystem_ == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "Effect system is unavailable.";
        return false;
    }
    EffectSystem staged;
    return staged.ReplaceAssets(assets, AuthoringRegistry(), errorMessage);
}
