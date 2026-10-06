#include "EditorProductionPropertyAdapter.h"

#include "../AppRuntimeState.h"
#include "../EffectRuntime.h"
#include "../EffectSystem.h"
#include "../PostProcessStack.h"
#include "../course/CourseAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

namespace editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * kPi / 180.0f;
}

float RadiansToDegrees(float radians) {
    return radians * 180.0f / kPi;
}

bool SameFloat(float lhs, float rhs) {
    return std::fabs(lhs - rhs) <= 0.0001f;
}

void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) {
        *errorMessage = message != nullptr ? message : "";
    }
}

bool AssignFloat(float& target, float value) {
    if (SameFloat(target, value)) {
        return false;
    }
    target = value;
    return true;
}

bool AssignUInt(uint32_t& target, uint32_t value) {
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

bool AssignBool(bool& target, bool value) {
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

bool AssignString(std::string& target, const std::string& value) {
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

std::string StableIdSuffix(const EditorObjectHandle& object, const char* prefix) {
    const std::string expected = std::string(prefix) + ":";
    if (object.stableId.find(expected) == 0) {
        return object.stableId.substr(expected.size());
    }
    return object.displayName;
}

const EffectAsset* FindEffectAsset(
    const EffectRuntime* runtime,
    const EditorObjectHandle& object) {
    if (runtime == nullptr || object.domain != EditorDomainId::VfxEffectAsset) {
        return nullptr;
    }
    const std::string name = StableIdSuffix(object, "vfx-asset");
    if (!name.empty()) {
        auto it = runtime->Assets().find(name);
        return it != runtime->Assets().end() ? &it->second : nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(object.localIndex);
    if (index >= runtime->Assets().size()) {
        return nullptr;
    }
    auto it = runtime->Assets().begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    return &it->second;
}


std::string EffectTechniqueLabel(const EffectAsset& asset) {
    bool hasParticle = false;
    bool hasTrail = false;
    bool hasBeam = false;
    bool hasDistortion = false;
    bool hasRing = false;
    bool hasCylinder = false;
    asset.Components().ForEachComponentCommon(
        [&](const EffectComponentCommon& common) {
            switch (common.type) {
            case EffectComponentType::Particle: hasParticle = true; break;
            case EffectComponentType::Trail: hasTrail = true; break;
            case EffectComponentType::Beam: hasBeam = true; break;
            case EffectComponentType::Distortion: hasDistortion = true; break;
            case EffectComponentType::Ring: hasRing = true; break;
            case EffectComponentType::Cylinder: hasCylinder = true; break;
            }
        });
    const uint32_t count =
        (hasParticle ? 1u : 0u) + (hasTrail ? 1u : 0u) + (hasBeam ? 1u : 0u) +
        (hasDistortion ? 1u : 0u) + (hasRing ? 1u : 0u) + (hasCylinder ? 1u : 0u);
    if (count > 1) {
        return "mixed";
    }
    if (hasTrail) return "trail";
    if (hasBeam) return "beam";
    if (hasDistortion) return "distortion";
    if (hasRing) return "ring";
    if (hasCylinder) return "cylinder";
    return "particle";
}

const PostProcessPass* FindPostProcessPass(
    const PostProcessStack* stack,
    const EditorObjectHandle& object) {
    if (stack == nullptr || object.domain != EditorDomainId::PostProcessPass) {
        return nullptr;
    }
    const std::vector<PostProcessPass>& passes = stack->Passes();
    if (!object.stableId.empty()) {
        const std::string name = StableIdSuffix(object, "post-process");
        auto it = std::find_if(
            passes.begin(),
            passes.end(),
            [&](const PostProcessPass& pass) {
                return pass.name == name;
            });
        if (it != passes.end()) {
            return &*it;
        }
    }
    const std::size_t index = static_cast<std::size_t>(object.localIndex);
    return index < passes.size() ? &passes[index] : nullptr;
}


std::string PostProcessStage(const PostProcessPass& pass) {
    if (pass.pipeline == "ToneMapping") {
        return "tonemap";
    }
    if (pass.pipeline.find("Bloom") == 0 || pass.pipeline.find("Blur") != std::string::npos) {
        return "pre_tonemap";
    }
    if (pass.pipeline.find("Debug") != std::string::npos) {
        return "debug";
    }
    return "post_tonemap";
}

float& PostProcessThreshold(PostProcessPass& pass) {
    if (pass.pipeline == "BloomExtract") {
        return pass.parameters.bloomThresholdMin;
    }
    if (pass.pipeline == "PrewittOutline") {
        return pass.parameters.outlineThreshold;
    }
    return pass.parameters.bloomThresholdMin;
}

const float& PostProcessThreshold(const PostProcessPass& pass) {
    return PostProcessThreshold(const_cast<PostProcessPass&>(pass));
}

float& PostProcessRadius(PostProcessPass& pass) {
    if (pass.pipeline == "Vignette") {
        return pass.parameters.vignetteRadius;
    }
    if (pass.pipeline == "AccretionComposite") {
        return pass.parameters.accretionRadius;
    }
    if (pass.pipeline.find("GaussianBlur") != std::string::npos) {
        return pass.parameters.gaussianBlurKernelRadius;
    }
    if (pass.pipeline.find("BoxBlur") != std::string::npos) {
        return pass.parameters.boxBlurKernelRadius;
    }
    return pass.parameters.blurRadius;
}

const float& PostProcessRadius(const PostProcessPass& pass) {
    return PostProcessRadius(const_cast<PostProcessPass&>(pass));
}

CourseCameraKey* FindCameraKey(CourseAsset* course, const EditorObjectHandle& object) {
    if (course == nullptr || object.domain != EditorDomainId::CourseCameraKey) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(object.localIndex);
    return index < course->cameraKeys.size() ? &course->cameraKeys[index] : nullptr;
}

const CourseCameraKey* FindCameraKey(const CourseAsset* course, const EditorObjectHandle& object) {
    return FindCameraKey(const_cast<CourseAsset*>(course), object);
}

CourseEventMarker* FindCourseEvent(CourseAsset* course, const EditorObjectHandle& object) {
    if (course == nullptr || object.domain != EditorDomainId::CourseEventMarker) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(object.localIndex);
    return index < course->events.size() ? &course->events[index] : nullptr;
}

const CourseEventMarker* FindCourseEvent(const CourseAsset* course, const EditorObjectHandle& object) {
    return FindCourseEvent(const_cast<CourseAsset*>(course), object);
}

bool ShouldBumpAuthoringRevision(EditorDomainId domain) {
    return domain == EditorDomainId::CourseCameraKey ||
        domain == EditorDomainId::CourseEventMarker ||
        domain == EditorDomainId::TerrainGeneration;
}

void MarkProductionEdited(
    AppRuntimeState* runtimeState,
    EditorDomainId domain,
    bool markEdits) {
    if (runtimeState != nullptr && markEdits) {
        if (ShouldBumpAuthoringRevision(domain)) {
            ++runtimeState->terrain.courseObjectEditRevision;
        }
    }
}

} // namespace

EditorProductionPropertyAdapter::EditorProductionPropertyAdapter(
    EffectRuntime* effectRuntime,
    PostProcessStack* postProcessStack,
    AppRuntimeState* runtimeState,
    CourseAsset* course,
    bool markEdits)
    : effectRuntime_(effectRuntime)
    , postProcessStack_(postProcessStack)
    , runtimeState_(runtimeState)
    , course_(course)
    , markEdits_(markEdits) {
}

bool EditorProductionPropertyAdapter::CanAccess(
    const EditorObjectHandle& object,
    const EditorPropertyDescriptor& descriptor) const {
    if (descriptor.domain != object.domain) {
        return false;
    }
    switch (object.domain) {
    case EditorDomainId::VfxEffectAsset:
        return FindEffectAsset(effectRuntime_, object) != nullptr;
    case EditorDomainId::PostProcessPass:
        return FindPostProcessPass(postProcessStack_, object) != nullptr;
    case EditorDomainId::CourseCameraKey:
        return FindCameraKey(course_, object) != nullptr;
    case EditorDomainId::CourseEventMarker:
        return FindCourseEvent(course_, object) != nullptr;
    case EditorDomainId::TerrainGeneration:
    case EditorDomainId::CameraRig:
    case EditorDomainId::GameplayTuning:
        return runtimeState_ != nullptr;
    default:
        return false;
    }
}

bool EditorProductionPropertyAdapter::Get(
    const EditorObjectHandle& object,
    const EditorPropertyDescriptor& descriptor,
    EditorPropertyValue& outValue) const {
    if (!CanAccess(object, descriptor)) {
        return false;
    }
    const std::string& name = descriptor.name;

    if (const EffectAsset* asset = FindEffectAsset(effectRuntime_, object)) {
        if (name == "VfxEffectAsset.name") { outValue.stringValue = asset->name; return true; }
        if (name == "VfxEffectAsset.technique") { outValue.stringValue = EffectTechniqueLabel(*asset); return true; }
        if (name == "VfxEffectAsset.spawnRate") { outValue.floatValue = asset->defaultParticle.spawnFrequency; return true; }
        if (name == "VfxEffectAsset.lifetime") { outValue.floatValue = asset->lifetime; return true; }
        if (name == "VfxEffectAsset.initialSpeed") { outValue.floatValue = asset->defaultParticle.uvScrollSpeed; return true; }
        if (name == "VfxEffectAsset.color") { outValue.vec3Value = {asset->color.x, asset->color.y, asset->color.z}; return true; }
        if (name == "VfxEffectAsset.texture") { outValue.stringValue = asset->texture; return true; }
        return false;
    }

    if (const PostProcessPass* pass = FindPostProcessPass(postProcessStack_, object)) {
        if (name == "PostProcessPass.enabled") { outValue.boolValue = pass->enabled; return true; }
        if (name == "PostProcessPass.stage") { outValue.stringValue = PostProcessStage(*pass); return true; }
        if (name == "PostProcessPass.intensity") { outValue.floatValue = pass->intensity; return true; }
        if (name == "PostProcessPass.threshold") { outValue.floatValue = PostProcessThreshold(*pass); return true; }
        if (name == "PostProcessPass.radius") { outValue.floatValue = PostProcessRadius(*pass); return true; }
        return false;
    }

    if (const CourseCameraKey* key = FindCameraKey(course_, object)) {
        if (name == "CourseCameraKey.distance") { outValue.floatValue = key->distance; return true; }
        if (name == "CourseCameraKey.backDistance") { outValue.floatValue = key->backDistance; return true; }
        if (name == "CourseCameraKey.verticalOffset") { outValue.floatValue = key->verticalOffset; return true; }
        if (name == "CourseCameraKey.lateralOffset") { outValue.floatValue = key->lateralOffset; return true; }
        if (name == "CourseCameraKey.lookAheadDistance") { outValue.floatValue = key->lookAheadDistance; return true; }
        if (name == "CourseCameraKey.lookUpOffset") { outValue.floatValue = key->lookUpOffset; return true; }
        if (name == "CourseCameraKey.lookForwardOffset") { outValue.floatValue = key->lookForwardOffset; return true; }
        if (name == "CourseCameraKey.fov") { outValue.floatValue = RadiansToDegrees(key->fovY); return true; }
        if (name == "CourseCameraKey.roll") { outValue.floatValue = RadiansToDegrees(key->roll); return true; }
        return false;
    }

    if (const CourseEventMarker* event = FindCourseEvent(course_, object)) {
        if (name == "CourseEventMarker.distance") { outValue.floatValue = event->distance; return true; }
        if (name == "CourseEventMarker.type") { outValue.stringValue = event->type; return true; }
        if (name == "CourseEventMarker.id") { outValue.stringValue = event->id; return true; }
        if (name == "CourseEventMarker.payload") { outValue.stringValue = event->payload; return true; }
        return false;
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::TerrainGeneration) {
        const TerrainGenerationSettings& settings = runtimeState_->terrain.settings;
        if (name == "TerrainGeneration.seed") { outValue.uintValue = settings.seed; return true; }
        if (name == "TerrainGeneration.chunkLength") { outValue.floatValue = settings.chunkLength; return true; }
        if (name == "TerrainGeneration.chunkRadius") { outValue.uintValue = settings.visibleAheadChunks; return true; }
        if (name == "TerrainGeneration.noiseScale") { outValue.floatValue = settings.volumeRoughness; return true; }
        if (name == "TerrainGeneration.wallHeight") { outValue.floatValue = settings.wallHeight; return true; }
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::CameraRig) {
        if (name == "CameraRig.mode") { outValue.stringValue = runtimeState_->camera.enableDebugInput ? "debug" : "gameplay"; return true; }
        if (name == "CameraRig.nearClip") { outValue.floatValue = runtimeState_->camera.nearZ; return true; }
        if (name == "CameraRig.farClip") { outValue.floatValue = runtimeState_->camera.farZ; return true; }
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::GameplayTuning) {
        if (name == "GameplayTuning.playerSpeed") { outValue.floatValue = runtimeState_->terrain.previewSpeed; return true; }
        if (name == "GameplayTuning.lockOnRange") { outValue.floatValue = runtimeState_->terrain.courseObjectMoveSensitivity * 10000.0f; return true; }
        if (name == "GameplayTuning.reticleAssist") { outValue.floatValue = runtimeState_->terrain.courseObjectRotateSensitivity * 100.0f; return true; }
        if (name == "GameplayTuning.enemySpawnBudget") { outValue.uintValue = runtimeState_->terrain.debrisOcclusionUpdateInterval; return true; }
    }

    return false;
}

bool EditorProductionPropertyAdapter::Set(
    const EditorObjectHandle& object,
    const EditorPropertyDescriptor& descriptor,
    const EditorPropertyValue& value,
    std::string* errorMessage) {
    if (!CanAccess(object, descriptor)) {
        SetError(errorMessage, "Production property adapter cannot access this target.");
        return false;
    }
    const std::string& name = descriptor.name;
    bool changed = false;

    if ((object.domain == EditorDomainId::VfxEffectAsset || object.domain == EditorDomainId::PostProcessPass) &&
        (!std::isfinite(value.floatValue) || !std::isfinite(value.vec3Value.x) ||
         !std::isfinite(value.vec3Value.y) || !std::isfinite(value.vec3Value.z))) {
        SetError(errorMessage, "Effect and post-process properties must be finite.");
        return false;
    }
    if (const EffectAsset* current = FindEffectAsset(effectRuntime_, object)) {
        EffectAsset candidate = *current;
        EffectAsset* asset = &candidate;
        if (name == "VfxEffectAsset.name") {
            SetError(errorMessage, "Renaming VFX assets must go through Asset Mutation.");
            return false;
        } else if (name == "VfxEffectAsset.technique") {
            SetError(errorMessage, "Changing VFX component technique requires typed component replacement.");
            return false;
        } else if (name == "VfxEffectAsset.spawnRate") {
            changed = AssignFloat(asset->defaultParticle.spawnFrequency, (std::max)(0.0f, value.floatValue));
        } else if (name == "VfxEffectAsset.lifetime") {
            changed = AssignFloat(asset->lifetime, (std::max)(0.01f, value.floatValue));
        } else if (name == "VfxEffectAsset.initialSpeed") {
            changed = AssignFloat(asset->defaultParticle.uvScrollSpeed, value.floatValue);
        } else if (name == "VfxEffectAsset.color") {
            changed = AssignFloat(asset->color.x, value.vec3Value.x) || changed;
            changed = AssignFloat(asset->color.y, value.vec3Value.y) || changed;
            changed = AssignFloat(asset->color.z, value.vec3Value.z) || changed;
        } else if (name == "VfxEffectAsset.texture") {
            changed = AssignString(asset->texture, value.stringValue);
        } else {
            SetError(errorMessage, "Unsupported VFX asset property.");
            return false;
        }
        if (changed && !effectRuntime_->RegisterAsset(std::move(candidate), errorMessage)) return false;
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (const PostProcessPass* current = FindPostProcessPass(postProcessStack_, object)) {
        PostProcessPass candidate = *current;
        PostProcessPass* pass = &candidate;
        if (name == "PostProcessPass.enabled") {
            changed = AssignBool(pass->enabled, value.boolValue);
        } else if (name == "PostProcessPass.stage") {
            SetError(errorMessage, "Post-process stage is derived from the production pipeline.");
            return false;
        } else if (name == "PostProcessPass.intensity") {
            changed = AssignFloat(pass->intensity, (std::max)(0.0f, value.floatValue));
        } else if (name == "PostProcessPass.threshold") {
            changed = AssignFloat(PostProcessThreshold(*pass), (std::max)(0.0f, value.floatValue));
        } else if (name == "PostProcessPass.radius") {
            changed = AssignFloat(PostProcessRadius(*pass), (std::max)(0.0f, value.floatValue));
        } else {
            SetError(errorMessage, "Unsupported post-process property.");
            return false;
        }
        if (changed) {
            if (name == "PostProcessPass.enabled") {
                postProcessStack_->SetEnabled(candidate.name, candidate.enabled);
            } else if (!postProcessStack_->ConfigurePass(candidate, errorMessage)) {
                return false;
            }
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (CourseCameraKey* key = FindCameraKey(course_, object)) {
        if (name == "CourseCameraKey.distance") changed = AssignFloat(key->distance, value.floatValue);
        else if (name == "CourseCameraKey.backDistance") changed = AssignFloat(key->backDistance, value.floatValue);
        else if (name == "CourseCameraKey.verticalOffset") changed = AssignFloat(key->verticalOffset, value.floatValue);
        else if (name == "CourseCameraKey.lateralOffset") changed = AssignFloat(key->lateralOffset, value.floatValue);
        else if (name == "CourseCameraKey.lookAheadDistance") changed = AssignFloat(key->lookAheadDistance, value.floatValue);
        else if (name == "CourseCameraKey.lookUpOffset") changed = AssignFloat(key->lookUpOffset, value.floatValue);
        else if (name == "CourseCameraKey.lookForwardOffset") changed = AssignFloat(key->lookForwardOffset, value.floatValue);
        else if (name == "CourseCameraKey.fov") changed = AssignFloat(key->fovY, DegreesToRadians(value.floatValue));
        else if (name == "CourseCameraKey.roll") changed = AssignFloat(key->roll, DegreesToRadians(value.floatValue));
        else {
            SetError(errorMessage, "Unsupported camera key property.");
            return false;
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (CourseEventMarker* event = FindCourseEvent(course_, object)) {
        if (name == "CourseEventMarker.distance") changed = AssignFloat(event->distance, value.floatValue);
        else if (name == "CourseEventMarker.type") changed = AssignString(event->type, value.stringValue);
        else if (name == "CourseEventMarker.id") changed = AssignString(event->id, value.stringValue);
        else if (name == "CourseEventMarker.payload") changed = AssignString(event->payload, value.stringValue);
        else {
            SetError(errorMessage, "Unsupported course event property.");
            return false;
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::TerrainGeneration) {
        TerrainGenerationSettings& settings = runtimeState_->terrain.settings;
        if (name == "TerrainGeneration.seed") changed = AssignUInt(settings.seed, value.uintValue);
        else if (name == "TerrainGeneration.chunkLength") changed = AssignFloat(settings.chunkLength, (std::max)(16.0f, value.floatValue));
        else if (name == "TerrainGeneration.chunkRadius") changed = AssignUInt(settings.visibleAheadChunks, (std::max)(1u, value.uintValue));
        else if (name == "TerrainGeneration.noiseScale") changed = AssignFloat(settings.volumeRoughness, (std::clamp)(value.floatValue, 0.001f, 1.0f));
        else if (name == "TerrainGeneration.wallHeight") changed = AssignFloat(settings.wallHeight, (std::max)(0.0f, value.floatValue));
        else {
            SetError(errorMessage, "Unsupported terrain generation property.");
            return false;
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::CameraRig) {
        if (name == "CameraRig.mode") {
            if (value.stringValue == "debug") changed = AssignBool(runtimeState_->camera.enableDebugInput, true);
            else if (value.stringValue == "gameplay" || value.stringValue == "authoring" || value.stringValue == "cinematic") changed = AssignBool(runtimeState_->camera.enableDebugInput, false);
            else {
                SetError(errorMessage, "Unknown camera rig mode.");
                return false;
            }
        } else if (name == "CameraRig.nearClip") {
            changed = AssignFloat(runtimeState_->camera.nearZ, (std::max)(0.001f, value.floatValue));
        } else if (name == "CameraRig.farClip") {
            changed = AssignFloat(runtimeState_->camera.farZ, (std::max)(10.0f, value.floatValue));
        } else {
            SetError(errorMessage, "Unsupported camera rig property.");
            return false;
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    if (runtimeState_ != nullptr && object.domain == EditorDomainId::GameplayTuning) {
        if (name == "GameplayTuning.playerSpeed") changed = AssignFloat(runtimeState_->terrain.previewSpeed, (std::max)(0.0f, value.floatValue));
        else if (name == "GameplayTuning.lockOnRange") changed = AssignFloat(runtimeState_->terrain.courseObjectMoveSensitivity, (std::max)(0.0f, value.floatValue) / 10000.0f);
        else if (name == "GameplayTuning.reticleAssist") changed = AssignFloat(runtimeState_->terrain.courseObjectRotateSensitivity, (std::clamp)(value.floatValue, 0.0f, 1.0f) / 100.0f);
        else if (name == "GameplayTuning.enemySpawnBudget") changed = AssignUInt(runtimeState_->terrain.debrisOcclusionUpdateInterval, (std::max)(1u, value.uintValue));
        else {
            SetError(errorMessage, "Unsupported gameplay tuning property.");
            return false;
        }
        MarkProductionEdited(runtimeState_, object.domain, markEdits_ && changed);
        return true;
    }

    SetError(errorMessage, "Unsupported production property domain.");
    return false;
}

} // namespace editor
