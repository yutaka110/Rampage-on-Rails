#include "CourseMapVisualBakePipeline.h"
#include "../EditorSettingsValidation.h"

#include "../../course/CourseRuntimeProgramAsset.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace editor {
namespace {

Vector3 Add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashString(uint64_t hash, std::string_view value) {
    return HashBytes(hash, value.data(), value.size());
}

Vector3 ParseVector(const EditorSceneComponent* component,
    std::string_view name, Vector3 fallback) {
    if (component == nullptr) return fallback;
    const auto found = std::find_if(component->properties.begin(), component->properties.end(),
        [&](const EditorSceneProperty& property) { return property.name == name; });
    if (found == component->properties.end()) return fallback;
    std::istringstream stream(found->value);
    Vector3 result{};
    if (!(stream >> result.x >> result.y >> result.z) ||
        !std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        return fallback;
    }
    return result;
}

struct SceneTransform final {
    Vector3 position{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

SceneTransform ResolveSceneTransform(const EditorScene& scene,
    const EditorSceneEntity& entity,
    std::unordered_map<std::string, SceneTransform>& cache,
    std::unordered_set<std::string>& visiting) {
    if (const auto found = cache.find(entity.guid); found != cache.end()) return found->second;
    if (!visiting.insert(entity.guid).second) return {};
    const EditorSceneComponent* transform =
        scene.FindComponent(entity, kEditorTransformComponentType);
    SceneTransform result{};
    result.position = ParseVector(transform, "translation", {});
    result.scale = ParseVector(transform, "scale", {1.0f, 1.0f, 1.0f});
    if (!entity.parentGuid.empty()) {
        if (const EditorSceneEntity* parent = scene.FindEntity(entity.parentGuid)) {
            const SceneTransform inherited = ResolveSceneTransform(
                scene, *parent, cache, visiting);
            result.position = Add(inherited.position,
                {result.position.x * inherited.scale.x,
                 result.position.y * inherited.scale.y,
                 result.position.z * inherited.scale.z});
            result.scale = {result.scale.x * inherited.scale.x,
                result.scale.y * inherited.scale.y,
                result.scale.z * inherited.scale.z};
        }
    }
    visiting.erase(entity.guid);
    cache.insert_or_assign(entity.guid, result);
    return result;
}

std::string MeshIdentity(const EditorSceneComponent* mesh,
    const EditorSceneEntity& entity) {
    if (mesh != nullptr) {
        for (const EditorSceneProperty& property : mesh->properties) {
            if ((property.name == "mesh" || property.name == "asset" ||
                    property.name == "meshId") && !property.value.empty()) {
                return property.value;
            }
        }
        for (const EditorSceneObjectReference& reference : mesh->references) {
            if (!reference.assetGuid.empty()) return reference.assetGuid;
        }
    }
    return entity.name;
}

std::vector<Vector3> BoxCorners(Vector3 center,
    Vector3 axisX, Vector3 axisY, Vector3 axisZ) {
    std::vector<Vector3> corners;
    corners.reserve(8u);
    for (uint32_t corner = 0; corner < 8u; ++corner) {
        Vector3 point = center;
        point = Add(point, Scale(axisX, (corner & 1u) != 0u ? 1.0f : -1.0f));
        point = Add(point, Scale(axisY, (corner & 2u) != 0u ? 1.0f : -1.0f));
        point = Add(point, Scale(axisZ, (corner & 4u) != 0u ? 1.0f : -1.0f));
        corners.push_back(point);
    }
    return corners;
}

CourseMapVisualLayer VisualLayer(CourseTerrainLayer layer) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision: return CourseMapVisualLayer::GameplayTerrain;
    case CourseTerrainLayer::HeroLandmark: return CourseMapVisualLayer::HeroLandmark;
    case CourseTerrainLayer::VistaBackground: return CourseMapVisualLayer::VistaBackground;
    }
    return CourseMapVisualLayer::HeroLandmark;
}

void HeightRange(const std::vector<Vector3>& points, float& minimum, float& maximum) {
    minimum = (std::numeric_limits<float>::max)();
    maximum = -(std::numeric_limits<float>::max)();
    for (const Vector3& point : points) {
        minimum = (std::min)(minimum, point.y);
        maximum = (std::max)(maximum, point.y);
    }
}

CourseMapVisualContour TopContour(const CourseMapVisualPrimitive& primitive,
    float interval) {
    CourseMapVisualContour contour{};
    contour.stableId = primitive.stableId + ":contour";
    contour.height = primitive.maximumHeight;
    contour.major = interval <= 0.0f ||
        std::abs(std::remainder(contour.height, interval * 4.0f)) <= interval * 0.35f;
    if (primitive.worldCorners.size() >= 8u) {
        contour.worldPoints = {primitive.worldCorners[2], primitive.worldCorners[3],
            primitive.worldCorners[7], primitive.worldCorners[6], primitive.worldCorners[2]};
    } else {
        contour.worldPoints = primitive.worldCorners;
        if (!contour.worldPoints.empty()) contour.worldPoints.push_back(contour.worldPoints.front());
    }
    return contour;
}

bool SameSettings(const CourseMapVisualBakeSettings& a,
    const CourseMapVisualBakeSettings& b) {
    return a.includeVistaBackground == b.includeVistaBackground &&
        a.includeRockMasses == b.includeRockMasses &&
        a.includeSceneStructures == b.includeSceneStructures &&
        a.autoBake == b.autoBake &&
        a.persistDerivedAsset == b.persistDerivedAsset &&
        a.contourInterval == b.contourInterval &&
        a.tileWorldSize == b.tileWorldSize &&
        a.minimumPrimitiveExtent == b.minimumPrimitiveExtent &&
        a.maximumPrimitives == b.maximumPrimitives &&
        a.maximumContours == b.maximumContours;
}

std::string SafeName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const unsigned char value : name) {
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_') {
            result.push_back(static_cast<char>(value));
        } else if (value == ' ') {
            result.push_back('_');
        }
    }
    return result.empty() ? "course" : result;
}

} // namespace

