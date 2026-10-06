#include "CourseTerrainMapRenderer.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

uint32_t Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    return static_cast<uint32_t>(red) |
        (static_cast<uint32_t>(green) << 8u) |
        (static_cast<uint32_t>(blue) << 16u) |
        (static_cast<uint32_t>(alpha) << 24u);
}

uint8_t Alpha(float value) {
    return static_cast<uint8_t>((std::clamp)(value, 0.0f, 1.0f) * 255.0f);
}

bool SameRect(CourseOverviewMapRect a, CourseOverviewMapRect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width &&
        a.height == b.height;
}

bool SameProjection(
    const CourseOverviewMapProjectionSettings& a,
    const CourseOverviewMapProjectionSettings& b) {
    return a.mode == b.mode && a.zoom == b.zoom &&
        a.panPixels.x == b.panPixels.x && a.panPixels.y == b.panPixels.y &&
        a.paddingPixels == b.paddingPixels &&
        a.freeYawRadians == b.freeYawRadians &&
        a.freePitchRadians == b.freePitchRadians &&
        a.fitSamplesPerSegment == b.fitSamplesPerSegment;
}

std::array<Vector3, 8> BoxCorners(Vector3 minimum, Vector3 maximum) {
    return {{
        {minimum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, minimum.z},
        {minimum.x, maximum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z},
        {minimum.x, minimum.y, maximum.z},
        {maximum.x, minimum.y, maximum.z},
        {minimum.x, maximum.y, maximum.z},
        {maximum.x, maximum.y, maximum.z},
    }};
}

bool ProjectedIntersects(
    Vector3 minimum,
    Vector3 maximum,
    const CourseOverviewMapProjection& projection) {
    float minX = (std::numeric_limits<float>::max)();
    float minY = minX;
    float maxX = -minX;
    float maxY = -minX;
    bool any = false;
    for (Vector3 corner : BoxCorners(minimum, maximum)) {
        const CourseOverviewMapProjectedPoint point =
            projection.ProjectWorldScreenOnly(corner);
        if (!point.valid) continue;
        any = true;
        minX = (std::min)(minX, point.mapPosition.x);
        minY = (std::min)(minY, point.mapPosition.y);
        maxX = (std::max)(maxX, point.mapPosition.x);
        maxY = (std::max)(maxY, point.mapPosition.y);
    }
    if (!any) return false;
    const CourseOverviewMapRect rect = projection.State().rect;
    return maxX >= rect.x && minX <= rect.x + rect.width &&
        maxY >= rect.y && minY <= rect.y + rect.height;
}

float ScreenArea(Vector2 a, Vector2 b, Vector2 c) {
    return std::abs((b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x)) * 0.5f;
}

uint32_t TriangleBudget(
    CourseMapSemanticLODLevel level,
    const CourseTerrainMapRenderSettings& settings) {
    switch (level) {
    case CourseMapSemanticLODLevel::Course: return settings.courseTriangleBudget;
    case CourseMapSemanticLODLevel::Region: return settings.regionTriangleBudget;
    case CourseMapSemanticLODLevel::Detail: return settings.detailTriangleBudget;
    case CourseMapSemanticLODLevel::Inspect: return settings.inspectTriangleBudget;
    }
    return settings.courseTriangleBudget;
}

uint32_t SourceLod(CourseMapSemanticLODLevel level) {
    if (level == CourseMapSemanticLODLevel::Course) return 0u;
    if (level == CourseMapSemanticLODLevel::Region) return 1u;
    return 2u;
}

} // namespace

