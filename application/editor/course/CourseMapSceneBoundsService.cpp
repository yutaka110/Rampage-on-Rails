#include "CourseMapSceneBoundsService.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
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
            const SceneTransform parentTransform =
                ResolveSceneTransform(scene, *parent, cache, visiting);
            result.position = Add(parentTransform.position,
                {result.position.x * parentTransform.scale.x,
                 result.position.y * parentTransform.scale.y,
                 result.position.z * parentTransform.scale.z});
            result.scale = {result.scale.x * parentTransform.scale.x,
                result.scale.y * parentTransform.scale.y,
                result.scale.z * parentTransform.scale.z};
        }
    }
    visiting.erase(entity.guid);
    cache.insert_or_assign(entity.guid, result);
    return result;
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

bool SameSettings(const CourseMapSceneBoundsSettings& a,
    const CourseMapSceneBoundsSettings& b) {
    return a.includeGameplayTerrain == b.includeGameplayTerrain &&
        a.includeHeroLandmarks == b.includeHeroLandmarks &&
        a.includeVistaBackground == b.includeVistaBackground &&
        a.includeRockClusters == b.includeRockClusters &&
        a.includeSceneStructures == b.includeSceneStructures &&
        a.includeEnemies == b.includeEnemies &&
        a.worldPadding == b.worldPadding &&
        a.maximumFitPoints == b.maximumFitPoints;
}

} // namespace

