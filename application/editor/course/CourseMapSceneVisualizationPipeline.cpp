#include "CourseMapSceneVisualizationPipeline.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace editor {
namespace {

constexpr float kEpsilon = 1.0e-5f;

uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) |
        (static_cast<uint32_t>(g) << 8u) |
        (static_cast<uint32_t>(b) << 16u) |
        (static_cast<uint32_t>(a) << 24u);
}

uint8_t Alpha(float opacity) {
    return static_cast<uint8_t>((std::clamp)(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
}

Vector3 Add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(Vector3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector2 Add(Vector2 a, Vector2 b) {
    return {a.x + b.x, a.y + b.y};
}

Vector2 Subtract(Vector2 a, Vector2 b) {
    return {a.x - b.x, a.y - b.y};
}

Vector2 Scale(Vector2 value, float scale) {
    return {value.x * scale, value.y * scale};
}

float LengthSquared(Vector2 value) {
    return value.x * value.x + value.y * value.y;
}

Vector2 Normalize(Vector2 value, Vector2 fallback = {0.0f, -1.0f}) {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kEpsilon) return fallback;
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
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

float HashUnit(std::string_view stableId, uint32_t index, uint32_t channel) {
    uint64_t hash = HashString(1469598103934665603ull, stableId);
    hash = HashValue(hash, index);
    hash = HashValue(hash, channel);
    return static_cast<float>((hash >> 40u) & 0xffffffu) /
        static_cast<float>(0xffffffu);
}

float Cross(Vector2 origin, Vector2 a, Vector2 b) {
    return (a.x - origin.x) * (b.y - origin.y) -
        (a.y - origin.y) * (b.x - origin.x);
}

std::vector<Vector2> ConvexHull(std::vector<Vector2> points) {
    std::sort(points.begin(), points.end(), [](Vector2 a, Vector2 b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    points.erase(std::unique(points.begin(), points.end(), [](Vector2 a, Vector2 b) {
        return std::abs(a.x - b.x) <= 0.05f && std::abs(a.y - b.y) <= 0.05f;
    }), points.end());
    if (points.size() < 3) return points;
    std::vector<Vector2> hull(points.size() * 2u);
    std::size_t count = 0;
    for (Vector2 point : points) {
        while (count >= 2 && Cross(hull[count - 2], hull[count - 1], point) <= 0.0f) --count;
        hull[count++] = point;
    }
    const std::size_t lower = count + 1;
    for (std::size_t index = points.size() - 1; index > 0; --index) {
        const Vector2 point = points[index - 1];
        while (count >= lower && Cross(hull[count - 2], hull[count - 1], point) <= 0.0f) --count;
        hull[count++] = point;
    }
    if (count > 1) --count;
    hull.resize(count);
    return hull;
}

bool Intersects(const std::vector<Vector2>& points, const CourseOverviewMapRect& rect) {
    if (points.empty()) return false;
    float minX = (std::numeric_limits<float>::max)();
    float minY = (std::numeric_limits<float>::max)();
    float maxX = -(std::numeric_limits<float>::max)();
    float maxY = -(std::numeric_limits<float>::max)();
    for (Vector2 point : points) {
        minX = (std::min)(minX, point.x);
        minY = (std::min)(minY, point.y);
        maxX = (std::max)(maxX, point.x);
        maxY = (std::max)(maxY, point.y);
    }
    return minX <= rect.x + rect.width && maxX >= rect.x &&
        minY <= rect.y + rect.height && maxY >= rect.y;
}

float PolygonArea(const std::vector<Vector2>& points) {
    if (points.size() < 3u) return 0.0f;
    float area = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vector2 a = points[i];
        const Vector2 b = points[(i + 1u) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return std::abs(area) * 0.5f;
}

std::vector<Vector2> ProjectBox(
    const CourseOverviewMapProjection& projection,
    Vector3 center,
    Vector3 axisX,
    Vector3 axisY,
    Vector3 axisZ) {
    std::vector<Vector2> points;
    points.reserve(8);
    for (uint32_t corner = 0; corner < 8; ++corner) {
        Vector3 world = center;
        world = Add(world, Scale(axisX, (corner & 1u) != 0 ? 1.0f : -1.0f));
        world = Add(world, Scale(axisY, (corner & 2u) != 0 ? 1.0f : -1.0f));
        world = Add(world, Scale(axisZ, (corner & 4u) != 0 ? 1.0f : -1.0f));
        const auto projected = projection.ProjectWorldScreenOnly(world);
        if (projected.valid) points.push_back(projected.mapPosition);
    }
    return ConvexHull(std::move(points));
}

Vector3 RailLocal(
    const CourseRailAuthoringModel& rail,
    float distance,
    float lateral,
    float vertical) {
    const RailPathSample sample = rail.RuntimePath().Evaluate(
        (std::clamp)(distance, 0.0f, rail.Length()));
    return Add(Add(sample.position, Scale(sample.right, lateral)),
        Scale(sample.up, vertical));
}

void TerrainColors(
    CourseTerrainLayer layer,
    float opacity,
    uint32_t& fill,
    uint32_t& outline,
    CourseMapSceneVisualKind& kind) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision:
        kind = CourseMapSceneVisualKind::GameplayTerrain;
        fill = Color(30, 188, 174, Alpha(opacity * 0.86f));
        outline = Color(77, 255, 229, 205);
        break;
    case CourseTerrainLayer::HeroLandmark:
        kind = CourseMapSceneVisualKind::HeroLandmark;
        fill = Color(33, 133, 162, Alpha(opacity));
        outline = Color(82, 222, 245, 215);
        break;
    case CourseTerrainLayer::VistaBackground:
        kind = CourseMapSceneVisualKind::VistaBackground;
        fill = Color(28, 74, 96, Alpha(opacity * 0.64f));
        outline = Color(57, 147, 176, 145);
        break;
    }
}

Vector3 ParseVector(
    const EditorSceneComponent* component,
    std::string_view name,
    Vector3 fallback) {
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

struct SceneProxyTransform final {
    Vector3 position{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

SceneProxyTransform ResolveSceneProxy(
    const EditorScene& scene,
    const EditorSceneEntity& entity,
    std::unordered_map<std::string, SceneProxyTransform>& cache,
    std::unordered_set<std::string>& visiting) {
    if (const auto found = cache.find(entity.guid); found != cache.end()) return found->second;
    if (!visiting.insert(entity.guid).second) return {};
    const EditorSceneComponent* transform =
        scene.FindComponent(entity, kEditorTransformComponentType);
    SceneProxyTransform result{};
    result.position = ParseVector(transform, "translation", {});
    result.scale = ParseVector(transform, "scale", {1.0f, 1.0f, 1.0f});
    if (!entity.parentGuid.empty()) {
        if (const EditorSceneEntity* parent = scene.FindEntity(entity.parentGuid)) {
            const SceneProxyTransform parentTransform =
                ResolveSceneProxy(scene, *parent, cache, visiting);
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

bool ContainsInsensitive(std::string_view value, std::string_view token) {
    if (token.empty() || value.size() < token.size()) return false;
    for (std::size_t start = 0; start + token.size() <= value.size(); ++start) {
        bool matches = true;
        for (std::size_t index = 0; index < token.size(); ++index) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[start + index])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(token[index])));
            if (a != b) { matches = false; break; }
        }
        if (matches) return true;
    }
    return false;
}

bool SelectedByDomainIndex(
    const EditorSelection* selection,
    EditorDomainId domain,
    uint64_t localIndex) {
    if (selection == nullptr) return false;
    return std::any_of(selection->Handles().begin(), selection->Handles().end(),
        [domain, localIndex](const EditorObjectHandle& handle) {
            return handle.domain == domain && handle.localIndex == localIndex;
        });
}

std::vector<Vector2> ActorSilhouette(
    Vector2 center,
    Vector2 forward,
    float radius,
    std::string_view actorId) {
    const Vector2 side{-forward.y, forward.x};
    const auto point = [&](float along, float across) {
        return Add(center, Add(Scale(forward, along * radius), Scale(side, across * radius)));
    };
    if (ContainsInsensitive(actorId, "turret") || ContainsInsensitive(actorId, "anchor")) {
        return {point(-0.72f, -0.72f), point(0.42f, -0.72f),
            point(0.42f, 0.72f), point(-0.72f, 0.72f)};
    }
    if (ContainsInsensitive(actorId, "boss") || ContainsInsensitive(actorId, "gatekeeper")) {
        return {point(1.05f, 0.0f), point(0.42f, 0.88f), point(-0.58f, 0.82f),
            point(-1.0f, 0.0f), point(-0.58f, -0.82f), point(0.42f, -0.88f)};
    }
    if (ContainsInsensitive(actorId, "probe")) {
        std::vector<Vector2> result;
        for (uint32_t index = 0; index < 10; ++index) {
            const float angle = static_cast<float>(index) * 0.62831853f;
            result.push_back({center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius});
        }
        return result;
    }
    return {point(1.18f, 0.0f), point(-0.15f, 0.40f), point(-0.62f, 1.0f),
        point(-0.82f, 0.22f), point(-0.58f, 0.0f), point(-0.82f, -0.22f),
        point(-0.62f, -1.0f), point(-0.15f, -0.40f)};
}

bool ProjectionSettingsEqual(
    const CourseOverviewMapProjectionSettings& a,
    const CourseOverviewMapProjectionSettings& b) {
    return a.mode == b.mode && a.zoom == b.zoom &&
        a.panPixels.x == b.panPixels.x && a.panPixels.y == b.panPixels.y &&
        a.paddingPixels == b.paddingPixels &&
        a.freeYawRadians == b.freeYawRadians &&
        a.freePitchRadians == b.freePitchRadians &&
        a.fitSamplesPerSegment == b.fitSamplesPerSegment;
}

bool VisualizationSettingsEqual(
    const CourseMapSceneVisualizationSettings& a,
    const CourseMapSceneVisualizationSettings& b) {
    return a.enabled == b.enabled &&
        a.showTerrain == b.showTerrain &&
        a.showRockClusters == b.showRockClusters &&
        a.showSceneStructures == b.showSceneStructures &&
        a.showAuthoredEnemies == b.showAuthoredEnemies &&
        a.showEncounterPreview == b.showEncounterPreview &&
        a.showLabels == b.showLabels &&
        a.hologramGrid == b.hologramGrid &&
        a.terrainOpacity == b.terrainOpacity &&
        a.structureOpacity == b.structureOpacity &&
        a.enemyOpacity == b.enemyOpacity &&
        a.maxTerrainPrimitives == b.maxTerrainPrimitives &&
        a.maxRockInstances == b.maxRockInstances &&
        a.maxSceneStructures == b.maxSceneStructures &&
        a.maxActorProxies == b.maxActorProxies &&
        a.labelBudget == b.labelBudget;
}

} // namespace

const CourseMapSceneVisualizationFrame&
CourseMapSceneVisualizationPipeline::Build(
    const CourseMapSceneVisualizationInput& input) {
    static CourseMapSceneVisualizationFrame invalidFrame{};
    if (input.projection == nullptr || !input.projection->State().valid ||
        input.rail == nullptr || !input.rail->IsValid() || input.course == nullptr) {
        invalidFrame = {};
        invalidFrame.message = "Course scene visualization requires a valid Course and projection.";
        return invalidFrame;
    }

    const std::size_t cacheIndex = static_cast<std::size_t>(
        input.projection->Settings().mode);
    CacheEntry& cache = caches_[(std::min)(cacheIndex, caches_.size() - 1u)];
    FrameKey key{};
    key.courseSignature = ComputeCourseSignature(*input.course);
    key.courseRevision = input.courseRevision;
    key.sceneRevision = input.scene != nullptr ? input.scene->revision : 0;
    key.settingsRevision = settingsRevision_;
    key.assetRevision = assetRevision_;
    key.semanticLodRevision = semanticLod_.SettingsRevision();
    key.selectionRevision = input.selection != nullptr ? input.selection->Revision() : 0;
    key.railGeneration = input.railGeneration;
    key.enemyGeneration = input.enemyGeneration;
    key.projection = input.projection->Settings();
    key.rect = input.projection->State().rect;
    key.hasScene = input.scene != nullptr;
    key.hasEnemies = input.enemies != nullptr;
    key.coarseGeometry = input.coarseGeometry;
    if (cache.valid && SameKey(cache.key, key)) {
        ++lifetimeStats_.cacheHits;
        ++cache.frame.stats.cacheHits;
        return cache.frame;
    }

    cache.key = key;
    cache.frame = BuildFrame(input);
    cache.valid = cache.frame.valid;
    ++lifetimeStats_.builds;
    cache.frame.stats.builds = lifetimeStats_.builds;
    cache.frame.stats.cacheHits = lifetimeStats_.cacheHits;
    return cache.frame;
}

const CourseMapSceneVisualizationFrame*
CourseMapSceneVisualizationPipeline::CurrentFrame(
    CourseOverviewMapProjectionMode mode) const noexcept {
    const std::size_t index = (std::min)(static_cast<std::size_t>(mode),
        caches_.size() - 1u);
    return caches_[index].valid ? &caches_[index].frame : nullptr;
}

bool CourseMapSceneVisualizationSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(terrainOpacity, 0.0f, 1.0f) &&
        settings::InRange(structureOpacity, 0.0f, 1.0f) &&
        settings::InRange(enemyOpacity, 0.0f, 1.0f) &&
        settings::InRange(maxTerrainPrimitives, 1u, 16384u) &&
        settings::InRange(maxRockInstances, 1u, 32768u) &&
        settings::InRange(maxSceneStructures, 1u, 16384u) &&
        settings::InRange(maxActorProxies, 1u, 32768u) &&
        settings::InRange(labelBudget, 0u, 4096u);
    return settings::Result(valid, errorMessage,
        "CourseMapSceneVisualizationSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapSceneVisualizationPipeline::SetSettings(
    CourseMapSceneVisualizationSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (VisualizationSettingsEqual(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapSceneVisualizationPipeline::SetResourceRoot(
    std::filesystem::path resourceRoot) {
    if (resourceRoot.empty() || resourceRoot == resourceRoot_) return;
    resourceRoot_ = std::move(resourceRoot);
    ReloadVisualAssets();
}

void CourseMapSceneVisualizationPipeline::ReloadVisualAssets() {
    waveCache_.clear();
    actorCache_.clear();
    missingWaves_.clear();
    missingActors_.clear();
    ++assetRevision_;
    Invalidate();
}

void CourseMapSceneVisualizationPipeline::Invalidate() {
    for (CacheEntry& cache : caches_) cache.valid = false;
}

EnemyWaveAsset* CourseMapSceneVisualizationPipeline::ResolveWaveAsset(
    std::string_view id) {
    if (id.empty() || missingWaves_.contains(std::string(id))) return nullptr;
    if (auto found = waveCache_.find(std::string(id)); found != waveCache_.end()) {
        return &found->second;
    }
    EnemyWaveAsset asset{};
    const std::filesystem::path path = resourceRoot_ / "waves" /
        (std::string(id) + ".wave");
    if (!asset.LoadFromFile(path.string(), nullptr)) {
        missingWaves_.insert(std::string(id));
        return nullptr;
    }
    return &waveCache_.emplace(std::string(id), std::move(asset)).first->second;
}

CourseActorAsset* CourseMapSceneVisualizationPipeline::ResolveActorAsset(
    std::string_view id) {
    if (id.empty() || missingActors_.contains(std::string(id))) return nullptr;
    if (auto found = actorCache_.find(std::string(id)); found != actorCache_.end()) {
        return &found->second;
    }
    CourseActorAsset asset{};
    const std::filesystem::path path = resourceRoot_ / "actors" /
        (std::string(id) + ".actor");
    if (!asset.LoadFromFile(path.string(), nullptr)) {
        missingActors_.insert(std::string(id));
        return nullptr;
    }
    return &actorCache_.emplace(std::string(id), std::move(asset)).first->second;
}

CourseMapSceneVisualizationFrame
CourseMapSceneVisualizationPipeline::BuildFrame(
    const CourseMapSceneVisualizationInput& input) {
    CourseMapSceneVisualizationFrame frame{};
    frame.valid = true;
    frame.rect = input.projection->State().rect;
    if (!settings_.enabled) {
        frame.message = "Course scene visualization is disabled.";
        return frame;
    }

    const CourseOverviewMapProjection& projection = *input.projection;
    const CourseRailAuthoringModel& rail = *input.rail;
    const RailPath& path = rail.RuntimePath();
    const CourseMapSemanticLODPolicy lod = semanticLod_.Evaluate(projection);
    frame.semanticLod = lod.level;
    std::vector<CourseMapLabelCandidate> labelCandidates;
    const auto addLabel = [&](Vector2 anchor, Vector2 preferred, uint32_t color,
        std::string text, std::string stableId, CourseMapLabelPriority priority,
        bool selected = false) {
        if (!settings_.showLabels || text.empty()) return;
        labelCandidates.push_back({anchor, preferred, color, std::move(text),
            std::move(stableId), selected ? CourseMapLabelPriority::Selected : priority,
            selected, true});
    };

    if (settings_.showTerrain) {
        for (uint32_t placementIndex = 0;
             placementIndex < input.course->terrainPlacements.size();
             ++placementIndex) {
            const CourseTerrainPlacement& placement =
                input.course->terrainPlacements[placementIndex];
            if (!placement.editorVisible ||
                frame.stats.terrainPlacements >= settings_.maxTerrainPrimitives) continue;
            if (placement.layer == CourseTerrainLayer::VistaBackground &&
                !lod.showVistaTerrain) {
                ++frame.stats.lodCulledPolygons;
                continue;
            }
            const float distance = placement.distance + placement.forwardOffset;
            const RailPathSample sample = path.Evaluate(
                (std::clamp)(distance, 0.0f, rail.Length()));
            const Vector3 center = RailLocal(
                rail, distance, placement.lateralOffset, placement.verticalOffset);
            const float yaw = placement.rotation.y * 0.017453292519943295f;
            const float cosine = std::cos(yaw);
            const float sine = std::sin(yaw);
            const Vector3 localRight = Add(Scale(sample.right, cosine), Scale(sample.tangent, sine));
            const Vector3 localForward = Add(Scale(sample.tangent, cosine), Scale(sample.right, -sine));
            const Vector3 axisX = Scale(localRight,
                (std::max)(1.0f, std::abs(placement.scale.x) * 0.5f));
            const Vector3 axisY = Scale(sample.up,
                (std::max)(1.0f, std::abs(placement.scale.y) * 0.5f));
            const Vector3 axisZ = Scale(localForward,
                (std::max)(1.0f, std::abs(placement.scale.z) * 0.5f));
            const auto projectedCenter =
                projection.ProjectWorldScreenOnly(center);
            const bool selected = SelectedByDomainIndex(input.selection,
                EditorDomainId::CourseTerrainPlacement, placementIndex);
            const bool importantLandmark =
                placement.layer == CourseTerrainLayer::HeroLandmark;
            const bool needsReadableProxy =
                placement.layer != CourseTerrainLayer::VistaBackground || selected;
            if (projectedCenter.valid && frame.rect.Contains(projectedCenter.mapPosition) &&
                needsReadableProxy) {
                CourseMapScreenSpaceProxy proxy{};
                proxy.kind = placement.layer == CourseTerrainLayer::HeroLandmark
                    ? CourseMapSceneVisualKind::HeroLandmark
                    : CourseMapSceneVisualKind::GameplayTerrain;
                proxy.center = projectedCenter.mapPosition;
                proxy.worldPosition = center;
                proxy.radiusPixels = selected ? lod.selectedActorRadiusPixels
                    : lod.structureProxyRadiusPixels;
                proxy.fillColor = selected ? Color(58, 212, 138, 230)
                    : Color(40, 156, 183, 185);
                proxy.outlineColor = selected ? Color(220, 255, 236, 255)
                    : Color(118, 231, 246, 235);
                proxy.stableId = "course-terrain:" + placement.editorGuid;
                proxy.displayName = placement.id.empty()
                    ? placement.meshId : placement.id;
                proxy.domain = EditorDomainId::CourseTerrainPlacement;
                proxy.localIndex = placementIndex;
                proxy.selected = selected;
                proxy.locked = placement.editorLocked;
                frame.screenSpaceProxies.push_back(std::move(proxy));
            }
            std::vector<Vector2> polygon = ProjectBox(projection, center, axisX, axisY, axisZ);
            if (polygon.size() < 3 || !Intersects(polygon, frame.rect)) continue;
            if (PolygonArea(polygon) < lod.minimumPolygonAreaPixels) {
                ++frame.stats.lodCulledPolygons;
                continue;
            }
            CourseMapScenePolygon visual{};
            visual.points = std::move(polygon);
            TerrainColors(placement.layer, settings_.terrainOpacity,
                visual.fillColor, visual.outlineColor, visual.kind);
            visual.outlineThickness = placement.collisionMode == CourseTerrainCollisionMode::Solid
                ? 2.2f : placement.collisionMode == CourseTerrainCollisionMode::Proxy
                    ? 1.8f : 1.15f;
            visual.stableId = "course-terrain:" + placement.editorGuid;
            visual.label = placement.id.empty() ? placement.meshId : placement.id;
            visual.locked = placement.editorLocked;
            if (projectedCenter.valid && placement.layer != CourseTerrainLayer::VistaBackground &&
                (lod.showTerrainLabels || importantLandmark)) {
                addLabel(projectedCenter.mapPosition,
                    Add(projectedCenter.mapPosition, {7.0f, 5.0f}),
                    visual.outlineColor, visual.label, visual.stableId,
                    importantLandmark ? CourseMapLabelPriority::Landmark
                        : CourseMapLabelPriority::Decoration);
            }
            if (input.coarseGeometry.terrain) {
                frame.polygons.push_back(std::move(visual));
            }
            ++frame.stats.terrainPlacements;
        }
    }

    if (settings_.showRockClusters && !lod.showRockInstances &&
        input.coarseGeometry.rocks) {
        for (const CourseRockCluster& cluster : input.course->rockClusters) {
            if (!cluster.editorVisible ||
                frame.stats.rockClusterEnvelopes >= settings_.maxTerrainPrimitives) {
                continue;
            }
            const float side = cluster.anchor == CourseRockClusterAnchor::LeftWall
                ? -1.0f
                : cluster.anchor == CourseRockClusterAnchor::RightWall
                    ? 1.0f
                    : cluster.anchor == CourseRockClusterAnchor::VistaWall
                        ? 1.0f : 0.0f;
            const float lateral = side *
                (cluster.clearLaneRadius + cluster.spread.x * 0.5f);
            float vertical = 0.0f;
            if (cluster.anchor == CourseRockClusterAnchor::Floor) {
                vertical -= cluster.spread.y * 0.25f;
            } else if (cluster.anchor == CourseRockClusterAnchor::CeilingBreak) {
                vertical += cluster.spread.y * 0.25f;
            }
            const Vector3 center = RailLocal(
                rail, cluster.distance, lateral, vertical);
            const RailPathSample sample = path.Evaluate(
                (std::clamp)(cluster.distance, 0.0f, rail.Length()));
            std::vector<Vector2> polygon = ProjectBox(projection, center,
                Scale(sample.right, (std::max)(2.0f, cluster.spread.x * 0.5f)),
                Scale(sample.up, (std::max)(2.0f, cluster.spread.y * 0.5f)),
                Scale(sample.tangent, (std::max)(2.0f, cluster.spread.z * 0.5f)));
            if (polygon.size() < 3u || !Intersects(polygon, frame.rect) ||
                PolygonArea(polygon) < lod.minimumPolygonAreaPixels) {
                ++frame.stats.lodCulledPolygons;
                continue;
            }
            frame.polygons.push_back({CourseMapSceneVisualKind::RockCluster,
                std::move(polygon),
                Color(31, 101, 116, Alpha(settings_.structureOpacity * 0.48f)),
                Color(62, 161, 177, 145), 1.15f,
                "course-rock-envelope:" + cluster.editorGuid, {},
                cluster.editorLocked});
            ++frame.stats.rockClusterEnvelopes;
        }
    }

    if (settings_.showRockClusters && lod.showRockInstances &&
        input.coarseGeometry.rocks) {
        for (const CourseRockCluster& cluster : input.course->rockClusters) {
            if (!cluster.editorVisible) continue;
            const uint32_t count = (std::min)(cluster.count,
                settings_.maxRockInstances - frame.stats.rockInstances);
            for (uint32_t index = 0; index < count; ++index) {
                const float side = cluster.anchor == CourseRockClusterAnchor::LeftWall ? -1.0f :
                    cluster.anchor == CourseRockClusterAnchor::RightWall ? 1.0f :
                    cluster.anchor == CourseRockClusterAnchor::VistaWall
                        ? (HashUnit(cluster.editorGuid, index, 0) < 0.5f ? -1.0f : 1.0f)
                        : 0.0f;
                const float lateral = side * (cluster.clearLaneRadius + cluster.spread.x *
                    (0.25f + HashUnit(cluster.editorGuid, index, 1) * 0.5f));
                float vertical = (HashUnit(cluster.editorGuid, index, 2) - 0.5f) * cluster.spread.y;
                if (cluster.anchor == CourseRockClusterAnchor::Floor) vertical -= cluster.spread.y * 0.5f;
                if (cluster.anchor == CourseRockClusterAnchor::CeilingBreak) vertical += cluster.spread.y * 0.5f;
                const float distance = cluster.distance +
                    (HashUnit(cluster.editorGuid, index, 3) - 0.5f) * cluster.spread.z;
                const Vector3 center = RailLocal(rail, distance, lateral, vertical);
                const RailPathSample sample = path.Evaluate((std::clamp)(distance, 0.0f, rail.Length()));
                const float instanceScale = cluster.minScale +
                    (cluster.maxScale - cluster.minScale) * HashUnit(cluster.editorGuid, index, 4);
                std::vector<Vector2> polygon = ProjectBox(projection, center,
                    Scale(sample.right, (std::max)(0.8f, instanceScale * 2.2f)),
                    Scale(sample.up, (std::max)(0.8f, instanceScale * 1.7f)),
                    Scale(sample.tangent, (std::max)(0.8f, instanceScale * 2.0f)));
                if (polygon.size() < 3 || !Intersects(polygon, frame.rect)) continue;
                if (PolygonArea(polygon) < lod.minimumPolygonAreaPixels) {
                    ++frame.stats.lodCulledPolygons;
                    continue;
                }
                frame.polygons.push_back({CourseMapSceneVisualKind::RockCluster,
                    std::move(polygon),
                    Color(42, 117, 132, Alpha(settings_.structureOpacity * 0.78f)),
                    Color(76, 184, 199, 160), 1.0f,
                    "course-rock:" + cluster.editorGuid + ":" + std::to_string(index),
                    {}, cluster.editorLocked});
                ++frame.stats.rockInstances;
            }
            if (frame.stats.rockInstances >= settings_.maxRockInstances) break;
        }
    }

    if (settings_.showSceneStructures && lod.showSceneStructures && input.scene != nullptr) {
        std::unordered_map<std::string, SceneProxyTransform> transforms;
        std::unordered_set<std::string> visiting;
        for (uint32_t entityIndex = 0; entityIndex < input.scene->entities.size();
             ++entityIndex) {
            const EditorSceneEntity& entity = input.scene->entities[entityIndex];
            if (!entity.visible || frame.stats.sceneStructures >= settings_.maxSceneStructures) continue;
            const EditorSceneComponent* mesh =
                input.scene->FindComponent(entity, kEditorMeshRendererComponentType);
            if (mesh == nullptr || !mesh->enabled) continue;
            const SceneProxyTransform transform =
                ResolveSceneProxy(*input.scene, entity, transforms, visiting);
            const auto center =
                projection.ProjectWorldScreenOnly(transform.position);
            const bool selected = SelectedByDomainIndex(input.selection,
                EditorDomainId::SceneEntity, entityIndex);
            if (center.valid && frame.rect.Contains(center.mapPosition)) {
                CourseMapScreenSpaceProxy proxy{};
                proxy.kind = CourseMapSceneVisualKind::SceneStructure;
                proxy.center = center.mapPosition;
                proxy.worldPosition = transform.position;
                proxy.radiusPixels = selected ? lod.selectedActorRadiusPixels
                    : lod.structureProxyRadiusPixels;
                proxy.fillColor = selected ? Color(58, 212, 138, 230)
                    : Color(49, 151, 179, 190);
                proxy.outlineColor = selected ? Color(220, 255, 236, 255)
                    : Color(126, 224, 239, 240);
                proxy.stableId = "scene-structure:" + entity.guid;
                proxy.displayName = entity.name;
                proxy.domain = EditorDomainId::SceneEntity;
                proxy.localIndex = entityIndex;
                proxy.selected = selected;
                proxy.locked = entity.locked;
                frame.screenSpaceProxies.push_back(std::move(proxy));
            }
            std::vector<Vector2> polygon = ProjectBox(projection, transform.position,
                {std::abs(transform.scale.x) * 0.5f, 0.0f, 0.0f},
                {0.0f, std::abs(transform.scale.y) * 0.5f, 0.0f},
                {0.0f, 0.0f, std::abs(transform.scale.z) * 0.5f});
            if (polygon.size() < 3 || !Intersects(polygon, frame.rect)) continue;
            if (PolygonArea(polygon) < lod.minimumPolygonAreaPixels) {
                ++frame.stats.lodCulledPolygons;
                continue;
            }
            if (input.coarseGeometry.structures) {
                frame.polygons.push_back({CourseMapSceneVisualKind::SceneStructure,
                    std::move(polygon),
                    Color(50, 151, 178, Alpha(settings_.structureOpacity)),
                    Color(96, 225, 245, 190), 1.2f,
                    "scene-structure:" + entity.guid, entity.name, entity.locked});
            }
            if (center.valid && lod.showStructureLabels) {
                addLabel(center.mapPosition, Add(center.mapPosition, {7.0f, 5.0f}),
                    Color(126, 224, 239, 205), entity.name,
                    "scene-structure:" + entity.guid,
                    CourseMapLabelPriority::Structure);
            }
            ++frame.stats.sceneStructures;
        }
    }

    const auto addActor = [&](Vector3 center, Vector3 tangent, float radius,
        std::string stableId, std::string actorAssetId, std::string displayName,
        uint32_t color, CourseMapSceneVisualKind kind, bool selected,
        bool enabled, bool locked) {
        if (frame.actors.size() >= settings_.maxActorProxies) return;
        const auto projected = projection.ProjectWorldScreenOnly(center);
        const auto projectedForward = projection.ProjectWorldScreenOnly(
            Add(center, Scale(tangent, (std::max)(4.0f, radius * 3.0f))));
        if (!projected.valid || !projectedForward.valid || !frame.rect.Contains(projected.mapPosition)) return;
        const Vector2 forward = Normalize(
            Subtract(projectedForward.mapPosition, projected.mapPosition));
        const float minimumRadius = selected
            ? lod.selectedActorRadiusPixels : lod.minimumActorRadiusPixels;
        const float radiusPixels = (std::clamp)(
            (std::max)((5.5f + radius * 1.4f) * lod.actorScale,
                minimumRadius), minimumRadius, 24.0f);
        CourseMapSceneActorProxy actor{};
        actor.kind = kind;
        actor.center = projected.mapPosition;
        actor.headingEnd = Add(actor.center, Scale(forward, radiusPixels * 1.55f));
        actor.silhouette = ActorSilhouette(actor.center, forward, radiusPixels, actorAssetId);
        actor.fillColor = color;
        actor.outlineColor = selected ? Color(112, 255, 158, 255) :
            locked ? Color(214, 122, 255, 245) : Color(174, 244, 255, 235);
        actor.radiusPixels = radiusPixels;
        actor.stableId = std::move(stableId);
        actor.actorAssetId = std::move(actorAssetId);
        actor.displayName = std::move(displayName);
        actor.selected = selected;
        actor.enabled = enabled;
        actor.locked = locked;
        actor.worldPosition = center;
        actor.worldHeadingEnd = Add(center,
            Scale(tangent, (std::max)(4.0f, radius * 3.0f)));
        frame.actors.push_back(std::move(actor));
    };

    if (settings_.showAuthoredEnemies && input.enemies != nullptr && input.enemies->IsValid()) {
        const auto& placements = input.enemies->Placements();
        for (uint32_t index = 0; index < placements.size(); ++index) {
            const CourseEnemyPlacement& placement = placements[index];
            if (!placement.editorVisible) continue;
            const CourseEnemyPlacementResolution resolved = input.enemies->Resolve(placement);
            if (!resolved.valid) continue;
            CourseActorAsset* asset = ResolveActorAsset(placement.actorAssetId);
            const float radius = asset != nullptr ? asset->radius : 1.2f;
            const std::string displayName = asset != nullptr && !asset->displayName.empty()
                ? asset->displayName : placement.actorAssetId;
            const EditorObjectHandle handle{EditorDomainId::CourseEnemyPlacement,
                "course-enemy-placement:" + placement.editorGuid, index,
                input.enemyGeneration, displayName};
            const bool selected = input.selection != nullptr && input.selection->Contains(handle);
            uint32_t color = Color(53, 208, 231, Alpha(settings_.enemyOpacity));
            if (!placement.enabled) color = Color(100, 110, 116, 170);
            addActor(resolved.worldPosition, resolved.railSample.tangent, radius,
                handle.stableId, placement.actorAssetId, displayName, color,
                CourseMapSceneVisualKind::AuthoredEnemy, selected,
                placement.enabled, placement.editorLocked);
            ++frame.stats.authoredEnemies;
        }
    }

    if (settings_.showEncounterPreview) {
        for (const CourseEventMarker& event : input.course->events) {
            if (!event.editorVisible || event.type != "enemy_wave") continue;
            EnemyWaveAsset* wave = ResolveWaveAsset(event.id);
            if (wave == nullptr) continue;
            for (uint32_t unitIndex = 0; unitIndex < wave->units.size(); ++unitIndex) {
                const EnemyWaveUnit& unit = wave->units[unitIndex];
                const float distance = event.distance + unit.distanceOffset;
                const RailPathSample sample = path.Evaluate((std::clamp)(distance, 0.0f, rail.Length()));
                const Vector3 center = RailLocal(
                    rail, distance, unit.lateralOffset, unit.verticalOffset);
                CourseActorAsset* asset = ResolveActorAsset(unit.actorAssetId);
                const float radius = asset != nullptr ? asset->radius : unit.radius;
                const std::string actorId = !unit.actorAssetId.empty()
                    ? unit.actorAssetId : unit.role;
                const std::string displayName = asset != nullptr && !asset->displayName.empty()
                    ? asset->displayName : actorId;
                const uint8_t red = static_cast<uint8_t>((std::clamp)(unit.color.x, 0.0f, 1.0f) * 255.0f);
                const uint8_t green = static_cast<uint8_t>((std::clamp)(unit.color.y, 0.0f, 1.0f) * 255.0f);
                const uint8_t blue = static_cast<uint8_t>((std::clamp)(unit.color.z, 0.0f, 1.0f) * 255.0f);
                addActor(center, sample.tangent, radius,
                    "encounter:" + event.editorGuid + ":" + std::to_string(unitIndex),
                    actorId, displayName,
                    Color(red, green, blue, Alpha(settings_.enemyOpacity * 0.78f)),
                    CourseMapSceneVisualKind::EncounterEnemy,
                    false, true, event.editorLocked);
                ++frame.stats.encounterEnemies;
            }
            if (frame.actors.size() >= settings_.maxActorProxies) break;
        }
    }

    if (lod.clusterEncounterEnemies) {
        struct Cluster final {
            CourseMapSceneActorProxy actor{};
            Vector2 sum{};
            Vector3 worldSum{};
        };
        std::unordered_map<uint64_t, std::size_t> clusterLookup;
        std::vector<Cluster> clusters;
        std::vector<CourseMapSceneActorProxy> retained;
        retained.reserve(frame.actors.size());
        const float cell = (std::max)(16.0f, lod.encounterClusterCellPixels);
        for (CourseMapSceneActorProxy& actor : frame.actors) {
            if (actor.kind != CourseMapSceneVisualKind::EncounterEnemy || actor.selected) {
                retained.push_back(std::move(actor));
                continue;
            }
            const int x = static_cast<int>(std::floor((actor.center.x - frame.rect.x) / cell));
            const int y = static_cast<int>(std::floor((actor.center.y - frame.rect.y) / cell));
            const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u) |
                static_cast<uint32_t>(y);
            auto found = clusterLookup.find(key);
            if (found == clusterLookup.end()) {
                Cluster cluster{};
                cluster.actor = std::move(actor);
                cluster.actor.stableId = "encounter-cluster:" + std::to_string(x) + ":" + std::to_string(y);
                cluster.actor.actorAssetId = "encounter_cluster";
                cluster.actor.clusterCount = 1u;
                cluster.sum = cluster.actor.center;
                cluster.worldSum = cluster.actor.worldPosition;
                clusterLookup.emplace(key, clusters.size());
                clusters.push_back(std::move(cluster));
            } else {
                Cluster& cluster = clusters[found->second];
                cluster.sum = Add(cluster.sum, actor.center);
                cluster.worldSum = Add(cluster.worldSum, actor.worldPosition);
                ++cluster.actor.clusterCount;
            }
        }
        for (Cluster& cluster : clusters) {
            const float inverse = 1.0f / static_cast<float>(cluster.actor.clusterCount);
            const Vector3 worldHeading = Subtract(
                cluster.actor.worldHeadingEnd, cluster.actor.worldPosition);
            cluster.actor.center = Scale(cluster.sum, inverse);
            cluster.actor.worldPosition = Scale(cluster.worldSum, inverse);
            cluster.actor.worldHeadingEnd = Add(
                cluster.actor.worldPosition, worldHeading);
            cluster.actor.radiusPixels = (std::clamp)(
                6.0f + std::sqrt(static_cast<float>(cluster.actor.clusterCount)) * 1.8f,
                7.0f, 18.0f);
            cluster.actor.displayName = std::to_string(cluster.actor.clusterCount) + " enemies";
            cluster.actor.headingEnd = Add(cluster.actor.center,
                {0.0f, -cluster.actor.radiusPixels * 1.5f});
            cluster.actor.silhouette = ActorSilhouette(cluster.actor.center,
                {0.0f, -1.0f}, cluster.actor.radiusPixels, "probe");
            retained.push_back(std::move(cluster.actor));
            ++frame.stats.encounterClusters;
        }
        frame.stats.lodCulledActors += static_cast<uint32_t>(
            frame.actors.size() > retained.size() ? frame.actors.size() - retained.size() : 0u);
        frame.actors = std::move(retained);
    } else if (!lod.showIndividualEncounterEnemies) {
        const std::size_t before = frame.actors.size();
        frame.actors.erase(std::remove_if(frame.actors.begin(), frame.actors.end(),
            [](const CourseMapSceneActorProxy& actor) {
                return actor.kind == CourseMapSceneVisualKind::EncounterEnemy && !actor.selected;
            }), frame.actors.end());
        frame.stats.lodCulledActors += static_cast<uint32_t>(before - frame.actors.size());
    }

    for (const CourseMapSceneActorProxy& actor : frame.actors) {
        const bool cluster = actor.clusterCount > 1u;
        const bool show = actor.selected || cluster ||
            (actor.kind == CourseMapSceneVisualKind::AuthoredEnemy && lod.showAuthoredEnemyLabels) ||
            (actor.kind == CourseMapSceneVisualKind::EncounterEnemy && lod.showEncounterEnemyLabels);
        if (!show) continue;
        addLabel(actor.center,
            Add(actor.center, {actor.radiusPixels + 5.0f, -actor.radiusPixels}),
            actor.outlineColor, actor.displayName, actor.stableId,
            cluster ? CourseMapLabelPriority::Wave : CourseMapLabelPriority::Enemy,
            actor.selected);
    }

    if (settings_.showLabels && settings_.labelBudget > 0u &&
        !labelCandidates.empty()) {
        CourseMapLabelLayoutSettings layoutSettings = labelLayout_.Settings();
        layoutSettings.maximumLabels = (std::min)(settings_.labelBudget, lod.labelBudget);
        labelLayout_.SetSettings(layoutSettings);
        const CourseMapLabelLayoutFrame& layout =
            labelLayout_.Build(labelCandidates, frame.rect);
        frame.labels = layout.labels;
        frame.stats.labels = static_cast<uint32_t>(frame.labels.size());
        frame.stats.labelsSuppressed = static_cast<uint32_t>(labelCandidates.size()) -
            frame.stats.labels;
    }

    frame.message = "Scene visualization [" +
        std::string(ToString(frame.semanticLod)) + "]: " +
        std::to_string(frame.stats.terrainPlacements) + " terrain, " +
        std::to_string(frame.stats.rockInstances) + " rocks, " +
        std::to_string(frame.stats.authoredEnemies + frame.stats.encounterEnemies) +
        " enemy proxies.";
    return frame;
}

uint64_t CourseMapSceneVisualizationPipeline::ComputeCourseSignature(
    const CourseAsset& course) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, course.terrainPlacements.size());
    hash = HashValue(hash, course.rockClusters.size());
    hash = HashValue(hash, course.enemyPlacements.size());
    hash = HashValue(hash, course.events.size());
    for (const CourseTerrainPlacement& value : course.terrainPlacements) {
        hash = HashString(hash, value.editorGuid);
        hash = HashString(hash, value.id);
        hash = HashString(hash, value.meshId);
        hash = HashValue(hash, value.distance);
        hash = HashValue(hash, value.lateralOffset);
        hash = HashValue(hash, value.verticalOffset);
        hash = HashValue(hash, value.forwardOffset);
        hash = HashValue(hash, value.scale);
        hash = HashValue(hash, value.rotation);
        hash = HashValue(hash, value.layer);
        hash = HashValue(hash, value.editorVisible);
    }
    for (const CourseRockCluster& value : course.rockClusters) {
        hash = HashString(hash, value.editorGuid);
        hash = HashString(hash, value.meshId);
        hash = HashValue(hash, value.distance);
        hash = HashValue(hash, value.count);
        hash = HashValue(hash, value.spread);
        hash = HashValue(hash, value.minScale);
        hash = HashValue(hash, value.maxScale);
        hash = HashValue(hash, value.editorVisible);
    }
    for (const CourseEnemyPlacement& value : course.enemyPlacements) {
        hash = HashString(hash, value.editorGuid);
        hash = HashString(hash, value.actorAssetId);
        hash = HashString(hash, value.railAnchor.segmentGuid);
        hash = HashValue(hash, value.railAnchor.normalizedT);
        hash = HashValue(hash, value.railAnchor.lateralOffset);
        hash = HashValue(hash, value.railAnchor.verticalOffset);
        hash = HashValue(hash, value.railAnchor.forwardOffset);
        hash = HashValue(hash, value.enabled);
        hash = HashValue(hash, value.editorVisible);
    }
    for (const CourseEventMarker& value : course.events) {
        hash = HashString(hash, value.editorGuid);
        hash = HashString(hash, value.type);
        hash = HashString(hash, value.id);
        hash = HashValue(hash, value.distance);
        hash = HashValue(hash, value.editorVisible);
    }
    return hash;
}

bool CourseMapSceneVisualizationPipeline::SameKey(
    const FrameKey& lhs,
    const FrameKey& rhs) noexcept {
    return lhs.courseSignature == rhs.courseSignature &&
        lhs.courseRevision == rhs.courseRevision &&
        lhs.sceneRevision == rhs.sceneRevision &&
        lhs.settingsRevision == rhs.settingsRevision &&
        lhs.assetRevision == rhs.assetRevision &&
        lhs.semanticLodRevision == rhs.semanticLodRevision &&
        lhs.selectionRevision == rhs.selectionRevision &&
        lhs.railGeneration == rhs.railGeneration &&
        lhs.enemyGeneration == rhs.enemyGeneration &&
        lhs.hasScene == rhs.hasScene && lhs.hasEnemies == rhs.hasEnemies &&
        lhs.coarseGeometry.terrain == rhs.coarseGeometry.terrain &&
        lhs.coarseGeometry.rocks == rhs.coarseGeometry.rocks &&
        lhs.coarseGeometry.structures == rhs.coarseGeometry.structures &&
        lhs.rect.x == rhs.rect.x && lhs.rect.y == rhs.rect.y &&
        lhs.rect.width == rhs.rect.width && lhs.rect.height == rhs.rect.height &&
        ProjectionSettingsEqual(lhs.projection, rhs.projection);
}

const char* ToString(CourseMapSceneVisualKind kind) noexcept {
    switch (kind) {
    case CourseMapSceneVisualKind::GameplayTerrain: return "Gameplay Terrain";
    case CourseMapSceneVisualKind::HeroLandmark: return "Hero Landmark";
    case CourseMapSceneVisualKind::VistaBackground: return "Vista Background";
    case CourseMapSceneVisualKind::RockCluster: return "Rock Cluster";
    case CourseMapSceneVisualKind::SceneStructure: return "Scene Structure";
    case CourseMapSceneVisualKind::AuthoredEnemy: return "Authored Enemy";
    case CourseMapSceneVisualKind::EncounterEnemy: return "Encounter Preview Enemy";
    }
    return "Unknown";
}

} // namespace editor