const CourseMapVisualBakeResult& CourseMapVisualBakePipeline::Ensure(
    const CourseMapVisualBakeInput& input) {
    lastResult_ = {};
    if (input.rail == nullptr || !input.rail->IsValid() || input.course == nullptr) {
        lastResult_.status = CourseMapVisualBakeStatus::Failed;
        lastResult_.message = "Course Map visual bake requires a valid Course and Rail.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    const uint64_t fingerprint = ResolveFingerprint(input);
    lastResult_.expectedFingerprint = fingerprint;
    lastResult_.cachePath = CachePath(input);
    if (!forceRebuild_ && !cacheAttempted_ && asset_.Empty()) {
        cacheAttempted_ = true;
        if (TryLoadCache(input, fingerprint)) {
            lastResult_.status = CourseMapVisualBakeStatus::Current;
            lastResult_.assetAvailable = true;
            lastResult_.loadedFromCache = true;
            lastResult_.fallbackRequired = false;
            lastResult_.message = "Course Map visual derived asset loaded from cache.";
            lastResult_.stats = lifetimeStats_;
            return lastResult_;
        }
    }
    const CourseMapVisualBakeStatus status = Assess(asset_, input, nullptr);
    if (!forceRebuild_ && status == CourseMapVisualBakeStatus::Current) {
        ++lifetimeStats_.currentHits;
        lastResult_.status = status;
        lastResult_.assetAvailable = true;
        lastResult_.fallbackRequired = false;
        lastResult_.message = "Course Map visual derived asset is current.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    lastResult_.status = forceRebuild_ ? CourseMapVisualBakeStatus::Stale : status;
    lastResult_.fallbackRequired = true;
    if (!settings_.autoBake && !forceRebuild_) {
        lastResult_.message = std::string("Course Map visual asset is ") + ToString(status) +
            "; using authoring fallback.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    CourseMapVisualBakeResult baked = Bake(input);
    lastResult_ = std::move(baked);
    return lastResult_;
}

CourseMapVisualBakeResult CourseMapVisualBakePipeline::Bake(
    const CourseMapVisualBakeInput& input) {
    CourseMapVisualBakeResult result{};
    result.cachePath = CachePath(input);
    if (input.rail == nullptr || !input.rail->IsValid() || input.course == nullptr) {
        result.status = CourseMapVisualBakeStatus::Failed;
        result.message = "Course Map visual bake requires a valid Course and Rail.";
        result.stats = lifetimeStats_;
        return result;
    }
    uint64_t courseHash = 0;
    uint64_t sceneHash = 0;
    uint64_t settingsHash = 0;
    const uint64_t fingerprint = ComputeSourceFingerprint(
        input, settings_, &courseHash, &sceneHash, &settingsHash);
    result.expectedFingerprint = fingerprint;
    if (fingerprint == 0u) {
        result.status = CourseMapVisualBakeStatus::Failed;
        result.message = "Course Map visual source fingerprint could not be generated.";
        result.stats = lifetimeStats_;
        return result;
    }

    CourseMapVisualAsset baked{};
    baked.sourceCourseName = input.course->name;
    baked.sourceCourseHash = courseHash;
    baked.sourceSceneHash = sceneHash;
    baked.bakeSettingsHash = settingsHash;
    baked.sourceFingerprint = fingerprint;
    baked.tileWorldSize = settings_.tileWorldSize;
    const RailPath& path = input.rail->RuntimePath();

    const auto addPrimitive = [&](CourseMapVisualPrimitive primitive) {
        if (baked.primitives.size() >= settings_.maximumPrimitives ||
            primitive.worldCorners.size() < 4u) return false;
        HeightRange(primitive.worldCorners,
            primitive.minimumHeight, primitive.maximumHeight);
        baked.primitives.push_back(std::move(primitive));
        return true;
    };

    for (const CourseTerrainPlacement& placement : input.course->terrainPlacements) {
        if (!placement.editorVisible ||
            (placement.layer == CourseTerrainLayer::VistaBackground &&
                !settings_.includeVistaBackground)) continue;
        const float distance = placement.distance + placement.forwardOffset;
        const RailPathSample sample = path.Evaluate(
            (std::clamp)(distance, 0.0f, input.rail->Length()));
        const Vector3 center = Add(Add(sample.position,
            Scale(sample.right, placement.lateralOffset)),
            Scale(sample.up, placement.verticalOffset));
        const float yaw = placement.rotation.y * 0.017453292519943295f;
        const Vector3 right = Add(Scale(sample.right, std::cos(yaw)),
            Scale(sample.tangent, std::sin(yaw)));
        const Vector3 forward = Add(Scale(sample.tangent, std::cos(yaw)),
            Scale(sample.right, -std::sin(yaw)));
        const float minimumExtent = settings_.minimumPrimitiveExtent;
        CourseMapVisualPrimitive primitive{};
        primitive.stableId = "terrain:" + placement.editorGuid;
        primitive.sourceMeshId = placement.meshId;
        primitive.layer = VisualLayer(placement.layer);
        primitive.worldCenter = center;
        primitive.locked = placement.editorLocked;
        primitive.worldCorners = BoxCorners(center,
            Scale(right, (std::max)(minimumExtent, std::abs(placement.scale.x) * 0.5f)),
            Scale(sample.up, (std::max)(minimumExtent, std::abs(placement.scale.y) * 0.5f)),
            Scale(forward, (std::max)(minimumExtent, std::abs(placement.scale.z) * 0.5f)));
        if (addPrimitive(std::move(primitive))) {
            ++result.stats.terrainPrimitives;
            if (placement.layer == CourseTerrainLayer::HeroLandmark) {
                baked.landmarks.push_back({"landmark:" + placement.editorGuid,
                    placement.id.empty() ? placement.meshId : placement.id,
                    center, 450u});
            }
        }
    }

    if (settings_.includeRockMasses) {
        for (const CourseRockCluster& cluster : input.course->rockClusters) {
            if (!cluster.editorVisible) continue;
            const RailPathSample sample = path.Evaluate(
                (std::clamp)(cluster.distance, 0.0f, input.rail->Length()));
            float side = 0.0f;
            if (cluster.anchor == CourseRockClusterAnchor::LeftWall) side = -1.0f;
            else if (cluster.anchor == CourseRockClusterAnchor::RightWall) side = 1.0f;
            const float lateral = side * (cluster.clearLaneRadius +
                std::abs(cluster.spread.x) * 0.5f);
            const Vector3 center = Add(sample.position, Scale(sample.right, lateral));
            CourseMapVisualPrimitive primitive{};
            primitive.stableId = "rocks:" + cluster.editorGuid;
            primitive.sourceMeshId = cluster.meshId;
            primitive.layer = CourseMapVisualLayer::RockMass;
            primitive.worldCenter = center;
            primitive.locked = cluster.editorLocked;
            primitive.worldCorners = BoxCorners(center,
                Scale(sample.right, (std::max)(1.0f, std::abs(cluster.spread.x) * 0.5f)),
                Scale(sample.up, (std::max)(1.0f, std::abs(cluster.spread.y) * 0.5f)),
                Scale(sample.tangent, (std::max)(1.0f, std::abs(cluster.spread.z) * 0.5f)));
            if (addPrimitive(std::move(primitive))) ++result.stats.rockMasses;
        }
    }

    if (settings_.includeSceneStructures && input.scene != nullptr) {
        std::unordered_map<std::string, SceneTransform> transforms;
        std::unordered_set<std::string> visiting;
        for (const EditorSceneEntity& entity : input.scene->entities) {
            if (!entity.visible) continue;
            const EditorSceneComponent* mesh =
                input.scene->FindComponent(entity, kEditorMeshRendererComponentType);
            if (mesh == nullptr || !mesh->enabled) continue;
            const SceneTransform transform = ResolveSceneTransform(
                *input.scene, entity, transforms, visiting);
            CourseMapVisualPrimitive primitive{};
            primitive.stableId = "structure:" + entity.guid;
            primitive.sourceMeshId = MeshIdentity(mesh, entity);
            primitive.layer = CourseMapVisualLayer::SceneStructure;
            primitive.worldCenter = transform.position;
            primitive.locked = entity.locked;
            primitive.worldCorners = BoxCorners(transform.position,
                {(std::max)(settings_.minimumPrimitiveExtent,
                    std::abs(transform.scale.x) * 0.5f), 0.0f, 0.0f},
                {0.0f, (std::max)(settings_.minimumPrimitiveExtent,
                    std::abs(transform.scale.y) * 0.5f), 0.0f},
                {0.0f, 0.0f, (std::max)(settings_.minimumPrimitiveExtent,
                    std::abs(transform.scale.z) * 0.5f)});
            if (addPrimitive(std::move(primitive))) {
                ++result.stats.sceneStructures;
                baked.landmarks.push_back({"structure-landmark:" + entity.guid,
                    entity.name, transform.position, 180u});
            }
        }
    }

    std::stable_sort(baked.primitives.begin(), baked.primitives.end(),
        [](const CourseMapVisualPrimitive& a, const CourseMapVisualPrimitive& b) {
            return a.stableId < b.stableId;
        });
    for (const CourseMapVisualPrimitive& primitive : baked.primitives) {
        if (baked.contours.size() >= settings_.maximumContours) break;
        baked.contours.push_back(TopContour(primitive, settings_.contourInterval));
    }
    std::stable_sort(baked.landmarks.begin(), baked.landmarks.end(),
        [](const CourseMapVisualLandmark& a, const CourseMapVisualLandmark& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.stableId < b.stableId;
        });

    const float maximum = (std::numeric_limits<float>::max)();
    baked.worldMinimum = {maximum, maximum, maximum};
    baked.worldMaximum = {-maximum, -maximum, -maximum};
    const auto includeBounds = [&](const Vector3& point) {
        baked.worldMinimum.x = (std::min)(baked.worldMinimum.x, point.x);
        baked.worldMinimum.y = (std::min)(baked.worldMinimum.y, point.y);
        baked.worldMinimum.z = (std::min)(baked.worldMinimum.z, point.z);
        baked.worldMaximum.x = (std::max)(baked.worldMaximum.x, point.x);
        baked.worldMaximum.y = (std::max)(baked.worldMaximum.y, point.y);
        baked.worldMaximum.z = (std::max)(baked.worldMaximum.z, point.z);
    };
    std::map<std::pair<int32_t, int32_t>, CourseMapVisualTile> tiles;
    for (uint32_t i = 0; i < baked.primitives.size(); ++i) {
        const CourseMapVisualPrimitive& primitive = baked.primitives[i];
        for (const Vector3& point : primitive.worldCorners) includeBounds(point);
        const int32_t tileX = static_cast<int32_t>(std::floor(
            primitive.worldCenter.x / settings_.tileWorldSize));
        const int32_t tileZ = static_cast<int32_t>(std::floor(
            primitive.worldCenter.z / settings_.tileWorldSize));
        CourseMapVisualTile& tile = tiles[{tileX, tileZ}];
        tile.x = tileX;
        tile.z = tileZ;
        tile.primitiveIndices.push_back(i);
        if (i < baked.contours.size()) tile.contourIndices.push_back(i);
    }
    if (baked.primitives.empty()) {
        baked.worldMinimum = {};
        baked.worldMaximum = {};
    }
    for (auto& [key, tile] : tiles) {
        (void)key;
        tile.worldMinimum = {tile.x * settings_.tileWorldSize,
            baked.worldMinimum.y, tile.z * settings_.tileWorldSize};
        tile.worldMaximum = {(tile.x + 1) * settings_.tileWorldSize,
            baked.worldMaximum.y, (tile.z + 1) * settings_.tileWorldSize};
        baked.tiles.push_back(std::move(tile));
    }
    result.stats.contours = static_cast<uint32_t>(baked.contours.size());
    result.stats.landmarks = static_cast<uint32_t>(baked.landmarks.size());
    result.stats.tiles = static_cast<uint32_t>(baked.tiles.size());
    baked.contentRevision = asset_.contentRevision + 1u;
    if (baked.Empty()) {
        result.status = CourseMapVisualBakeStatus::Missing;
        result.message = "Course Map visual bake produced no world geometry; using authoring fallback.";
        result.fallbackRequired = true;
        result.stats = lifetimeStats_;
        return result;
    }
    std::string validationError;
    if (!baked.Validate(&validationError)) {
        result.status = CourseMapVisualBakeStatus::Failed;
        result.message = "Course Map visual bake validation failed: " + validationError;
        result.fallbackRequired = true;
        result.stats = lifetimeStats_;
        return result;
    }
    asset_ = std::move(baked);
    forceRebuild_ = false;
    cacheAttempted_ = true;
    ++lifetimeStats_.bakes;
    result.stats.bakes = lifetimeStats_.bakes;
    result.stats.cacheLoads = lifetimeStats_.cacheLoads;
    result.stats.currentHits = lifetimeStats_.currentHits;
    if (settings_.persistDerivedAsset) {
        std::string saveError;
        if (!asset_.SaveToFile(result.cachePath.string(), &saveError)) {
            result.message = "Course Map visual baked in memory; cache write failed: " + saveError;
        }
    }
    result.status = CourseMapVisualBakeStatus::Current;
    result.assetAvailable = true;
    result.bakedThisCall = true;
    result.fallbackRequired = false;
    if (result.message.empty()) {
        result.message = "Course Map visual asset baked: " +
            std::to_string(asset_.primitives.size()) + " primitives, " +
            std::to_string(asset_.tiles.size()) + " tiles.";
    }
    lastResult_ = result;
    return result;
}

CourseMapVisualBakeStatus CourseMapVisualBakePipeline::Assess(
    const CourseMapVisualAsset& asset,
    const CourseMapVisualBakeInput& input,
    uint64_t* expectedFingerprint) const {
    const uint64_t fingerprint = ResolveFingerprint(input);
    if (expectedFingerprint != nullptr) *expectedFingerprint = fingerprint;
    if (asset.Empty()) return CourseMapVisualBakeStatus::Missing;
    if (asset.schemaVersion != kCourseMapVisualAssetSchemaVersion ||
        asset.bakerVersion != kCourseMapVisualBakerVersion) {
        return CourseMapVisualBakeStatus::Incompatible;
    }
    std::string error;
    if (!asset.Validate(&error)) return CourseMapVisualBakeStatus::Incompatible;
    return asset.IsSourceCurrent(fingerprint)
        ? CourseMapVisualBakeStatus::Current : CourseMapVisualBakeStatus::Stale;
}

void CourseMapVisualBakePipeline::RequestRebuild() noexcept {
    forceRebuild_ = true;
}

void CourseMapVisualBakePipeline::InvalidateAsset() noexcept {
    asset_ = {};
    cacheAttempted_ = false;
    forceRebuild_ = false;
    lastResult_ = {};
}

bool CourseMapVisualBakeSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(contourInterval, .5f, 1000.0f) &&
        settings::InRange(tileWorldSize, 16.0f, 8192.0f) &&
        settings::InRange(minimumPrimitiveExtent, .05f, 1000.0f) &&
        settings::InRange(maximumPrimitives, 1u, 262144u) &&
        settings::InRange(maximumContours, 1u, 524288u);
    return settings::Result(valid, errorMessage,
        "CourseMapVisualBakeSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapVisualBakePipeline::SetSettings(
    CourseMapVisualBakeSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    cachedFingerprint_ = 0;
    cacheAttempted_ = false;
    forceRebuild_ = true;
    return true;
}

void CourseMapVisualBakePipeline::SetCacheRoot(std::filesystem::path path) {
    if (path.empty() || path == cacheRoot_) return;
    cacheRoot_ = std::move(path);
    cacheAttempted_ = false;
}

const CourseMapVisualAsset* CourseMapVisualBakePipeline::CurrentAsset() const noexcept {
    return lastResult_.status == CourseMapVisualBakeStatus::Current &&
        !asset_.Empty() ? &asset_ : nullptr;
}

uint64_t CourseMapVisualBakePipeline::ComputeSourceFingerprint(
    const CourseMapVisualBakeInput& input,
    const CourseMapVisualBakeSettings& settings,
    uint64_t* courseHash,
    uint64_t* sceneHash,
    uint64_t* settingsHash) {
    if (input.course == nullptr) return 0u;
    std::string error;
    const uint64_t resolvedCourseHash = ComputeCourseAssetSourceHash(*input.course, &error);
    uint64_t resolvedSceneHash = 1469598103934665603ull;
    if (input.scene != nullptr) {
        resolvedSceneHash = HashValue(resolvedSceneHash, input.scene->schemaVersion);
        for (const EditorSceneEntity& entity : input.scene->entities) {
            resolvedSceneHash = HashString(resolvedSceneHash, entity.guid);
            resolvedSceneHash = HashString(resolvedSceneHash, entity.parentGuid);
            resolvedSceneHash = HashString(resolvedSceneHash, entity.name);
            resolvedSceneHash = HashValue(resolvedSceneHash, entity.visible);
            resolvedSceneHash = HashValue(resolvedSceneHash, entity.locked);
            for (const EditorSceneComponent& component : entity.components) {
                if (component.typeId != kEditorTransformComponentType &&
                    component.typeId != kEditorMeshRendererComponentType) continue;
                resolvedSceneHash = HashString(resolvedSceneHash, component.typeId);
                resolvedSceneHash = HashValue(resolvedSceneHash, component.enabled);
                for (const EditorSceneProperty& property : component.properties) {
                    resolvedSceneHash = HashString(resolvedSceneHash, property.name);
                    resolvedSceneHash = HashString(resolvedSceneHash, property.value);
                }
                for (const EditorSceneObjectReference& reference : component.references) {
                    resolvedSceneHash = HashString(resolvedSceneHash, reference.property);
                    resolvedSceneHash = HashString(resolvedSceneHash, reference.assetGuid);
                }
            }
        }
    }
    uint64_t resolvedSettingsHash = 1469598103934665603ull;
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.includeVistaBackground);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.includeRockMasses);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.includeSceneStructures);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.contourInterval);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.tileWorldSize);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.minimumPrimitiveExtent);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.maximumPrimitives);
    resolvedSettingsHash = HashValue(resolvedSettingsHash, settings.maximumContours);
    uint64_t fingerprint = 1469598103934665603ull;
    fingerprint = HashValue(fingerprint, resolvedCourseHash);
    fingerprint = HashValue(fingerprint, resolvedSceneHash);
    fingerprint = HashValue(fingerprint, resolvedSettingsHash);
    fingerprint = HashValue(fingerprint, kCourseMapVisualAssetSchemaVersion);
    fingerprint = HashValue(fingerprint, kCourseMapVisualBakerVersion);
    if (courseHash != nullptr) *courseHash = resolvedCourseHash;
    if (sceneHash != nullptr) *sceneHash = resolvedSceneHash;
    if (settingsHash != nullptr) *settingsHash = resolvedSettingsHash;
    return fingerprint;
}

