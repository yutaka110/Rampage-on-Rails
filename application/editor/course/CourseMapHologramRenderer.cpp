#include "CourseMapHologramRenderer.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
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

float Cross(Vector2 origin, Vector2 a, Vector2 b) {
    return (a.x - origin.x) * (b.y - origin.y) -
        (a.y - origin.y) * (b.x - origin.x);
}

std::vector<Vector2> ConvexHull(std::vector<Vector2> points) {
    std::sort(points.begin(), points.end(), [](Vector2 a, Vector2 b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end(), [](Vector2 a, Vector2 b) {
        return std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f;
    }), points.end());
    if (points.size() < 3u) return points;
    std::vector<Vector2> hull(points.size() * 2u);
    std::size_t count = 0;
    for (Vector2 point : points) {
        while (count >= 2u && Cross(hull[count - 2u], hull[count - 1u], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    const std::size_t lowerCount = count;
    for (std::size_t i = points.size() - 1u; i > 0u; --i) {
        const Vector2 point = points[i - 1u];
        while (count > lowerCount && Cross(hull[count - 2u], hull[count - 1u], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    if (count > 1u) --count;
    hull.resize(count);
    return hull;
}

float PolygonArea(const std::vector<Vector2>& points) {
    if (points.size() < 3u) return 0.0f;
    float twiceArea = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vector2 a = points[i];
        const Vector2 b = points[(i + 1u) % points.size()];
        twiceArea += a.x * b.y - b.x * a.y;
    }
    return std::abs(twiceArea) * 0.5f;
}

bool Intersects(const std::vector<Vector2>& points, const CourseOverviewMapRect& rect) {
    if (points.empty()) return false;
    float minX = points.front().x;
    float maxX = minX;
    float minY = points.front().y;
    float maxY = minY;
    for (Vector2 point : points) {
        minX = (std::min)(minX, point.x);
        maxX = (std::max)(maxX, point.x);
        minY = (std::min)(minY, point.y);
        maxY = (std::max)(maxY, point.y);
    }
    return maxX >= rect.x && minX <= rect.x + rect.width &&
        maxY >= rect.y && minY <= rect.y + rect.height;
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

bool SameSettings(const CourseMapHologramSettings& a,
    const CourseMapHologramSettings& b) {
    return a.enabled == b.enabled && a.showContours == b.showContours &&
        a.glow == b.glow && a.terrainOpacity == b.terrainOpacity &&
        a.structureOpacity == b.structureOpacity &&
        a.contourOpacity == b.contourOpacity &&
        a.maximumPolygons == b.maximumPolygons &&
        a.maximumContours == b.maximumContours;
}

void ResolveColors(CourseMapVisualLayer layer,
    const CourseMapHologramSettings& settings,
    uint32_t& fill, uint32_t& outline, uint32_t& glow) {
    switch (layer) {
    case CourseMapVisualLayer::GameplayTerrain:
        fill = Color(19, 125, 139, Alpha(settings.terrainOpacity));
        outline = Color(78, 222, 229, 190);
        glow = Color(30, 200, 220, 48);
        break;
    case CourseMapVisualLayer::HeroLandmark:
        fill = Color(30, 174, 192, Alpha(settings.terrainOpacity + 0.12f));
        outline = Color(132, 246, 250, 235);
        glow = Color(61, 229, 242, 72);
        break;
    case CourseMapVisualLayer::VistaBackground:
        fill = Color(18, 69, 91, Alpha(settings.terrainOpacity * 0.52f));
        outline = Color(51, 139, 166, 115);
        glow = Color(27, 126, 162, 26);
        break;
    case CourseMapVisualLayer::RockMass:
        fill = Color(30, 100, 112, Alpha(settings.structureOpacity * 0.82f));
        outline = Color(80, 177, 188, 160);
        glow = Color(38, 166, 182, 38);
        break;
    case CourseMapVisualLayer::SceneStructure:
        fill = Color(35, 137, 169, Alpha(settings.structureOpacity));
        outline = Color(101, 224, 244, 205);
        glow = Color(47, 199, 232, 58);
        break;
    }
}

bool LayerVisible(CourseMapVisualLayer layer, const CourseMapSemanticLODPolicy& lod) {
    if (layer == CourseMapVisualLayer::VistaBackground) return lod.showVistaTerrain;
    if (layer == CourseMapVisualLayer::RockMass) return lod.showRockInstances;
    return true;
}

} // namespace

const CourseMapHologramFrame& CourseMapHologramRenderer::Build(
    const CourseMapVisualAsset* asset,
    CourseMapVisualBakeStatus sourceStatus,
    const CourseOverviewMapProjection& projection) {
    static CourseMapHologramFrame fallbackFrame{};
    if (!settings_.enabled || sourceStatus != CourseMapVisualBakeStatus::Current ||
        asset == nullptr || !projection.State().valid) {
        fallbackFrame = {};
        fallbackFrame.rect = projection.State().rect;
        fallbackFrame.sourceStatus = sourceStatus;
        fallbackFrame.fallbackRequested = true;
        fallbackFrame.message = !settings_.enabled
            ? "Baked hologram rendering is disabled; using live visualization."
            : "Course Map visual is not current; using live visualization.";
        return fallbackFrame;
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
    if (cache.valid && SameKey(cache.key, key)) {
        ++lifetimeStats_.cacheHits;
        ++cache.frame.stats.cacheHits;
        return cache.frame;
    }
    cache.frame = BuildFrame(*asset, projection);
    cache.key = key;
    cache.valid = true;
    ++lifetimeStats_.builds;
    cache.frame.stats.builds = lifetimeStats_.builds;
    cache.frame.stats.cacheHits = lifetimeStats_.cacheHits;
    return cache.frame;
}

CourseMapHologramFrame CourseMapHologramRenderer::BuildFrame(
    const CourseMapVisualAsset& asset,
    const CourseOverviewMapProjection& projection) {
    CourseMapHologramFrame frame{};
    frame.valid = true;
    frame.fallbackRequested = false;
    frame.rect = projection.State().rect;
    frame.sourceStatus = CourseMapVisualBakeStatus::Current;
    const CourseMapSemanticLODPolicy lod = semanticLod_.Evaluate(projection);
    frame.semanticLod = lod.level;
    frame.stats.sourcePrimitives = static_cast<uint32_t>(asset.primitives.size());
    for (const CourseMapVisualPrimitive& primitive : asset.primitives) {
        if (frame.polygons.size() >= settings_.maximumPolygons) break;
        if (!LayerVisible(primitive.layer, lod)) {
            ++frame.stats.culledPolygons;
            continue;
        }
        std::vector<Vector2> projected;
        projected.reserve(primitive.worldCorners.size());
        for (const Vector3& point : primitive.worldCorners) {
            const CourseOverviewMapProjectedPoint value =
                projection.ProjectWorldScreenOnly(point);
            if (value.valid) projected.push_back(value.mapPosition);
        }
        projected = ConvexHull(std::move(projected));
        if (projected.size() < 3u || !Intersects(projected, frame.rect) ||
            PolygonArea(projected) < lod.minimumPolygonAreaPixels) {
            ++frame.stats.culledPolygons;
            continue;
        }
        CourseMapHologramPolygon polygon{};
        polygon.layer = primitive.layer;
        polygon.points = std::move(projected);
        polygon.stableId = primitive.stableId;
        polygon.locked = primitive.locked;
        ResolveColors(primitive.layer, settings_, polygon.fillColor,
            polygon.outlineColor, polygon.glowColor);
        polygon.outlineThickness = primitive.layer == CourseMapVisualLayer::HeroLandmark
            ? 1.8f : 1.1f;
        polygon.glowThickness = primitive.layer == CourseMapVisualLayer::HeroLandmark
            ? 5.5f : 3.8f;
        if (!settings_.glow) polygon.glowColor = 0u;
        frame.polygons.push_back(std::move(polygon));
    }
    frame.stats.visiblePolygons = static_cast<uint32_t>(frame.polygons.size());
    if (settings_.showContours) {
        for (const CourseMapVisualContour& contour : asset.contours) {
            if (frame.contours.size() >= settings_.maximumContours) break;
            if (lod.level == CourseMapSemanticLODLevel::Course && !contour.major) continue;
            std::vector<Vector2> points;
            points.reserve(contour.worldPoints.size());
            for (const Vector3& point : contour.worldPoints) {
                const CourseOverviewMapProjectedPoint value =
                    projection.ProjectWorldScreenOnly(point);
                if (value.valid) points.push_back(value.mapPosition);
            }
            if (points.size() < 2u || !Intersects(points, frame.rect)) continue;
            CourseMapHologramLineBatch line{};
            line.points = std::move(points);
            line.major = contour.major;
            line.color = Color(98, 224, 231,
                Alpha(settings_.contourOpacity * (contour.major ? 1.0f : 0.58f)));
            line.glowColor = settings_.glow
                ? Color(50, 202, 220, contour.major ? 42u : 22u) : 0u;
            line.thickness = contour.major ? 1.2f : 0.8f;
            line.glowThickness = contour.major ? 4.0f : 2.5f;
            frame.contours.push_back(std::move(line));
        }
    }
    frame.stats.visibleContours = static_cast<uint32_t>(frame.contours.size());
    frame.message = "Baked hologram [" + std::string(ToString(frame.semanticLod)) +
        "]: " + std::to_string(frame.stats.visiblePolygons) + " polygons, " +
        std::to_string(frame.stats.visibleContours) + " contours.";
    return frame;
}

bool CourseMapHologramSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(terrainOpacity, 0.0f, 1.0f) &&
        settings::InRange(structureOpacity, 0.0f, 1.0f) &&
        settings::InRange(contourOpacity, 0.0f, 1.0f) &&
        settings::InRange(maximumPolygons, 1u, 262144u) &&
        settings::InRange(maximumContours, 1u, 524288u);
    return settings::Result(valid, errorMessage,
        "CourseMapHologramSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapHologramRenderer::SetSettings(
    CourseMapHologramSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapHologramRenderer::Invalidate() noexcept {
    for (CacheEntry& cache : caches_) cache.valid = false;
}

bool CourseMapHologramRenderer::SameKey(
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

} // namespace editor
