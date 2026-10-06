#include "CourseMapHybridCartographyCompositor.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace editor {
namespace {

constexpr std::size_t kLayerCount =
    static_cast<std::size_t>(CourseMapHybridGeometryLayer::Count);

float PolygonArea(const std::vector<Vector2>& points) {
    if (points.size() < 3u) return 0.0f;
    double twiceArea = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vector2 a = points[i];
        const Vector2 b = points[(i + 1u) % points.size()];
        twiceArea += static_cast<double>(a.x) * b.y -
            static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(std::abs(twiceArea) * 0.5);
}

float CoverageThreshold(std::size_t layer,
    const CourseMapHybridCartographySettings& settings) {
    if (layer == static_cast<std::size_t>(
            CourseMapHybridGeometryLayer::Rocks)) {
        return settings.rockCoverageThreshold;
    }
    if (layer == static_cast<std::size_t>(
            CourseMapHybridGeometryLayer::Structures)) {
        return settings.structureCoverageThreshold;
    }
    return settings.terrainCoverageThreshold;
}

} // namespace

const CourseMapHybridCartographyFrame&
CourseMapHybridCartographyCompositor::Compose(
    const CourseMapCartographyFrame* cartography) {
    const uint64_t generation = cartography != nullptr
        ? cartography->generation : 0u;
    const std::size_t cacheIndex = cartography != nullptr
        ? (std::min)(static_cast<std::size_t>(cartography->projectionMode),
            caches_.size() - 1u)
        : 0u;
    CacheEntry& cache = caches_[cacheIndex];
    if (cache.source == cartography && cache.generation == generation &&
        cache.settingsRevision == settingsRevision_) {
        return cache.frame;
    }
    cache.frame = BuildFrame(cartography);
    cache.source = cartography;
    cache.generation = generation;
    cache.settingsRevision = settingsRevision_;
    return cache.frame;
}

CourseMapHybridCartographyFrame
CourseMapHybridCartographyCompositor::BuildFrame(
    const CourseMapCartographyFrame* cartography) const {
    CourseMapHybridCartographyFrame result{};
    const bool exactFrameAvailable = cartography != nullptr &&
        cartography->valid && !cartography->fallbackRequested;
    result.valid = true;
    result.drawCartography = exactFrameAvailable;
    result.requestHologramFallback = !exactFrameAvailable;
    result.semanticLod = exactFrameAvailable
        ? cartography->semanticLod : CourseMapSemanticLODLevel::Course;
    result.sourceGeneration = exactFrameAvailable ? cartography->generation : 0u;

    if (!settings_.enabled || !exactFrameAvailable) {
        const bool showCoarse = !exactFrameAvailable;
        result.coarseGeometry = {showCoarse, showCoarse, showCoarse};
        for (CourseMapHybridLayerCoverage& layer : result.layers) {
            layer.drawCoarseGeometry = showCoarse;
        }
        result.message = !settings_.enabled
            ? "Hybrid cartography disabled; using exclusive exact/fallback rendering."
            : "Exact cartography unavailable; semantic geometry and hologram remain visible.";
        return result;
    }

    const CourseOverviewMapRect rect = cartography->rect;
    const float viewportArea = (std::max)(1.0f, rect.width * rect.height);
    const uint32_t gridResolution = (std::clamp)(
        settings_.coverageGridResolution, 8u, 128u);
    const std::size_t gridCells = static_cast<std::size_t>(gridResolution) *
        static_cast<std::size_t>(gridResolution);
    std::array<std::vector<uint8_t>, kLayerCount> occupied{};
    for (auto& grid : occupied) grid.assign(gridCells, 0u);

    for (const CourseMapCartographyOutline& outline : cartography->outlines) {
        if (!outline.exactSourceGeometry || outline.points.size() < 3u) continue;
        const std::size_t layer = LayerIndex(outline.layer);
        CourseMapHybridLayerCoverage& coverage = result.layers[layer];
        ++coverage.exactRegions;
        coverage.drawExactGeometry = true;
        coverage.largestRegionScreenRatio = (std::max)(
            coverage.largestRegionScreenRatio,
            (std::min)(1.0f, PolygonArea(outline.points) / viewportArea));

        float minimumX = outline.points.front().x;
        float maximumX = minimumX;
        float minimumY = outline.points.front().y;
        float maximumY = minimumY;
        for (Vector2 point : outline.points) {
            minimumX = (std::min)(minimumX, point.x);
            maximumX = (std::max)(maximumX, point.x);
            minimumY = (std::min)(minimumY, point.y);
            maximumY = (std::max)(maximumY, point.y);
        }
        minimumX = (std::clamp)(minimumX, rect.x, rect.x + rect.width);
        maximumX = (std::clamp)(maximumX, rect.x, rect.x + rect.width);
        minimumY = (std::clamp)(minimumY, rect.y, rect.y + rect.height);
        maximumY = (std::clamp)(maximumY, rect.y, rect.y + rect.height);
        if (maximumX <= minimumX || maximumY <= minimumY) continue;
        const auto toCell = [gridResolution](float normalized) {
            const float scaled =
                (std::clamp)(normalized, 0.0f, 0.999999f) *
                static_cast<float>(gridResolution);
            return (std::min)(gridResolution - 1u,
                static_cast<uint32_t>(scaled));
        };
        const uint32_t firstX = toCell((minimumX - rect.x) / rect.width);
        const uint32_t lastX = toCell((maximumX - rect.x) / rect.width);
        const uint32_t firstY = toCell((minimumY - rect.y) / rect.height);
        const uint32_t lastY = toCell((maximumY - rect.y) / rect.height);
        for (uint32_t y = firstY; y <= lastY; ++y) {
            for (uint32_t x = firstX; x <= lastX; ++x) {
                occupied[layer][static_cast<std::size_t>(y) * gridResolution + x] = 1u;
            }
        }
    }

    const bool forceCoarse =
        (result.semanticLod == CourseMapSemanticLODLevel::Course &&
            settings_.alwaysKeepCoarseAtCourseLod) ||
        (result.semanticLod == CourseMapSemanticLODLevel::Region &&
            settings_.alwaysKeepCoarseAtRegionLod);
    for (std::size_t layer = 0; layer < result.layers.size(); ++layer) {
        CourseMapHybridLayerCoverage& coverage = result.layers[layer];
        const std::size_t count = static_cast<std::size_t>(std::count(
            occupied[layer].begin(), occupied[layer].end(), uint8_t{1u}));
        coverage.occupiedScreenRatio = static_cast<float>(count) /
            static_cast<float>(gridCells);
        const bool insufficientCoverage = coverage.exactRegions == 0u ||
            (coverage.occupiedScreenRatio < CoverageThreshold(layer, settings_) &&
                coverage.largestRegionScreenRatio <
                    settings_.largestRegionThreshold);
        coverage.drawCoarseGeometry = forceCoarse || insufficientCoverage;
    }
    result.coarseGeometry.terrain = result.layers[static_cast<std::size_t>(
        CourseMapHybridGeometryLayer::Terrain)].drawCoarseGeometry;
    result.coarseGeometry.rocks = result.layers[static_cast<std::size_t>(
        CourseMapHybridGeometryLayer::Rocks)].drawCoarseGeometry;
    result.coarseGeometry.structures = result.layers[static_cast<std::size_t>(
        CourseMapHybridGeometryLayer::Structures)].drawCoarseGeometry;
    result.message = "Hybrid cartography composites semantic silhouettes under "
        "screen-significant exact geometry.";
    return result;
}

bool CourseMapHybridCartographySettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(coverageGridResolution, 8u, 128u) &&
        settings::InRange(terrainCoverageThreshold, 0.0f, 1.0f) &&
        settings::InRange(rockCoverageThreshold, 0.0f, 1.0f) &&
        settings::InRange(structureCoverageThreshold, 0.0f, 1.0f) &&
        settings::InRange(largestRegionThreshold, 0.0f, 1.0f);
    return settings::Result(valid, errorMessage,
        "CourseMapHybridCartographySettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapHybridCartographyCompositor::SetSettings(
    CourseMapHybridCartographySettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapHybridCartographyCompositor::Invalidate() noexcept {
    for (CacheEntry& cache : caches_) cache = {};
}

std::size_t CourseMapHybridCartographyCompositor::LayerIndex(
    CourseMapVisualLayer layer) noexcept {
    switch (layer) {
    case CourseMapVisualLayer::RockMass:
        return static_cast<std::size_t>(CourseMapHybridGeometryLayer::Rocks);
    case CourseMapVisualLayer::SceneStructure:
        return static_cast<std::size_t>(CourseMapHybridGeometryLayer::Structures);
    case CourseMapVisualLayer::GameplayTerrain:
    case CourseMapVisualLayer::HeroLandmark:
    case CourseMapVisualLayer::VistaBackground:
        return static_cast<std::size_t>(CourseMapHybridGeometryLayer::Terrain);
    }
    return static_cast<std::size_t>(CourseMapHybridGeometryLayer::Terrain);
}

bool CourseMapHybridCartographyCompositor::SameSettings(
    const CourseMapHybridCartographySettings& lhs,
    const CourseMapHybridCartographySettings& rhs) noexcept {
    return lhs.enabled == rhs.enabled &&
        lhs.alwaysKeepCoarseAtCourseLod == rhs.alwaysKeepCoarseAtCourseLod &&
        lhs.alwaysKeepCoarseAtRegionLod == rhs.alwaysKeepCoarseAtRegionLod &&
        lhs.coverageGridResolution == rhs.coverageGridResolution &&
        lhs.terrainCoverageThreshold == rhs.terrainCoverageThreshold &&
        lhs.rockCoverageThreshold == rhs.rockCoverageThreshold &&
        lhs.structureCoverageThreshold == rhs.structureCoverageThreshold &&
        lhs.largestRegionThreshold == rhs.largestRegionThreshold;
}

} // namespace editor
