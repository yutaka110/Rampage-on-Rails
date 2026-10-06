#include "EffectSystem.h"
#include "EffectAuthoringRegistry.h"
#include "TechniqueRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "vfx/VfxRenderInputs.h"

void EffectRuntimeFrame::Clear() {
    particleQueue.clear();
    trailQueue.clear();
    beamQueue.clear();
    distortionQueue.clear();
    ringQueue.clear();
    cylinderQueue.clear();
    authoring = EffectRuntimeAuthoringFrame{};
    activeInstanceCount = 0;
    activeComponentCount = 0;
}

namespace {
constexpr float kTrailHistoryMinSampleDistance = 0.03f;

float DistanceSq(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

Vector3 LatestTrailHistoryPoint(const EffectInstance& instance) {
    if (instance.trailHistoryCount == 0) {
        return instance.transform.translate;
    }
    return instance.trailHistory[instance.trailHistoryHead];
}

float ResolveTrailHistorySampleDistance(const EffectInstance& instance) {
    float sampleDistance = kTrailHistoryMinSampleDistance;
    if (instance.asset == nullptr) {
        return sampleDistance;
    }

    bool foundTrail = false;
    for (const EffectComponentInstance& componentInstance : instance.components) {
        if (!componentInstance.active) {
            continue;
        }
        const TrailComponentAssetView trail =
            FindTrailComponent(*instance.asset, componentInstance.componentId);
        if (!trail || trail.settings == nullptr) {
            continue;
        }
        if (!foundTrail) {
            sampleDistance = trail.settings->sampleDistance;
            foundTrail = true;
        } else {
            sampleDistance = (std::min)(sampleDistance, trail.settings->sampleDistance);
        }
    }
    return sampleDistance;
}

bool ShouldPushTrailHistoryPoint(
    const EffectInstance& instance,
    const Vector3& position,
    float sampleDistance) {
    if (instance.trailHistoryCount == 0) {
        return true;
    }
    if (sampleDistance <= 0.0f) {
        return true;
    }
    const float kMinDistanceSq = sampleDistance * sampleDistance;
    return DistanceSq(position, LatestTrailHistoryPoint(instance)) >= kMinDistanceSq;
}

float ComputeTotalLifetime(const EffectInstance& instance) {
    float totalLifetime = instance.asset != nullptr ? instance.asset->lifetime : 0.0f;
    if (instance.asset != nullptr) {
        instance.asset->Components().ForEachComponentCommon(
            [&totalLifetime](const EffectComponentCommon& component) {
                const float componentEnd = component.startTime + component.duration;
                if (componentEnd > totalLifetime) {
                    totalLifetime = componentEnd;
                }
            });
    }
    return totalLifetime;
}

void RestartEffectInstanceState(EffectInstance& instance) {
    instance.age = 0.0f;
    instance.previousPosition = instance.transform.translate;
    instance.velocity = {0.0f, 0.0f, 0.0f};
    instance.trailHistoryHead = 0;
    instance.trailHistoryCount = 1;
    instance.trailHistory[0] = instance.transform.translate;
    for (EffectComponentInstance& component : instance.components) {
        component.age = 0.0f;
        component.active = true;
    }
}

VfxComponentInputCommon MakeComponentInputCommon(const EffectRenderItemCommon& item) {
    return {
        &item,
        item.asset,
        item.componentCommon,
        item.rendererDescriptor,
        item.simulationDescriptor,
        item.instance,
        item.componentInstance,
        item.normalizedAge,
        item.renderQueue
    };
}

using ComponentNormalizationBuffer = std::variant<
    ParticleComponentAsset,
    TrailComponentAsset,
    BeamComponentAsset,
    DistortionComponentAsset,
    RingComponentAsset,
    CylinderComponentAsset>;

EffectComponentCommon& ComponentCommon(ComponentNormalizationBuffer& component) {
    return std::visit(
        [](auto& typedComponent) -> EffectComponentCommon& {
            return typedComponent.common;
        },
        component);
}

ComponentNormalizationBuffer MakeComponentBufferForNormalization(
    const EffectAsset& asset,
    const EffectComponentAsset& component) {
    switch (component.common.type) {
    case EffectComponentType::Particle:
        if (const ParticleComponentAssetView particle = ParticleComponentView(component)) {
            return ParticleComponentAsset{*particle.common, *particle.settings};
        }
        return EffectComponentAssetBuilder::MakeParticle(asset, component.common);
    case EffectComponentType::Trail:
        if (const TrailComponentAssetView trail = TrailComponentView(component)) {
            return TrailComponentAsset{*trail.common, *trail.settings};
        }
        return EffectComponentAssetBuilder::MakeTrail(asset, component.common);
    case EffectComponentType::Beam:
        if (const BeamComponentAssetView beam = BeamComponentView(component)) {
            return BeamComponentAsset{*beam.common, *beam.settings};
        }
        return EffectComponentAssetBuilder::MakeBeam(asset, component.common);
    case EffectComponentType::Distortion:
        if (const DistortionComponentAssetView distortion = DistortionComponentView(component)) {
            return DistortionComponentAsset{*distortion.common, *distortion.settings};
        }
        return EffectComponentAssetBuilder::MakeDistortion(asset, component.common);
    case EffectComponentType::Ring:
        if (const RingComponentAssetView ring = RingComponentView(component)) {
            return RingComponentAsset{*ring.common, *ring.settings};
        }
        return EffectComponentAssetBuilder::MakeRing(asset, component.common);
    case EffectComponentType::Cylinder:
        if (const CylinderComponentAssetView cylinder = CylinderComponentView(component)) {
            return CylinderComponentAsset{*cylinder.common, *cylinder.settings};
        }
        return EffectComponentAssetBuilder::MakeCylinder(asset, component.common);
    }
    return EffectComponentAssetBuilder::MakeParticle(asset, component.common);
}

void NormalizeComponentCommon(
    const EffectAsset& asset,
    uint32_t index,
    EffectComponentCommon& common,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (common.id == 0) {
        common.id = index + 1;
    }
    if (common.name.empty()) {
        common.name = asset.name + "_component_" + std::to_string(common.id);
    }
    if (common.duration <= 0.0f) {
        common.duration = asset.lifetime;
    }
    const std::string displayNameOverride = common.techniqueDisplayName;
    const std::string categoryOverride = common.techniqueCategory;
    const std::string descriptionOverride = common.techniqueDescription;
    const EffectTechniqueMetadataSource displayNameSource = common.techniqueDisplayNameSource;
    const EffectTechniqueMetadataSource categorySource = common.techniqueCategorySource;
    const EffectTechniqueMetadataSource descriptionSource = common.techniqueDescriptionSource;

    const EffectTechniqueDescriptor* descriptor = nullptr;
    if (!common.techniqueId.empty()) {
        descriptor = authoringRegistry.Techniques().Find(common.techniqueId);
    }
    if (descriptor == nullptr) {
        descriptor = authoringRegistry.Techniques().FindDefaultForComponentType(common.type);
    }
    if (descriptor == nullptr) {
        return;
    }

    if (common.techniqueId.empty()) {
        common.techniqueId = descriptor->id;
    }
    authoringRegistry.Techniques().ApplyDescriptor(common, *descriptor);
    if (displayNameSource != EffectTechniqueMetadataSource::Registry) {
        common.techniqueDisplayName = displayNameOverride;
        common.techniqueDisplayNameSource = displayNameSource;
    }
    if (categorySource != EffectTechniqueMetadataSource::Registry) {
        common.techniqueCategory = categoryOverride;
        common.techniqueCategorySource = categorySource;
    }
    if (descriptionSource != EffectTechniqueMetadataSource::Registry) {
        common.techniqueDescription = descriptionOverride;
        common.techniqueDescriptionSource = descriptionSource;
    }
    if (common.techniqueDisplayNameSource == EffectTechniqueMetadataSource::Component ||
        common.techniqueCategorySource == EffectTechniqueMetadataSource::Component ||
        common.techniqueDescriptionSource == EffectTechniqueMetadataSource::Component) {
        common.techniqueMetadataSource = EffectTechniqueMetadataSource::Component;
    } else if (
        common.techniqueDisplayNameSource == EffectTechniqueMetadataSource::Asset ||
        common.techniqueCategorySource == EffectTechniqueMetadataSource::Asset ||
        common.techniqueDescriptionSource == EffectTechniqueMetadataSource::Asset) {
        common.techniqueMetadataSource = EffectTechniqueMetadataSource::Asset;
    }
}

} // namespace

void PushTrailHistoryPoint(EffectInstance& instance, const Vector3& position) {
    instance.trailHistoryHead =
        (instance.trailHistoryHead + kEffectTrailHistoryCapacity - 1) % kEffectTrailHistoryCapacity;
    instance.trailHistory[instance.trailHistoryHead] = position;
    if (instance.trailHistoryCount < kEffectTrailHistoryCapacity) {
        ++instance.trailHistoryCount;
    }
}

ParticleRenderFallback EffectRuntimeFrame::PrimaryParticleFallback() const {
    if (particleQueue.empty()) {
        return {};
    }

    return {
        particleQueue.front().common.componentCommon,
        particleQueue.front().settings
    };
}

ParticleRenderInput EffectRuntimeFrame::ParticleInput(const ParticleRenderFallback& fallback) const {
    ParticleRenderInput input{};
    input.fallbackCommon = fallback.common;
    input.fallbackSettings = fallback.settings;
    if (!particleQueue.empty()) {
        input.primary = MakeComponentInputCommon(particleQueue.front().common);
        input.settings = particleQueue.front().settings;
    }
    return input;
}

TrailRenderInput EffectRuntimeFrame::TrailInput() const {
    TrailRenderInput input{};
    if (!trailQueue.empty()) {
        input.primary = MakeComponentInputCommon(trailQueue.front().common);
        input.settings = trailQueue.front().settings;
    }
    return input;
}

BeamRenderInput EffectRuntimeFrame::BeamInput() const {
    if (beamQueue.empty()) {
        return {};
    }

    return {
        MakeComponentInputCommon(beamQueue.front().common),
        beamQueue.front().settings
    };
}

DistortionRenderInput EffectRuntimeFrame::DistortionInput() const {
    DistortionRenderInput input{};
    if (!distortionQueue.empty()) {
        input.primary = MakeComponentInputCommon(distortionQueue.front().common);
        input.settings = distortionQueue.front().settings;
    }
    return input;
}

RingRenderInput EffectRuntimeFrame::RingInput() const {
    RingRenderInput input{};
    if (!ringQueue.empty()) {
        input.primary = MakeComponentInputCommon(ringQueue.front().common);
        input.settings = ringQueue.front().settings;
    }
    return input;
}

CylinderRenderInput EffectRuntimeFrame::CylinderInput() const {
    CylinderRenderInput input{};
    if (!cylinderQueue.empty()) {
        input.primary = MakeComponentInputCommon(cylinderQueue.front().common);
        input.settings = cylinderQueue.front().settings;
    }
    return input;
}


namespace {
bool FiniteVector(const Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool FiniteVector(const Vector4& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }
bool ValidTransform(const Transform& t) {
    return FiniteVector(t.translate) && FiniteVector(t.rotate) && FiniteVector(t.scale) &&
        t.scale.x >= 0.0f && t.scale.y >= 0.0f && t.scale.z >= 0.0f;
}
bool ValidCommon(const EffectComponentCommon& c) {
    return std::isfinite(c.startTime) && c.startTime >= 0.0f && std::isfinite(c.duration) && c.duration >= 0.0f &&
        FiniteVector(c.color) && FiniteVector(c.size) && FiniteVector(c.uvRect) &&
        c.size.x >= 0.0f && c.size.y >= 0.0f && c.size.z >= 0.0f;
}
bool ValidSettings(const EffectParticleSettings& s) {
    for (float v : {
        s.lifetime, s.emissive, s.distortionStrength,
        s.noiseStrength, s.uvScrollSpeed, s.pulseSpeed,
        s.spawnRadius, s.spawnCount, s.spawnFrequency,
        s.randomRotation, s.scaleYMin, s.scaleYMax,
        s.depthFadeSoftness, s.edgeSoftness}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return s.scaleYMin >= 0.0f && s.scaleYMax >= s.scaleYMin && s.spawnCount >= 0.0f && s.spawnCount <= 65536 && s.spawnFrequency >= 0.0f;
}
bool ValidSettings(const EffectTrailSettings& s) {
    for (float v : {
        s.depthFadeSoftness, s.trailTailFade, s.length,
        s.width, s.sampleDistance, s.smoothing,
        s.widthHead, s.widthTail, s.alphaTail,
        s.miterLimit}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return FiniteVector(s.colorTail) && s.segmentBudget > 0 && s.segmentBudget <= 4096 && s.followMode <= EffectTrailFollowMode::MovementHistory && s.length >= 0 && s.width >= 0 && s.sampleDistance >= 0;
}
bool ValidSettings(const EffectBeamSettings& s) {
    for (float v : {
        s.emissive}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return true;
}
bool ValidSettings(const EffectDistortionSettings& s) {
    for (float v : {
        s.strength, s.noiseStrength, s.uvScrollSpeed,
        s.depthFadeSoftness, s.depthAttenuation}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return true;
}
bool ValidSettings(const EffectRingSettings& s) {
    for (float v : {
        s.outerRadius, s.innerRadius, s.emissive,
        s.uvScrollSpeed, s.expansion, s.fadeOut,
        s.depthFadeSoftness}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return s.divide >= 3 && s.divide <= 4096 && s.innerRadius >= 0 && s.outerRadius >= s.innerRadius;
}
bool ValidSettings(const EffectCylinderSettings& s) {
    for (float v : {
        s.topRadius, s.bottomRadius, s.height,
        s.emissive, s.uvScrollSpeed, s.alphaReference,
        s.fadeOut, s.depthFadeSoftness}) {
        if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
    }
    return s.divide >= 3 && s.divide <= 4096 && s.topRadius >= 0 && s.bottomRadius >= 0 && s.height >= 0;
}
} // namespace

bool EffectAsset::Validate(std::string* errorMessage) const {
    bool valid = !name.empty() && name.size() <= 256 && std::isfinite(lifetime) && lifetime >= 0.0f &&
        FiniteVector(color) && FiniteVector(size) && FiniteVector(uvRect) &&
        size.x >= 0.0f && size.y >= 0.0f && size.z >= 0.0f && Components().ComponentCount() <= 4096 &&
        ValidSettings(defaultParticle) && ValidSettings(defaultTrail) && ValidSettings(defaultBeam) &&
        ValidSettings(defaultDistortion) && ValidSettings(defaultRing) && ValidSettings(defaultCylinder);
    std::unordered_set<uint32_t> ids;
    Components().ForEachComponentCommon([&](const EffectComponentCommon& c) {
        valid = valid && ValidCommon(c);
        if (c.id != 0 && !ids.insert(c.id).second) valid = false;
    });
    ForEachParticleComponent(Components().ParticleStorageView(), [&](const ParticleComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    ForEachTrailComponent(Components().TrailStorageView(), [&](const TrailComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    ForEachBeamComponent(Components().BeamStorageView(), [&](const BeamComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    ForEachDistortionComponent(Components().DistortionStorageView(), [&](const DistortionComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    ForEachRingComponent(Components().RingStorageView(), [&](const RingComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    ForEachCylinderComponent(Components().CylinderStorageView(), [&](const CylinderComponentAssetView& c) { valid = valid && ValidSettings(*c.settings); });
    if (errorMessage != nullptr) *errorMessage = valid ? "" : "Effect asset requires finite geometry/settings, valid shape budgets and unique component IDs.";
    return valid;
}

bool EffectSystem::ValidateAssets(const std::unordered_map<std::string, EffectAsset>& assets, std::string* errorMessage) {
    if (assets.size() > 65536) { if (errorMessage != nullptr) *errorMessage = "Effect asset count exceeds its budget."; return false; }
    for (const auto& [name, asset] : assets) {
        if (name != asset.name || !asset.Validate(errorMessage)) {
            if (errorMessage != nullptr && name != asset.name) *errorMessage = "Effect asset key must match its name.";
            return false;
        }
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void EffectSystem::RebindInstances() {
    std::erase_if(instances_, [&](EffectInstance& instance) {
        instance.asset = FindAsset(instance.assetName);
        if (instance.asset == nullptr) return true;
        std::vector<EffectComponentInstance> components;
        instance.asset->Components().ForEachComponentCommon([&](const EffectComponentCommon& c) {
            const auto previous = std::find_if(instance.components.begin(), instance.components.end(),
                [&](const auto& state) { return state.componentId == c.id; });
            components.push_back(previous != instance.components.end() ? *previous : EffectComponentInstance{c.id, instance.age, true});
        });
        instance.components = std::move(components);
        return false;
    });
    ++particlePoolResetSerial_;
}

bool EffectSystem::ReplaceAssets(std::unordered_map<std::string, EffectAsset> assets,
    const EffectAuthoringRegistry& authoringRegistry, std::string* errorMessage) {
    if (!ValidateAssets(assets, errorMessage)) return false;
    for (auto& [name, asset] : assets) EnsureDefaultComponent(asset, authoringRegistry);
    if (!ValidateAssets(assets, errorMessage)) return false;
    // 全候補の検証後に差し替え、再生中の参照とコンポーネントを所有者が更新する。
    assets_ = std::move(assets);
    RebindInstances();
    return true;
}

bool EffectSystem::SetInstanceAppearance(uint32_t id, const Transform& transform, const Vector4& color, bool attached) {
    if (!ValidTransform(transform) || !FiniteVector(color)) return false;
    EffectInstance* instance = FindMutableInstance(id);
    if (instance == nullptr) return false;
    instance->transform = transform; instance->color = color; instance->attached = attached;
    return true;
}

bool EffectSystem::MoveInstance(uint32_t id, const Vector3& position, bool resetVelocity) {
    if (!FiniteVector(position)) return false;
    EffectInstance* instance = FindMutableInstance(id);
    if (instance == nullptr) return false;
    instance->transform.translate = position;
    if (resetVelocity) { instance->previousPosition = position; instance->velocity = {}; }
    return true;
}

bool EffectSystem::RegisterAsset(EffectAsset asset) {
    return RegisterAsset(std::move(asset), EffectAuthoringRegistry::Default());
}

bool EffectSystem::RegisterAsset(
    EffectAsset asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    if (!asset.Validate()) return false;
    EnsureDefaultComponent(asset, authoringRegistry);
    if (!asset.Validate()) return false;
    const std::string assetName = asset.name;
    assets_[assetName] = std::move(asset);
    RebindInstances();
    return true;
}

const EffectAsset* EffectSystem::FindAsset(std::string_view name) const {
    const auto found = assets_.find(std::string(name));
    if (found == assets_.end()) {
        return nullptr;
    }
    return &found->second;
}

uint32_t EffectSystem::PlayEffect(std::string_view name, const Vector3& position) {
    return PlayEffectWithParams(
        name,
        position,
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f});
}

uint32_t EffectSystem::PlayEffectWithParams(
    std::string_view name,
    const Vector3& position,
    const Vector4& color,
    const Vector3& scale) {
    const EffectAsset* asset = FindAsset(name);
    if (asset == nullptr) {
        return 0;
    }

    EffectInstance instance{};
    if (nextInstanceId_ == 0 || nextInstanceId_ == (std::numeric_limits<uint32_t>::max)()) return 0;
    instance.id = nextInstanceId_;
    instance.assetName = asset->name;
    instance.asset = asset;
    instance.components.reserve(asset->Components().ComponentCount());
    asset->Components().ForEachComponentCommon([&instance](const EffectComponentCommon& component) {
        instance.components.push_back({component.id, 0.0f, true});
    });
    instance.transform.translate = position;
    instance.transform.rotate = {0.0f, 0.0f, 0.0f};
    instance.transform.scale = {
        asset->size.x * scale.x,
        asset->size.y * scale.y,
        asset->size.z * scale.z,
    };
    instance.previousPosition = instance.transform.translate;
    instance.velocity = {0.0f, 0.0f, 0.0f};
    instance.trailHistoryHead = 0;
    instance.trailHistoryCount = 1;
    instance.trailHistory[0] = instance.transform.translate;
    instance.color = {
        asset->color.x * color.x,
        asset->color.y * color.y,
        asset->color.z * color.z,
        asset->color.w * color.w,
    };
    if (!ValidTransform(instance.transform) || !FiniteVector(instance.color) || instance.id == 0) return 0;
    ++nextInstanceId_;
    instances_.push_back(instance);
    ++particlePoolResetSerial_;
    return instance.id;
}

void EffectSystem::StopEffect(uint32_t id) {
    instances_.erase(
        std::remove_if(
            instances_.begin(),
            instances_.end(),
            [id](const EffectInstance& instance) {
                return instance.id == id;
            }),
        instances_.end());
}

void EffectSystem::Update(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) return;
    for (EffectInstance& instance : instances_) {
        const Vector3 currentPosition = instance.transform.translate;
        if (deltaTime > 0.0f) {
            instance.velocity = {
                (currentPosition.x - instance.previousPosition.x) / deltaTime,
                (currentPosition.y - instance.previousPosition.y) / deltaTime,
                (currentPosition.z - instance.previousPosition.z) / deltaTime,
            };
        } else {
            instance.velocity = {0.0f, 0.0f, 0.0f};
        }
        instance.previousPosition = currentPosition;
        if (ShouldPushTrailHistoryPoint(instance, currentPosition, ResolveTrailHistorySampleDistance(instance))) {
            PushTrailHistoryPoint(instance, currentPosition);
        }
        instance.age += deltaTime;
        for (EffectComponentInstance& component : instance.components) {
            component.age += deltaTime;
        }

        const float totalLifetime = ComputeTotalLifetime(instance);
        if (instance.previewLoop && totalLifetime > 0.0f && instance.age >= totalLifetime) {
            RestartEffectInstanceState(instance);
            ++particlePoolResetSerial_;
        }
    }

    instances_.erase(
        std::remove_if(
            instances_.begin(),
            instances_.end(),
            [](const EffectInstance& instance) {
                const float totalLifetime = ComputeTotalLifetime(instance);
                return instance.asset == nullptr ||
                       (!instance.previewLoop && totalLifetime > 0.0f && instance.age >= totalLifetime);
            }),
        instances_.end());
}

void EffectSystem::ClearInstances() {
    instances_.clear();
    ++particlePoolResetSerial_;
}

EffectInstance* EffectSystem::FindMutableInstance(uint32_t id) {
    for (EffectInstance& instance : instances_) {
        if (instance.id == id) {
            return &instance;
        }
    }
    return nullptr;
}

const EffectInstance* EffectSystem::FindInstance(uint32_t id) const {
    for (const EffectInstance& instance : instances_) {
        if (instance.id == id) {
            return &instance;
        }
    }
    return nullptr;
}

void EffectSystem::SetEffectPreviewLoop(uint32_t id, bool enabled) {
    if (EffectInstance* instance = FindMutableInstance(id)) {
        instance->previewLoop = enabled;
    }
}

void EffectSystem::RestartInstance(uint32_t id) {
    EffectInstance* instance = FindMutableInstance(id);
    if (instance != nullptr) {
        RestartEffectInstanceState(*instance);
        ++particlePoolResetSerial_;
    }
}

void EffectSystem::SetInstanceAge(uint32_t id, float age) {
    EffectInstance* instance = FindMutableInstance(id);
    if (instance == nullptr) {
        return;
    }

    if (!std::isfinite(age)) return;
    const float clampedAge = (std::max)(0.0f, age);
    if (clampedAge != instance->age) {
        instance->age = clampedAge;
        instance->previousPosition = instance->transform.translate;
        for (EffectComponentInstance& component : instance->components) {
            component.age = clampedAge;
            component.active = true;
        }
        ++particlePoolResetSerial_;
    }
}

void EffectSystem::EnsureDefaultComponent(EffectAsset& asset) {
    EnsureDefaultComponent(asset, EffectAuthoringRegistry::Default());
}

void EffectSystem::EnsureDefaultComponent(
    EffectAsset& asset,
    const EffectAuthoringRegistry& authoringRegistry) {
    EffectAssetComponentStorage& components = asset.MutableComponents();
    if (components.HasPackedComponentsForNormalization()) {
        for (uint32_t index = 0; index < components.PackedComponentCountForNormalization(); ++index) {
            ComponentNormalizationBuffer component =
                MakeComponentBufferForNormalization(
                    asset,
                    components.PackedComponentAtForNormalization(index));
            NormalizeComponentCommon(asset, index, ComponentCommon(component), authoringRegistry);
            std::visit(
                [&components, index](const auto& typedComponent) {
                    using Component = std::decay_t<decltype(typedComponent)>;
                    if constexpr (std::is_same_v<Component, ParticleComponentAsset>) {
                        components.ReplaceParticleComponentAtForAuthoring(index, typedComponent);
                    } else if constexpr (std::is_same_v<Component, TrailComponentAsset>) {
                        components.ReplaceTrailComponentAtForAuthoring(index, typedComponent);
                    } else if constexpr (std::is_same_v<Component, BeamComponentAsset>) {
                        components.ReplaceBeamComponentAtForAuthoring(index, typedComponent);
                    } else if constexpr (std::is_same_v<Component, DistortionComponentAsset>) {
                        components.ReplaceDistortionComponentAtForAuthoring(index, typedComponent);
                    } else if constexpr (std::is_same_v<Component, RingComponentAsset>) {
                        components.ReplaceRingComponentAtForAuthoring(index, typedComponent);
                    } else if constexpr (std::is_same_v<Component, CylinderComponentAsset>) {
                        components.ReplaceCylinderComponentAtForAuthoring(index, typedComponent);
                    }
                },
                component);
        }
        components.SyncTypedStorageFromPackedForNormalization();
        return;
    }

    EffectComponentCommon common = EffectComponentAssetBuilder::MakeCommon(
        asset,
        EffectComponentType::Particle,
        1,
        authoringRegistry.Techniques());
    common.name = asset.name + "_particle";
    components.Add(EffectComponentAssetBuilder::MakeParticle(asset, common));
}