uint64_t CourseMapVisualBakePipeline::ResolveFingerprint(
    const CourseMapVisualBakeInput& input) const {
    const SourceKey key{input.course, input.scene, input.courseRevision,
        input.enemyRevision, input.scene != nullptr ? input.scene->revision : 0u,
        settingsRevision_, input.railGeneration, input.enemyGeneration};
    if (cachedFingerprint_ != 0u && SameSourceKey(fingerprintKey_, key)) {
        return cachedFingerprint_;
    }
    fingerprintKey_ = key;
    cachedFingerprint_ = ComputeSourceFingerprint(input, settings_);
    return cachedFingerprint_;
}

std::filesystem::path CourseMapVisualBakePipeline::CachePath(
    const CourseMapVisualBakeInput& input) const {
    const std::string name = input.course != nullptr
        ? SafeName(input.course->name) : "course";
    return cacheRoot_ / (name + ".coursemapvisual");
}

bool CourseMapVisualBakePipeline::TryLoadCache(
    const CourseMapVisualBakeInput& input,
    uint64_t fingerprint) {
    CourseMapVisualAsset loaded{};
    std::string error;
    if (!loaded.LoadFromFile(CachePath(input).string(), &error)) return false;
    asset_ = std::move(loaded);
    if (!asset_.IsSourceCurrent(fingerprint)) return false;
    ++lifetimeStats_.cacheLoads;
    return true;
}

bool CourseMapVisualBakePipeline::SameSourceKey(
    const SourceKey& a, const SourceKey& b) noexcept {
    return a.course == b.course && a.scene == b.scene &&
        a.courseRevision == b.courseRevision &&
        a.enemyRevision == b.enemyRevision &&
        a.sceneRevision == b.sceneRevision &&
        a.settingsRevision == b.settingsRevision &&
        a.railGeneration == b.railGeneration &&
        a.enemyGeneration == b.enemyGeneration;
}

const char* ToString(CourseMapVisualBakeStatus status) noexcept {
    switch (status) {
    case CourseMapVisualBakeStatus::Missing: return "Missing";
    case CourseMapVisualBakeStatus::Current: return "Current";
    case CourseMapVisualBakeStatus::Stale: return "Stale";
    case CourseMapVisualBakeStatus::Incompatible: return "Incompatible";
    case CourseMapVisualBakeStatus::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace editor