const CourseTerrainMapFrame& CourseTerrainMapRenderer::Build(
    const CourseTerrainMapAsset* asset,
    const CourseOverviewMapProjection& projection,
    CourseTerrainMapBuildOptions options) {
    const std::size_t cacheIndex = static_cast<std::size_t>(
        projection.Settings().mode);
    CacheEntry& cache = caches_[cacheIndex];
    if (!settings_.enabled || asset == nullptr || asset->Empty() ||
        !projection.State().valid) {
        CourseTerrainMapFrame& fallback = fallbackFrames_[cacheIndex];
        fallback = {};
        fallback.rect = projection.State().rect;
        fallback.message = !settings_.enabled
            ? "Course terrain map rendering is disabled."
            : "Course terrain map asset is not ready.";
        return fallback;
    }
    const CourseMapSemanticLODPolicy lod = semanticLod_.Evaluate(projection);
    const FrameKey key{asset->sourceFingerprint, asset->contentRevision,
        settingsRevision_, semanticLod_.SettingsRevision(),
        projection.Settings(), projection.State().rect};
    if (cache.valid && SameKey(cache.key, key)) {
        ++lifetimeStats_.cacheHits;
        cache.frame.stats = lifetimeStats_;
        return cache.frame;
    }
    if (options.interactivePan && cache.valid) {
        ++lifetimeStats_.interactivePanDeferrals;
        cache.frame.stats = lifetimeStats_;
        return cache.frame;
    }
    cache.frame = BuildFrame(*asset, projection, settings_, lod);
    cache.key = key;
    cache.valid = cache.frame.valid;
    ++lifetimeStats_.builds;
    lifetimeStats_.sourceTiles = cache.frame.stats.sourceTiles;
    lifetimeStats_.visibleTiles = cache.frame.stats.visibleTiles;
    lifetimeStats_.culledTiles = cache.frame.stats.culledTiles;
    lifetimeStats_.inspectedTriangles = cache.frame.stats.inspectedTriangles;
    lifetimeStats_.emittedTriangles = cache.frame.stats.emittedTriangles;
    lifetimeStats_.budgetCulledTriangles = cache.frame.stats.budgetCulledTriangles;
    cache.frame.stats = lifetimeStats_;
    return cache.frame;
}

bool CourseTerrainMapRenderSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(opacity, .02f, .95f) &&
        settings::InRange(courseTriangleBudget, 100u, 100000u) &&
        settings::InRange(regionTriangleBudget, 100u, 200000u) &&
        settings::InRange(detailTriangleBudget, 100u, 400000u) &&
        settings::InRange(inspectTriangleBudget, 100u, 800000u);
    return settings::Result(valid, errorMessage,
        "CourseTerrainMapRenderSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseTerrainMapRenderer::SetSettings(
    CourseTerrainMapRenderSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseTerrainMapRenderer::Invalidate() noexcept {
    for (CacheEntry& cache : caches_) cache = {};
}

CourseTerrainMapFrame CourseTerrainMapRenderer::BuildFrame(
    const CourseTerrainMapAsset& asset,
    const CourseOverviewMapProjection& projection,
    const CourseTerrainMapRenderSettings& settings,
    const CourseMapSemanticLODPolicy& lod) {
    CourseTerrainMapFrame frame{};
    frame.valid = true;
    frame.rect = projection.State().rect;
    frame.projectionMode = projection.Settings().mode;
    frame.semanticLod = lod.level;
    frame.sourceLod = SourceLod(lod.level);
    const CourseTerrainMapLod* source = asset.FindLod(frame.sourceLod);
    if (source == nullptr && !asset.lods.empty()) source = &asset.lods.front();
    if (source == nullptr) {
        frame.valid = false;
        frame.message = "Course terrain map has no compatible LOD.";
        return frame;
    }
    frame.stats.sourceTiles = static_cast<uint32_t>(source->tiles.size());
    const uint32_t budget = TriangleBudget(lod.level, settings);
    frame.triangles.reserve(budget);
    for (const CourseTerrainMapTile& tile : source->tiles) {
        if (!ProjectedIntersects(tile.worldMinimum, tile.worldMaximum, projection)) {
            ++frame.stats.culledTiles;
            continue;
        }
        ++frame.stats.visibleTiles;
        const std::size_t triangleCount = tile.indices.size() / 3u;
        const uint32_t remaining = budget > frame.stats.inspectedTriangles
            ? budget - frame.stats.inspectedTriangles : 0u;
        const std::size_t inspectCount = (std::min)(triangleCount,
            static_cast<std::size_t>(remaining));
        frame.stats.budgetCulledTriangles += static_cast<uint32_t>(
            triangleCount - inspectCount);
        if (settings.showSurface) {
            for (std::size_t inspected = 0; inspected < inspectCount; ++inspected) {
                ++frame.stats.inspectedTriangles;
                const std::size_t triangle = inspectCount == triangleCount
                    ? inspected : inspected * triangleCount / inspectCount;
                const std::size_t offset = triangle * 3u;
                const CourseTerrainMapVertex& va = tile.vertices[tile.indices[offset]];
                const CourseTerrainMapVertex& vb = tile.vertices[tile.indices[offset + 1u]];
                const CourseTerrainMapVertex& vc = tile.vertices[tile.indices[offset + 2u]];
                const auto a = projection.ProjectWorldScreenOnly(va.position);
                const auto b = projection.ProjectWorldScreenOnly(vb.position);
                const auto c = projection.ProjectWorldScreenOnly(vc.position);
                if (!a.valid || !b.valid || !c.valid ||
                    ScreenArea(a.mapPosition, b.mapPosition, c.mapPosition) < 0.035f) {
                    continue;
                }
                const float upward = (std::clamp)(std::abs(
                    (va.normal.y + vb.normal.y + vc.normal.y) / 3.0f), 0.0f, 1.0f);
                const uint8_t shade = static_cast<uint8_t>(upward * 24.0f);
                frame.triangles.push_back({a.mapPosition, b.mapPosition,
                    c.mapPosition,
                    Color(static_cast<uint8_t>(20u + shade),
                        static_cast<uint8_t>(74u + shade),
                        static_cast<uint8_t>(88u + shade), Alpha(settings.opacity)),
                    (a.depth + b.depth + c.depth) / 3.0f});
            }
        }
        if (settings.showContours && !tile.vertices.empty()) {
            const uint32_t columns = source->radialSegments + 1u;
            if (columns > 1u && tile.vertices.size() >= columns) {
                const uint32_t rows = static_cast<uint32_t>(tile.vertices.size() / columns);
                for (uint32_t contourIndex : {0u, source->radialSegments / 2u}) {
                    CourseTerrainMapPolyline contour{};
                    contour.color = Color(78, 188, 205, 110);
                    contour.thickness = 1.0f;
                    for (uint32_t row = 0; row < rows; ++row) {
                        const auto point = projection.ProjectWorldScreenOnly(
                            tile.vertices[row * columns + contourIndex].position);
                        if (point.valid) contour.points.push_back(point.mapPosition);
                    }
                    if (contour.points.size() >= 2u) {
                        frame.contours.push_back(std::move(contour));
                    }
                }
            }
        }
    }
    std::stable_sort(frame.triangles.begin(), frame.triangles.end(),
        [](const CourseTerrainMapTriangle& a,
           const CourseTerrainMapTriangle& b) { return a.depth > b.depth; });
    frame.stats.emittedTriangles = static_cast<uint32_t>(frame.triangles.size());
    frame.message = "Terrain Map [" + std::string(ToString(lod.level)) +
        "]: " + std::to_string(frame.stats.visibleTiles) + " tiles, " +
        std::to_string(frame.stats.emittedTriangles) + " triangles.";
    return frame;
}

bool CourseTerrainMapRenderer::SameKey(
    const FrameKey& a,
    const FrameKey& b) noexcept {
    return a.sourceFingerprint == b.sourceFingerprint &&
        a.contentRevision == b.contentRevision &&
        a.settingsRevision == b.settingsRevision &&
        a.semanticLodRevision == b.semanticLodRevision &&
        SameProjection(a.projection, b.projection) && SameRect(a.rect, b.rect);
}

} // namespace editor
