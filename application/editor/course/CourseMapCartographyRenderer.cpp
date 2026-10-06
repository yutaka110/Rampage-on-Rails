#include "CourseMapCartographyRenderer.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace editor {
namespace {

uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) |
        (static_cast<uint32_t>(g) << 8u) |
        (static_cast<uint32_t>(b) << 16u) |
        (static_cast<uint32_t>(a) << 24u);
}

uint8_t Alpha(float value) {
    return static_cast<uint8_t>((std::clamp)(value, 0.0f, 1.0f) * 255.0f);
}

Vector3 Subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

float Length(Vector3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

float ScreenArea(Vector2 a, Vector2 b, Vector2 c) {
    return std::abs((b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x)) * 0.5f;
}

bool Intersects(const std::vector<Vector2>& points,
    const CourseOverviewMapRect& rect) {
    if (points.empty()) return false;
    float minimumX = points.front().x;
    float maximumX = minimumX;
    float minimumY = points.front().y;
    float maximumY = minimumY;
    for (Vector2 point : points) {
        minimumX = (std::min)(minimumX, point.x);
        maximumX = (std::max)(maximumX, point.x);
        minimumY = (std::min)(minimumY, point.y);
        maximumY = (std::max)(maximumY, point.y);
    }
    return maximumX >= rect.x && minimumX <= rect.x + rect.width &&
        maximumY >= rect.y && minimumY <= rect.y + rect.height;
}

std::vector<Vector3> BoxCorners(Vector3 minimum, Vector3 maximum) {
    std::vector<Vector3> result;
    result.reserve(8u);
    for (uint32_t corner = 0; corner < 8u; ++corner) {
        result.push_back({(corner & 1u) != 0u ? maximum.x : minimum.x,
            (corner & 2u) != 0u ? maximum.y : minimum.y,
            (corner & 4u) != 0u ? maximum.z : minimum.z});
    }
    return result;
}

bool ProjectedIntersects(const std::vector<Vector3>& worldPoints,
    const CourseOverviewMapProjection& projection,
    std::vector<Vector2>* projectedOutput = nullptr) {
    std::vector<Vector2> projected;
    projected.reserve(worldPoints.size());
    for (const Vector3& point : worldPoints) {
        const CourseOverviewMapProjectedPoint value =
            projection.ProjectWorldScreenOnly(point);
        if (value.valid) projected.push_back(value.mapPosition);
    }
    const bool visible = projected.size() >= 2u &&
        Intersects(projected, projection.State().rect);
    if (projectedOutput != nullptr) *projectedOutput = std::move(projected);
    return visible;
}

bool LayerVisible(CourseMapVisualLayer layer,
    const CourseMapSemanticLODPolicy& lod) {
    if (layer == CourseMapVisualLayer::VistaBackground) return lod.showVistaTerrain;
    if (layer == CourseMapVisualLayer::RockMass) return lod.showRockInstances;
    return true;
}

void ResolvePalette(CourseMapVisualLayer layer,
    const CourseMapCartographyRenderSettings& settings,
    uint8_t shade,
    uint32_t& fill,
    uint32_t& outline,
    uint32_t& glow) {
    const auto addShade = [shade](uint8_t value) {
        return static_cast<uint8_t>((std::min)(
            255u, static_cast<uint32_t>(value) + shade));
    };
    switch (layer) {
    case CourseMapVisualLayer::GameplayTerrain:
        fill = Color(18, addShade(105), addShade(122), Alpha(settings.terrainOpacity));
        outline = Color(75, 218, 226, 205);
        glow = Color(38, 200, 220, 48);
        break;
    case CourseMapVisualLayer::HeroLandmark:
        fill = Color(25, addShade(142), addShade(162),
            Alpha(settings.terrainOpacity + 0.12f));
        outline = Color(132, 246, 250, 240);
        glow = Color(61, 229, 242, 76);
        break;
    case CourseMapVisualLayer::VistaBackground:
        fill = Color(16, addShade(62), addShade(85), Alpha(settings.vistaOpacity));
        outline = Color(49, 135, 161, 125);
        glow = Color(27, 126, 162, 24);
        break;
    case CourseMapVisualLayer::RockMass:
        fill = Color(27, addShade(88), addShade(102),
            Alpha(settings.structureOpacity * 0.78f));
        outline = Color(78, 174, 188, 175);
        glow = Color(38, 166, 182, 38);
        break;
    case CourseMapVisualLayer::SceneStructure:
        fill = Color(31, addShade(118), addShade(153),
            Alpha(settings.structureOpacity));
        outline = Color(102, 225, 246, 215);
        glow = Color(47, 199, 232, 62);
        break;
    }
}

uint32_t TriangleBudget(CourseMapSemanticLODLevel level,
    const CourseMapCartographyRenderSettings& settings) {
    switch (level) {
    case CourseMapSemanticLODLevel::Course: return settings.courseTriangleBudget;
    case CourseMapSemanticLODLevel::Region: return settings.regionTriangleBudget;
    case CourseMapSemanticLODLevel::Detail: return settings.detailTriangleBudget;
    case CourseMapSemanticLODLevel::Inspect: return settings.inspectTriangleBudget;
    }
    return settings.courseTriangleBudget;
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

bool ProjectionSettingsEqualIgnoringPan(
    const CourseOverviewMapProjectionSettings& a,
    const CourseOverviewMapProjectionSettings& b) {
    return a.mode == b.mode && a.zoom == b.zoom &&
        a.paddingPixels == b.paddingPixels &&
        a.freeYawRadians == b.freeYawRadians &&
        a.freePitchRadians == b.freePitchRadians &&
        a.fitSamplesPerSegment == b.fitSamplesPerSegment;
}

bool SameSettings(const CourseMapCartographyRenderSettings& a,
    const CourseMapCartographyRenderSettings& b) {
    return a.enabled == b.enabled &&
        a.showSurfaceTriangles == b.showSurfaceTriangles &&
        a.showRegionOutlines == b.showRegionOutlines &&
        a.showFallbackGeometry == b.showFallbackGeometry &&
        a.glow == b.glow && a.terrainOpacity == b.terrainOpacity &&
        a.structureOpacity == b.structureOpacity &&
        a.vistaOpacity == b.vistaOpacity &&
        a.courseTriangleBudget == b.courseTriangleBudget &&
        a.regionTriangleBudget == b.regionTriangleBudget &&
        a.detailTriangleBudget == b.detailTriangleBudget &&
        a.inspectTriangleBudget == b.inspectTriangleBudget &&
        a.maximumOutlines == b.maximumOutlines;
}

} // namespace

const CourseMapCartographyFrame& CourseMapCartographyRenderer::Build(
    const CourseMapRegionAsset* asset,
    CourseMapCartographyBakeStatus sourceStatus,
    const CourseOverviewMapProjection& projection,
    CourseMapCartographyBuildOptions options) {
    if (!settings_.enabled || sourceStatus != CourseMapCartographyBakeStatus::Current ||
        asset == nullptr || !projection.State().valid) {
        const std::size_t fallbackIndex = (std::min)(
            static_cast<std::size_t>(projection.Settings().mode),
            fallbackFrames_.size() - 1u);
        CourseMapCartographyFrame& fallback = fallbackFrames_[fallbackIndex];
        fallback = {};
        fallback.rect = projection.State().rect;
        fallback.sourceStatus = sourceStatus;
        fallback.projectionMode = projection.Settings().mode;
        fallback.fallbackRequested = true;
        fallback.message = !settings_.enabled
            ? "Cartography rendering is disabled; using visual hologram fallback."
            : "Course Map regions are not current; using visual hologram fallback.";
        return fallback;
    }
    const std::size_t cacheIndex = static_cast<std::size_t>(projection.Settings().mode);
    CacheEntry& cache = caches_[(std::min)(cacheIndex, caches_.size() - 1u)];
    FrameKey key{};
    key.sourceFingerprint = asset->sourceFingerprint;
    key.contentRevision = asset->contentRevision;
    key.settingsRevision = settingsRevision_;
    key.semanticLodRevision = semanticLod_.SettingsRevision();
    key.sourceStatus = sourceStatus;
    key.projection = projection.Settings();
    key.rect = projection.State().rect;
    // Poll against the projection requested by this UI frame. A worker may
    // have completed for an older mouse position; publishing that result would
    // make the map jump backwards and immediately schedule another rebuild.
    PollBuild(cache, key);
    if (cache.valid && SameKey(cache.key, key)) {
        cache.frame.presentationOffset = {};
        ++lifetimeStats_.cacheHits;
        ++cache.frame.stats.cacheHits;
        return cache.frame;
    }
    if (!cache.pendingKey.has_value() && !options.interactivePan) {
        StartBuild(cache, key, AcquireSourceSnapshot(*asset), projection);
    } else if (!cache.pendingKey.has_value() && options.interactivePan) {
        ++lifetimeStats_.interactivePanDeferrals;
    }
    if (cache.valid) {
        cache.frame.presentationOffset = CanScreenTranslate(cache.key, key)
            ? ScreenTranslation(cache.key, key) : Vector2{};
        ++lifetimeStats_.retainedFramesWhileBuilding;
        if (cache.frame.presentationOffset.x != 0.0f ||
            cache.frame.presentationOffset.y != 0.0f) {
            ++lifetimeStats_.screenTranslatedFrames;
        }
        ++cache.frame.stats.retainedFramesWhileBuilding;
        cache.frame.stats.supersededBuildCompletions =
            lifetimeStats_.supersededBuildCompletions;
        cache.frame.stats.interactivePanDeferrals =
            lifetimeStats_.interactivePanDeferrals;
        cache.frame.stats.screenTranslatedFrames =
            lifetimeStats_.screenTranslatedFrames;
        return cache.frame;
    }
    CourseMapCartographyFrame& fallback = fallbackFrames_[cacheIndex];
    fallback = {};
    fallback.rect = projection.State().rect;
    fallback.sourceStatus = sourceStatus;
    fallback.projectionMode = projection.Settings().mode;
    fallback.fallbackRequested = true;
    fallback.message =
        "Building Course Map cartography asynchronously; visual fallback remains interactive.";
    fallback.stats = lifetimeStats_;
    return fallback;
}

CourseMapCartographyFrame CourseMapCartographyRenderer::BuildFrame(
    const CourseMapRegionAsset& asset,
    const CourseOverviewMapProjection& projection,
    const CourseMapCartographyRenderSettings& settings,
    const CourseMapSemanticLODPolicy& lod) {
    CourseMapCartographyFrame frame{};
    frame.valid = true;
    frame.fallbackRequested = false;
    frame.rect = projection.State().rect;
    frame.sourceStatus = CourseMapCartographyBakeStatus::Current;
    frame.projectionMode = projection.Settings().mode;
    frame.semanticLod = lod.level;
    frame.stats.sourceRegions = static_cast<uint32_t>(asset.regions.size());

    std::vector<uint32_t> candidateIndices;
    std::vector<bool> candidateMarks(asset.regions.size(), false);
    const auto addCandidate = [&](uint32_t index) {
        if (index >= asset.regions.size() || candidateMarks[index]) return;
        candidateMarks[index] = true;
        candidateIndices.push_back(index);
    };
    if (!asset.tiles.empty()) {
        for (const CourseMapRegionTile& tile : asset.tiles) {
            if (!ProjectedIntersects(BoxCorners(tile.worldMinimum, tile.worldMaximum),
                    projection)) {
                ++frame.stats.culledTiles;
                continue;
            }
            ++frame.stats.visibleTiles;
            for (uint32_t index : tile.regionIndices) addCandidate(index);
        }
    } else {
        for (uint32_t i = 0; i < asset.regions.size(); ++i) addCandidate(i);
    }
    std::sort(candidateIndices.begin(), candidateIndices.end());

    const uint32_t triangleBudget = TriangleBudget(frame.semanticLod, settings);
    frame.triangles.reserve(triangleBudget);
    for (uint32_t regionIndex : candidateIndices) {
        if (regionIndex >= asset.regions.size()) continue;
        const CourseMapRegion& region = asset.regions[regionIndex];
        if (!LayerVisible(region.layer, lod) ||
            (!region.exactSourceGeometry && !settings.showFallbackGeometry)) {
            ++frame.stats.culledRegions;
            continue;
        }
        std::vector<Vector2> projectedFootprint;
        if (!ProjectedIntersects(region.footprint, projection, &projectedFootprint)) {
            ++frame.stats.culledRegions;
            continue;
        }
        ++frame.stats.visibleRegions;
        if (region.exactSourceGeometry) ++frame.stats.visibleExactRegions;
        else ++frame.stats.visibleFallbackRegions;

        uint32_t outlineColor = 0;
        uint32_t fillColor = 0;
        uint32_t glowColor = 0;
        ResolvePalette(region.layer, settings, 0u,
            fillColor, outlineColor, glowColor);
        if (settings.showRegionOutlines &&
            frame.outlines.size() < settings.maximumOutlines &&
            projectedFootprint.size() >= 3u) {
            CourseMapCartographyOutline outline{};
            outline.points = std::move(projectedFootprint);
            outline.color = outlineColor;
            outline.glowColor = settings.glow ? glowColor : 0u;
            outline.thickness = region.exactSourceGeometry ? 1.35f : 0.95f;
            outline.glowThickness = region.layer == CourseMapVisualLayer::HeroLandmark
                ? 5.2f : 3.4f;
            outline.locked = region.locked;
            outline.exactSourceGeometry = region.exactSourceGeometry;
            outline.stableId = region.stableId;
            outline.layer = region.layer;
            frame.outlines.push_back(std::move(outline));
        }
        if (!settings.showSurfaceTriangles) continue;
        const std::size_t sourceTriangleCount = region.indices.size() / 3u;
        const uint32_t remaining = triangleBudget > frame.stats.inspectedTriangles
            ? triangleBudget - frame.stats.inspectedTriangles : 0u;
        if (remaining == 0u) {
            frame.stats.budgetCulledTriangles +=
                static_cast<uint32_t>(sourceTriangleCount);
            continue;
        }
        const std::size_t inspectCount = (std::min)(
            sourceTriangleCount, static_cast<std::size_t>(remaining));
        frame.stats.budgetCulledTriangles += static_cast<uint32_t>(
            sourceTriangleCount - inspectCount);
        for (std::size_t inspected = 0; inspected < inspectCount; ++inspected) {
            ++frame.stats.inspectedTriangles;
            const std::size_t sourceTriangle = inspectCount == sourceTriangleCount
                ? inspected : (inspected * sourceTriangleCount) / inspectCount;
            const std::size_t offset = sourceTriangle * 3u;
            const uint32_t ia = region.indices[offset];
            const uint32_t ib = region.indices[offset + 1u];
            const uint32_t ic = region.indices[offset + 2u];
            if (ia >= region.worldVertices.size() ||
                ib >= region.worldVertices.size() ||
                ic >= region.worldVertices.size()) continue;
            const Vector3 wa = region.worldVertices[ia];
            const Vector3 wb = region.worldVertices[ib];
            const Vector3 wc = region.worldVertices[ic];
            const CourseOverviewMapProjectedPoint pa =
                projection.ProjectWorldScreenOnly(wa);
            const CourseOverviewMapProjectedPoint pb =
                projection.ProjectWorldScreenOnly(wb);
            const CourseOverviewMapProjectedPoint pc =
                projection.ProjectWorldScreenOnly(wc);
            if (!pa.valid || !pb.valid || !pc.valid ||
                ScreenArea(pa.mapPosition, pb.mapPosition, pc.mapPosition) < 0.04f) {
                continue;
            }
            const Vector3 normal = Cross(Subtract(wb, wa), Subtract(wc, wa));
            const float normalLength = Length(normal);
            const float upward = normalLength > 0.00001f
                ? std::abs(normal.y) / normalLength : 0.0f;
            const float normalizedHeight = region.maximumHeight > region.minimumHeight
                ? (std::clamp)(((wa.y + wb.y + wc.y) / 3.0f -
                    region.minimumHeight) /
                    (region.maximumHeight - region.minimumHeight), 0.0f, 1.0f)
                : 0.5f;
            const uint8_t shade = static_cast<uint8_t>(
                (std::clamp)(upward * 22.0f + normalizedHeight * 18.0f,
                    0.0f, 40.0f));
            uint32_t triangleFill = 0;
            uint32_t ignoredOutline = 0;
            uint32_t ignoredGlow = 0;
            ResolvePalette(region.layer, settings, shade,
                triangleFill, ignoredOutline, ignoredGlow);
            frame.triangles.push_back({pa.mapPosition, pb.mapPosition,
                pc.mapPosition, triangleFill,
                (pa.depth + pb.depth + pc.depth) / 3.0f,
                region.stableId, region.layer, region.exactSourceGeometry});
        }
    }
    std::stable_sort(frame.triangles.begin(), frame.triangles.end(),
        [](const CourseMapCartographyTriangle& a,
           const CourseMapCartographyTriangle& b) {
            return a.depth > b.depth;
        });
    frame.stats.emittedTriangles = static_cast<uint32_t>(frame.triangles.size());
    frame.message = "Cartography [" + std::string(ToString(frame.semanticLod)) +
        "]: " + std::to_string(frame.stats.visibleRegions) + " regions, " +
        std::to_string(frame.stats.emittedTriangles) + " real-shape triangles.";
    return frame;
}

bool CourseMapCartographyRenderer::PollBuild(
    CacheEntry& cache,
    const FrameKey& latestRequestedKey) {
    if (!cache.pendingKey.has_value() || !cache.buildFuture.valid() ||
        cache.buildFuture.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready) {
        return false;
    }
    CourseMapCartographyFrame completed{};
    bool succeeded = true;
    try {
        completed = cache.buildFuture.get();
    } catch (const std::exception& error) {
        completed.message = std::string("Course Map cartography worker failed: ") +
            error.what();
        succeeded = false;
    } catch (...) {
        completed.message = "Course Map cartography worker failed with an unknown error.";
        succeeded = false;
    }
    const FrameKey completedKey = *cache.pendingKey;
    const uint64_t completedEpoch = cache.pendingEpoch;
    cache.pendingKey.reset();
    ++lifetimeStats_.asynchronousBuildCompletions;
    if (!succeeded || completedEpoch != invalidationEpoch_ ||
        !SameKey(completedKey, latestRequestedKey)) {
        if (succeeded) ++lifetimeStats_.supersededBuildCompletions;
        return false;
    }
    ++lifetimeStats_.builds;
    completed.generation = lifetimeStats_.builds;
    completed.presentationOffset = {};
    cache.key = completedKey;
    cache.frame = std::move(completed);
    cache.valid = cache.frame.valid;
    cache.frame.stats.builds = lifetimeStats_.builds;
    cache.frame.stats.cacheHits = lifetimeStats_.cacheHits;
    cache.frame.stats.asynchronousBuildStarts =
        lifetimeStats_.asynchronousBuildStarts;
    cache.frame.stats.asynchronousBuildCompletions =
        lifetimeStats_.asynchronousBuildCompletions;
    cache.frame.stats.retainedFramesWhileBuilding =
        lifetimeStats_.retainedFramesWhileBuilding;
    cache.frame.stats.supersededBuildCompletions =
        lifetimeStats_.supersededBuildCompletions;
    cache.frame.stats.interactivePanDeferrals =
        lifetimeStats_.interactivePanDeferrals;
    cache.frame.stats.screenTranslatedFrames =
        lifetimeStats_.screenTranslatedFrames;
    return cache.valid;
}

bool CourseMapCartographyRenderer::StartBuild(
    CacheEntry& cache,
    FrameKey key,
    std::shared_ptr<const CourseMapRegionAsset> asset,
    const CourseOverviewMapProjection& projection) {
    if (cache.pendingKey.has_value() || asset == nullptr) return false;
    CourseOverviewMapProjection projectionSnapshot =
        projection.MakeBackgroundSnapshot();
    const CourseMapCartographyRenderSettings settingsSnapshot = settings_;
    const CourseMapSemanticLODPolicy lodSnapshot =
        semanticLod_.Evaluate(projection);
    try {
        cache.buildFuture = std::async(std::launch::async,
            [asset = std::move(asset),
             projection = std::move(projectionSnapshot),
             settingsSnapshot, lodSnapshot]() {
                return BuildFrame(
                    *asset, projection, settingsSnapshot, lodSnapshot);
            });
        cache.pendingKey = std::move(key);
        cache.pendingEpoch = invalidationEpoch_;
        ++lifetimeStats_.asynchronousBuildStarts;
        return true;
    } catch (const std::exception&) {
        cache.pendingKey.reset();
        return false;
    }
}

std::shared_ptr<const CourseMapRegionAsset>
CourseMapCartographyRenderer::AcquireSourceSnapshot(
    const CourseMapRegionAsset& asset) {
    if (sourceSnapshot_ != nullptr &&
        sourceSnapshotFingerprint_ == asset.sourceFingerprint &&
        sourceSnapshotContentRevision_ == asset.contentRevision) {
        return sourceSnapshot_;
    }
    sourceSnapshot_ = std::make_shared<CourseMapRegionAsset>(asset);
    sourceSnapshotFingerprint_ = asset.sourceFingerprint;
    sourceSnapshotContentRevision_ = asset.contentRevision;
    return sourceSnapshot_;
}

bool CourseMapCartographyRenderSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(terrainOpacity, 0.0f, 1.0f) &&
        settings::InRange(structureOpacity, 0.0f, 1.0f) &&
        settings::InRange(vistaOpacity, 0.0f, 1.0f) &&
        settings::InRange(courseTriangleBudget, 1u, 1000000u) &&
        settings::InRange(regionTriangleBudget, 1u, 1000000u) &&
        settings::InRange(detailTriangleBudget, 1u, 1000000u) &&
        settings::InRange(inspectTriangleBudget, 1u, 1000000u) &&
        settings::InRange(maximumOutlines, 1u, 262144u);
    return settings::Result(valid, errorMessage,
        "CourseMapCartographyRenderSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapCartographyRenderer::SetSettings(
    CourseMapCartographyRenderSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapCartographyRenderer::Invalidate() noexcept {
    ++invalidationEpoch_;
    // Keep the last completed front frame visible. Any in-flight result from
    // the previous epoch is discarded and the next Build schedules a fresh
    // back frame without blanking the viewport.
}

bool CourseMapCartographyRenderer::SameKey(
    const FrameKey& lhs, const FrameKey& rhs) noexcept {
    return lhs.sourceFingerprint == rhs.sourceFingerprint &&
        lhs.contentRevision == rhs.contentRevision &&
        lhs.settingsRevision == rhs.settingsRevision &&
        lhs.semanticLodRevision == rhs.semanticLodRevision &&
        lhs.sourceStatus == rhs.sourceStatus &&
        lhs.rect.x == rhs.rect.x && lhs.rect.y == rhs.rect.y &&
        lhs.rect.width == rhs.rect.width && lhs.rect.height == rhs.rect.height &&
        ProjectionSettingsEqual(lhs.projection, rhs.projection);
}

bool CourseMapCartographyRenderer::CanScreenTranslate(
    const FrameKey& retained,
    const FrameKey& requested) noexcept {
    return retained.sourceFingerprint == requested.sourceFingerprint &&
        retained.contentRevision == requested.contentRevision &&
        retained.settingsRevision == requested.settingsRevision &&
        retained.semanticLodRevision == requested.semanticLodRevision &&
        retained.sourceStatus == requested.sourceStatus &&
        retained.rect.x == requested.rect.x &&
        retained.rect.y == requested.rect.y &&
        retained.rect.width == requested.rect.width &&
        retained.rect.height == requested.rect.height &&
        ProjectionSettingsEqualIgnoringPan(
            retained.projection, requested.projection);
}

Vector2 CourseMapCartographyRenderer::ScreenTranslation(
    const FrameKey& retained,
    const FrameKey& requested) noexcept {
    return {
        requested.projection.panPixels.x - retained.projection.panPixels.x,
        requested.projection.panPixels.y - retained.projection.panPixels.y};
}

} // namespace editor