const CourseMapSceneBoundsFrame& CourseMapSceneBoundsService::Build(
    const CourseMapSceneBoundsInput& input) {
    if (input.rail == nullptr || !input.rail->IsValid() || input.course == nullptr) {
        frame_ = {};
        cacheValid_ = false;
        return frame_;
    }
    const CacheKey key{CourseSignature(*input.course), input.courseRevision,
        input.scene != nullptr ? input.scene->revision : 0u, input.enemyRevision,
        settingsRevision_,
        input.railGeneration, input.enemyGeneration,
        input.scene != nullptr, input.enemies != nullptr};
    if (cacheValid_ && frame_.valid && SameKey(cachedKey_, key)) {
        ++frame_.stats.cacheHits;
        ++lifetimeStats_.cacheHits;
        return frame_;
    }

    frame_ = {};
    frame_.valid = true;
    cachedKey_ = key;
    cacheValid_ = true;
    const float maximum = (std::numeric_limits<float>::max)();
    frame_.worldMinimum = {maximum, maximum, maximum};
    frame_.worldMaximum = {-maximum, -maximum, -maximum};
    const auto include = [&](Vector3 point) {
        if (frame_.fitPoints.size() >= settings_.maximumFitPoints ||
            !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
        frame_.worldMinimum.x = (std::min)(frame_.worldMinimum.x, point.x);
        frame_.worldMinimum.y = (std::min)(frame_.worldMinimum.y, point.y);
        frame_.worldMinimum.z = (std::min)(frame_.worldMinimum.z, point.z);
        frame_.worldMaximum.x = (std::max)(frame_.worldMaximum.x, point.x);
        frame_.worldMaximum.y = (std::max)(frame_.worldMaximum.y, point.y);
        frame_.worldMaximum.z = (std::max)(frame_.worldMaximum.z, point.z);
        frame_.fitPoints.push_back(point);
    };
    const auto includeBox = [&](Vector3 center, Vector3 axisX, Vector3 axisY, Vector3 axisZ) {
        for (uint32_t corner = 0; corner < 8u; ++corner) {
            Vector3 point = center;
            point = Add(point, Scale(axisX, (corner & 1u) != 0u ? 1.0f : -1.0f));
            point = Add(point, Scale(axisY, (corner & 2u) != 0u ? 1.0f : -1.0f));
            point = Add(point, Scale(axisZ, (corner & 4u) != 0u ? 1.0f : -1.0f));
            include(point);
        }
    };

    const RailPath& path = input.rail->RuntimePath();
    for (const CourseTerrainPlacement& placement : input.course->terrainPlacements) {
        if (!placement.editorVisible) continue;
        const bool includedLayer =
            (placement.layer == CourseTerrainLayer::GameplayCollision && settings_.includeGameplayTerrain) ||
            (placement.layer == CourseTerrainLayer::HeroLandmark && settings_.includeHeroLandmarks) ||
            (placement.layer == CourseTerrainLayer::VistaBackground && settings_.includeVistaBackground);
        if (!includedLayer) continue;
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
        includeBox(center,
            Scale(right, (std::max)(1.0f, std::abs(placement.scale.x) * 0.5f)),
            Scale(sample.up, (std::max)(1.0f, std::abs(placement.scale.y) * 0.5f)),
            Scale(forward, (std::max)(1.0f, std::abs(placement.scale.z) * 0.5f)));
        ++frame_.stats.terrainPlacements;
    }

    if (settings_.includeRockClusters) {
        for (const CourseRockCluster& cluster : input.course->rockClusters) {
            if (!cluster.editorVisible) continue;
            const float lateralExtent = cluster.clearLaneRadius + std::abs(cluster.spread.x);
            const float verticalExtent = (std::max)(1.0f, std::abs(cluster.spread.y));
            const float distanceExtent = (std::max)(1.0f, std::abs(cluster.spread.z) * 0.5f);
            for (float distanceSign : {-1.0f, 1.0f}) {
                const float distance = cluster.distance + distanceSign * distanceExtent;
                const RailPathSample sample = path.Evaluate(
                    (std::clamp)(distance, 0.0f, input.rail->Length()));
                include(Add(Add(sample.position, Scale(sample.right, -lateralExtent)),
                    Scale(sample.up, -verticalExtent)));
                include(Add(Add(sample.position, Scale(sample.right, lateralExtent)),
                    Scale(sample.up, verticalExtent)));
            }
            ++frame_.stats.rockClusters;
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
            includeBox(transform.position,
                {std::abs(transform.scale.x) * 0.5f, 0.0f, 0.0f},
                {0.0f, std::abs(transform.scale.y) * 0.5f, 0.0f},
                {0.0f, 0.0f, std::abs(transform.scale.z) * 0.5f});
            ++frame_.stats.sceneStructures;
        }
    }

    if (settings_.includeEnemies && input.enemies != nullptr && input.enemies->IsValid()) {
        for (const CourseEnemyPlacement& placement : input.enemies->Placements()) {
            if (!placement.editorVisible) continue;
            const CourseEnemyPlacementResolution resolved = input.enemies->Resolve(placement);
            if (!resolved.valid) continue;
            includeBox(resolved.worldPosition, {2.0f, 0.0f, 0.0f},
                {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 2.0f});
            ++frame_.stats.enemies;
        }
    }

    if (frame_.fitPoints.empty()) {
        frame_.worldMinimum = {};
        frame_.worldMaximum = {};
    } else if (settings_.worldPadding > 0.0f) {
        const Vector3 padding{settings_.worldPadding,
            settings_.worldPadding, settings_.worldPadding};
        include(Add(frame_.worldMinimum, Scale(padding, -1.0f)));
        include(Add(frame_.worldMaximum, padding));
    }
    frame_.stats.fitPoints = static_cast<uint32_t>(frame_.fitPoints.size());
    frame_.revision = ++lifetimeStats_.builds;
    frame_.stats.builds = lifetimeStats_.builds;
    frame_.stats.cacheHits = lifetimeStats_.cacheHits;
    return frame_;
}

bool CourseMapSceneBoundsSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(worldPadding, 0.0f, 10000.0f) &&
        settings::InRange(maximumFitPoints, 16u, 131072u);
    return settings::Result(valid, errorMessage,
        "CourseMapSceneBoundsSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapSceneBoundsService::SetSettings(
    CourseMapSceneBoundsSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapSceneBoundsService::Invalidate() noexcept {
    cacheValid_ = false;
    frame_ = {};
}

uint64_t CourseMapSceneBoundsService::CourseSignature(const CourseAsset& course) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, course.terrainPlacements.size());
    for (const CourseTerrainPlacement& placement : course.terrainPlacements) {
        hash = HashString(hash, placement.editorGuid);
        hash = HashValue(hash, placement.distance);
        hash = HashValue(hash, placement.lateralOffset);
        hash = HashValue(hash, placement.verticalOffset);
        hash = HashValue(hash, placement.forwardOffset);
        hash = HashValue(hash, placement.scale);
        hash = HashValue(hash, placement.rotation);
        hash = HashValue(hash, placement.layer);
        hash = HashValue(hash, placement.editorVisible);
    }
    hash = HashValue(hash, course.rockClusters.size());
    for (const CourseRockCluster& cluster : course.rockClusters) {
        hash = HashString(hash, cluster.editorGuid);
        hash = HashValue(hash, cluster.distance);
        hash = HashValue(hash, cluster.spread);
        hash = HashValue(hash, cluster.clearLaneRadius);
        hash = HashValue(hash, cluster.editorVisible);
    }
    return hash;
}

bool CourseMapSceneBoundsService::SameKey(
    const CacheKey& a, const CacheKey& b) noexcept {
    return a.courseSignature == b.courseSignature &&
        a.courseRevision == b.courseRevision &&
        a.sceneRevision == b.sceneRevision &&
        a.enemyRevision == b.enemyRevision &&
        a.settingsRevision == b.settingsRevision &&
        a.railGeneration == b.railGeneration &&
        a.enemyGeneration == b.enemyGeneration &&
        a.hasScene == b.hasScene && a.hasEnemies == b.hasEnemies;
}

} // namespace editor
